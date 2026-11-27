#include <iostream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <sys/mman.h>
#include <syslog.h>
#include <openbmc/pal.h>
#include <openbmc/kv.h>
#include "bmc.h"

using namespace std;

#define BMC_RW_OFFSET               (64 * 1024)
#define ROMX_SIZE                   (84 * 1024)
#define META_SIZE                   (64 * 1024)
// image-meta offset within a full flash image; fixed for every platform by
// FBOBMC_IMAGE_META_OFFSET in meta-facebook/classes/fbobmc-image.bbclass.
#define META_OFFSET                 (960 * 1024)

int BmcComponent::update(const string& image)
{
  string dev;
  int ret;
  string flash_image = image;
  stringstream cmd_str;
  const string& comp = this->component();
  char key[MAX_KEY_LEN] = {0}, value[MAX_VALUE_LEN] = {0};

  if (_mtd_name == "") {
    // Upgrade not supported
    return FW_STATUS_NOT_SUPPORTED;
  }

  if (is_valid(image, false) == false) {
    sys().error << image<< " is not a valid BMC image for " << sys().name() << endl;
    syslog(LOG_CRIT, "Updating %s Fail. File: %s is not a valid image", comp.c_str(), image.c_str());
    return FW_STATUS_FAILURE;
  }

  if (!sys().get_mtd_name(_mtd_name, dev)) {
    sys().error << "Failed to get device for " << _mtd_name << endl;
    return FW_STATUS_FAILURE;
  }

  syslog(LOG_CRIT, "BMC fw upgrade initiated");

  sys().output << "Flashing to device: " << dev << endl;
  if (_skip_offset > 0 || _writable_offset > 0) {
    flash_image = image + "-tmp";
    // Ensure that we are not overwriting an existing file.
    while (access( flash_image.c_str(), F_OK  ) != -1) {
      flash_image.append("_");
    }
    // Open input image and seek skipping to the writable offset.
    int fd_r = open(image.c_str(), O_RDONLY);
    if (fd_r < 0) {
      sys().error << "Cannot open " << image<< " for reading" << endl;
      return FW_STATUS_FAILURE;
    }
    if (lseek(fd_r, _skip_offset, SEEK_SET) != (off_t)_skip_offset) {
      close(fd_r);
      sys().error << "Cannot seek " << image << endl;
      return FW_STATUS_FAILURE;
    }
    // create tmp file for writing.
    int fd_w = open(flash_image.c_str(), O_WRONLY | O_CREAT, 0666);
    if (fd_w < 0) {
      sys().error << "Cannot write to " << flash_image << endl;
      close(fd_r);
      return FW_STATUS_FAILURE;
    }
    uint8_t buf[1024];
    size_t r_b;

    if (_skip_offset > _writable_offset) {
      size_t copy = _skip_offset - _writable_offset;
      int fd_d = open(dev.c_str(), O_RDONLY);
      if (fd_d < 0) {
        close(fd_r);
        close(fd_w);
        return FW_STATUS_FAILURE;
      }
      while (copy > 0) {
        size_t to_copy = copy > sizeof(buf) ? sizeof(buf) : copy;
        r_b = read(fd_d, buf, to_copy);
        if (r_b != to_copy) {
          close(fd_r);
          close(fd_w);
          close(fd_d);
          return FW_STATUS_FAILURE;
        }
        if (write(fd_w, buf, to_copy) != (int)to_copy) {
          close(fd_r);
          close(fd_w);
          close(fd_d);
          return FW_STATUS_FAILURE;
        }
        copy -= to_copy;
      }
      close(fd_d);
    }

    // Copy from r to w.
    while ((r_b = read(fd_r, buf, 1024)) > 0) {
      if (write(fd_w, buf, r_b) != (int)r_b) {
        close(fd_r);
        close(fd_w);
        return -1;
      }
    }
    close(fd_r);
    close(fd_w);
  }

  snprintf(key, sizeof(key), "cur_%s_ver", _mtd_name.c_str());
  if ((kv_get(key, value, NULL, 0) < 0) && (errno == ENOENT)) { // no update before
    kv_set(key, get_bmc_version().c_str(), 0, 0);
  }

  cmd_str << "flashcp -v " << flash_image << " " << dev;
  ret = sys().runcmd(cmd_str.str());
  if (_writable_offset > 0) {
    // this is a temp. file, remove it.
    remove(flash_image.c_str());
  }

  // If flashcp cmd was successful, keep historical info that BMC fw was upgraded
  if (ret == 0) {
    syslog(LOG_CRIT, "BMC fw upgrade completed. Version: %s", get_bmc_version().c_str());
  }

  return ret;
}

// Parse the FBOBMC image-meta JSON blob at "offset" of "mtd" and return
// version_infos.fw_ver, or "NA" if it is absent or malformed.
std::string BmcComponent::get_version_from_image_meta(const std::string &mtd, off_t offset)
{
  std::string bmc_ver = "NA";
  FILE *fp = NULL;
  char *line = NULL;

  do {
    if (!(fp = fopen(mtd.c_str(), "r")))
      break;

    if (offset > 0 && fseeko(fp, offset, SEEK_SET) != 0)
      break;

    if (!(line = (char *)malloc(META_SIZE)))
      break;

    if (fgets(line, META_SIZE, fp)) {
      try {
        json meta = json::parse(string(line));
        bmc_ver = meta["version_infos"]["fw_ver"];
      } catch (json::exception& e) {
        syslog(LOG_ERR, "%s: no valid image-meta at offset 0x%llx: %s",
               mtd.c_str(), (unsigned long long)offset, e.what());
      }
    }
  } while (0);
  if (fp)
    fclose(fp);
  if (line)
    free(line);

  return bmc_ver;
}

std::string BmcComponent::get_bmc_version()
{
  std::string bmc_ver = "NA";
  std::string mtd;

  if ((_vers_mtd == "u-bootro" && sys().get_mtd_name(string("metaro"), mtd)) ||
      (_vers_mtd == "u-boot" && sys().get_mtd_name(string("meta"), mtd))) {
    bmc_ver = get_version_from_image_meta(mtd, 0);
    if (bmc_ver != "NA")
      return bmc_ver;
  }

  if (!sys().get_mtd_name(_vers_mtd, mtd)) {
    // The version partition does not exist. Some platforms expose the
    // second flash only as a flat "flash1" partition with no "u-bootx"/"metax"
    // children. Fall back to reading the image-meta json blob at its fixed
    // offset within the flash.
    // Returns "NA" if the flash has no image-meta there.
    if (_vers_mtd != "" && _mtd_name != "" &&
        sys().get_mtd_name(_mtd_name, mtd)) {
      bmc_ver = get_version_from_image_meta(mtd, META_OFFSET);
    }
    return bmc_ver;
  }
  return get_bmc_version(mtd);
}

std::string BmcComponent::get_bmc_version(const std::string &mtd)
{
  // parsering the image to get the version string
  std::string bmc_ver = "NA";
  FILE *fp;

  const std::string cmd = "dd if=" + mtd +
      " bs=64k count=6 2>/dev/null | strings | grep -E 'U-Boot (SPL ){,}20[[:digit:]]{2}\\.[[:digit:]]{2}'";
  fp = popen(cmd.c_str(), "r");
  if (fp) {
    char line[256];
    char *ver = (char*) malloc(256);
    while (fgets(line, sizeof(line), fp)) {
      int ret;
      ret = sscanf(line, "U-Boot%*[^2]20%*2d.%*2d%*[ ]%s[^ \n]%*[ ](%*[^)])\n", ver);
      if (1 == ret) {
        bmc_ver = ver;
        break;
      }
    }
    if (ver) {
       free(ver);
    }
    pclose(fp);
  }

  return bmc_ver;
}

int BmcComponent::print_version()
{
  string mtd;
  char key[MAX_KEY_LEN] = {0}, value[MAX_VALUE_LEN] = {0};
  int ret = 0;

  string comp = _component;
  std::transform(comp.begin(), comp.end(),comp.begin(), ::toupper);

  if (_vers_mtd == "") {
    sys().output << comp << " Version: " << sys().version() << endl;
    return FW_STATUS_SUCCESS;
  }

  snprintf(key, sizeof(key), "cur_%s_ver", _mtd_name.c_str());
  ret = kv_get(key, value, NULL, 0);
  if ((ret < 0) && (errno == ENOENT)) { // no update before
    sys().output << comp << " Version: " << get_bmc_version() << endl;
  } else if (ret == 0) {
    sys().output << comp << " Version: " << value << endl;
  }

  sys().output << comp << " Version After Activated: " << get_bmc_version() << endl;

  return FW_STATUS_SUCCESS;
}

int BmcComponent::get_version(json& j) {
  if (_vers_mtd == "") {
    j["VERSION"] = sys().version();
  } else {
    j["VERSION"] = get_bmc_version();
  }
  return FW_STATUS_SUCCESS;
}

class SystemConfig {
  public:
    bool dual_flash;
    System system;
  SystemConfig() : system() {
    // If flash1 exists, then we have two flashes
    if (system.get_mtd_name("flash1")) {
      dual_flash = true;
    } else {
      dual_flash = false;
    }

    int vboot = system.vboot_support_status();
    if (dual_flash) {
      if (vboot != VBOOT_NO_SUPPORT)  {
        // If verified boot is enabled, we should
        // have the romx partition
        if (vboot == VBOOT_HW_ENFORCE) {
          // Locked down, Update from a 64k offset.
          static BmcComponent bmc("bmc", "bmc", "flash1rw", "u-boot", BMC_RW_OFFSET, ROMX_SIZE);
          // Locked down, Allow getting version of ROM, but do not allow
          // upgrading it.
          static BmcComponent rom("bmc", "rom", "", "u-bootro");
        } else {
          // Unlocked Flash, Upgrade the full BMC flash1.
          static BmcComponent bmc("bmc", "bmc", "flash1", "u-boot");
          // Unlocked flash, Allow upgrading the ROM.
          static BmcComponent rom("bmc", "rom", "flash0", "u-bootro");
        }
      } else {
        // non-verified-boot. BMC boots off of flash0.
        static BmcComponent bmc("bmc", "bmc", "flash0", "u-boot");
        // Dual flash supported, but not in verified boot format.
        // Allow flashing the second flash. Read version from u-boot partition.
        static BmcComponent bmcalt("bmc", "altbmc", "flash1", "u-bootx");
      }
      // Verified boot supported and in dual-flash mode.
    } else {
      // We just have the one flash. Allow upgrading flash0.
      if (vboot != VBOOT_NO_SUPPORT) {
        // the vboot status "VBOOT_HW_ENFORCE" cannot be used for judgement here.
        // Therefore, create an unlocked flash component.
        static BmcComponent rom("bmc", "rom", "flash0", "u-bootro");
      } else {
        static BmcComponent bmc("bmc", "bmc", "flash0", "u-boot");
      }
    }
  }
};

SystemConfig bmc_sysconf;
