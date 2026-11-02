/*
 * cfg-util
 *
 * Copyright 2015-present Facebook. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <openbmc/pal.h>
#include <sys/reboot.h>
#include <syslog.h>
#include <sys/mman.h>
#include <openbmc/kv.h>
#include <openbmc/misc-utils.h>

#define PAGE_SIZE                              0x1000
#define AST_SRAM_BMC_REBOOT_BASE               0x1E721000
#define AST_SRAM_BMC_REBOOT_OFFSET             0x200
#define AST_G6_SRAM_BMC_REBOOT_BASE            0x10015000
#define AST_G6_SRAM_BMC_REBOOT_OFFSET          0xC00
#define BMC_REBOOT_BY_CMD(base, offset)        *((uint32_t *)(base + (offset + 0x4)))

#define BIT_RECORD_LOG                (1 << 8)
#define FLAG_CFG_UTIL                 (1 << 1)

static void
print_usage(void) {
  printf("Usage: cfg-util <dump-all|key> <value>\n");
  printf("       cfg-util --clear\n");
}

static int
set_ntp_server(const char *server_name)
{
  char cmd[MAX_VALUE_LEN + 64] = {0};
  char ntp_server_new[MAX_VALUE_LEN] = {0};
  char ntp_server_old[MAX_VALUE_LEN] = {0};

  // Remove old NTP server
  if (kv_get("ntp_server", ntp_server_old, NULL, KV_FPERSIST) == 0 &&
     strlen(ntp_server_old) > 2) {
    snprintf(cmd, sizeof(cmd), "sed -i '/^restrict %s$/d' /etc/ntp.conf", ntp_server_old);
    if (system(cmd) != 0) {
      syslog(LOG_WARNING, "NTP: restrict conf not removed\n");
    }
    snprintf(cmd, sizeof(cmd), "sed -i '/^server %s iburst$/d' /etc/ntp.conf", ntp_server_old);
    if (system(cmd) != 0) {
      syslog(LOG_WARNING, "NTP: Server conf not removed\n");
    }
  }
  // Add new NTP server
  snprintf(ntp_server_new, MAX_VALUE_LEN, "%s", server_name);
  if (strlen(ntp_server_new) > 2) {
    snprintf(cmd, sizeof(cmd), "echo \"restrict %s\" >> /etc/ntp.conf", ntp_server_new);
    if (system(cmd) != 0) {
      syslog(LOG_ERR, "NTP: restrict conf not added\n");
    }
    snprintf(cmd, sizeof(cmd), "echo \"server %s iburst\" >> /etc/ntp.conf", ntp_server_new);
    if (system(cmd) != 0) {
      syslog(LOG_ERR, "NTP: server conf not added\n");
    }
  }
  // Restart NTP server
  snprintf(cmd, sizeof(cmd), "/etc/init.d/ntpd restart > /dev/null &");
  if (system(cmd) != 0) {
    syslog(LOG_ERR, "NTP server restart failed\n");
    return -1;
  }
  if (kv_set("ntp_server", server_name, 0, KV_FPERSIST)) {
    return -1;
  }

  return 0;
}

int
main(int argc, char **argv) {

  int ret;
  int check_key;
  int mem_fd;
  char key[MAX_KEY_LEN] = {0};
  char val[MAX_VALUE_LEN] = {0};
  uint8_t *bmc_reboot_base;
  uint32_t sram_bmc_reboot_base = 0x0;
  uint32_t sram_offset = 0x0;

  // Handle boundary checks
  if (argc < 2 || argc > 3) {
    goto err_exit;
  }

  // Handle dump of key value data base
  if ((argc == 2) && (!strcmp(argv[1], "dump-all"))){
      pal_dump_key_value();
      return 0;
  }

  // Handle Reset Default factory settings
  if ((argc == 2) && (!strcmp(argv[1], "--clear"))){
      if (pal_is_fw_update_ongoing_system()) {
        printf("FW update is ongoing, block the command.\n");
        return -1;
      }

      // Prevent important information from being cleared
      // leading to the loss of some functionalities.
      // Store the kv and restore after clear.
      pal_store_key_value();

      printf("Reset BMC data to default factory settings and BMC will be reset...\n");
      if(system("rm /mnt/data/* -rf > /dev/null 2>&1") != 0) {
        printf("Cleaning persistent storage failed!\n");
        return 1;
      }
      sync();

      pal_restore_key_value();

      // No AST2700 mapping for the SRAM reboot-cause handoff is
      // established yet.
      if (get_soc_model() != SOC_MODEL_ASPEED_G7) {
        mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mem_fd >= 0) {
          if (get_soc_model() == SOC_MODEL_ASPEED_G6) {
            sram_bmc_reboot_base = AST_G6_SRAM_BMC_REBOOT_BASE;
            sram_offset = AST_G6_SRAM_BMC_REBOOT_OFFSET;
          } else {
            sram_bmc_reboot_base = AST_SRAM_BMC_REBOOT_BASE;
            sram_offset = AST_SRAM_BMC_REBOOT_OFFSET;
          }

          bmc_reboot_base = (uint8_t *)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, sram_bmc_reboot_base);
          if (bmc_reboot_base != 0) {
            BMC_REBOOT_BY_CMD(bmc_reboot_base, sram_offset) |= BIT_RECORD_LOG | FLAG_CFG_UTIL;
            munmap(bmc_reboot_base, PAGE_SIZE);
          }
          close(mem_fd);
        }
      }

      sleep(3);
      pal_bmc_reboot(RB_AUTOBOOT);
      return 0;
  }

  // Handle Get the Configuration
  if (argc == 2) {
    snprintf(key, MAX_KEY_LEN, "%s", argv[1]);

    ret = pal_get_key_value(key, val);
    if (ret) {
      goto err_exit;
    }

    printf("%s\n", val);
    return 0;
  }

  // Handle Set the configuration
  snprintf(key, MAX_KEY_LEN, "%s", argv[1]);
  snprintf(val, MAX_VALUE_LEN, "%s", argv[2]);

  // Common universal keys
  if (!strcmp(key, "ntp_server")) {
    ret = set_ntp_server(val);
  } else {
    // Platform specific key handling
    check_key = pal_cfg_key_check(key);
    if (check_key == 0) {
      ret = pal_set_key_value(key, val);
    } else {
      printf("cfg-util: %s cannot be set using cfg-util\n", key);
      ret = -1;
    }
  }
  return ret;
err_exit:
  print_usage();
  exit(-1);
}
