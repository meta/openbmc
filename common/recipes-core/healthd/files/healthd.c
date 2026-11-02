/*
 * healthd
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
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <pthread.h>
#include <jansson.h>
#include <stdbool.h>
#include <openbmc/pal.h>
#include <sys/sysinfo.h>
#include <sys/reboot.h>
#include <openbmc/watchdog.h>
#include <openbmc/kv.h>
#include <openbmc/obmc-i2c.h>
#include <openbmc/vbs.h>
#include <openbmc/misc-utils.h>
#include <signal.h>
#include <limits.h>
#include "healthd.h"

#define I2C_BUS_NUM            14
#define AST_I2C_BASE           0x1E78A000  /* I2C */
#define I2C_CMD_REG            0x14
#define AST_I2CD_SCL_LINE_STS  (0x1 << 18)
#define AST_I2CD_SDA_LINE_STS  (0x1 << 17)
#define AST_I2CD_BUS_BUSY_STS  (0x1 << 16)
#define PAGE_SIZE              0x1000
#define MIN_THRESHOLD          60.0
#define MAX_THRESHOLD          95.0
#define VERSION_HISTORY_COUNT  (4)

#define CPU_INFO_PATH "/proc/stat"
#define CPU_NAME_LENGTH 10
#define DEFAULT_WINDOW_SIZE 120
#define DEFAULT_MONITOR_INTERVAL 1
#define HEALTHD_MAX_RETRY 10
#define CONFIG_PATH "/etc/healthd-config.json"

#define AST_MCR_BASE 0x1e6e0000 // Base Address of SDRAM Memory Controller
#define INTR_CTRL_STS_OFFSET 0x50 // Interrupt Control/Status Register
#define ADDR_FIRST_UNRECOVER_ECC_OFFSET 0x58 // Address of First Un-Recoverable ECC Error Addr
#define ADDR_LAST_RECOVER_ECC_OFFSET 0x5c // Address of Last Recoverable ECC Error Addr
#define MAX_ECC_RECOVERABLE_ERROR_COUNTER 255
#define MAX_ECC_UNRECOVERABLE_ERROR_COUNTER 15

#define EDAC_SYSFS_PATH_DEFAULT "/sys/devices/system/edac/mc/mc0"
#define EDAC_CE_COUNT_FILE      "ce_count"
#define EDAC_UE_COUNT_FILE      "ue_count"
#define EDAC_RESET_COUNTERS_FILE "reset_counters"
#define ECC_SOURCE_MCR          0
#define ECC_SOURCE_EDAC         1

#define BMC_HEALTH_FILE "bmc_health"
#define HEALTH "1"
#define NOT_HEALTH "0"

#define VM_PANIC_ON_OOM_FILE "/proc/sys/vm/panic_on_oom"

/* Identify BMC reboot cause */
#define AST_SRAM_BMC_REBOOT_BASE               0x1E721000
#define AST_SRAM_BMC_REBOOT_OFFSET             0x200
#define AST_G6_SRAM_BMC_REBOOT_BASE            0x10015000
#define AST_G6_SRAM_BMC_REBOOT_OFFSET          0xC00
#define BMC_REBOOT_BY_KERN_PANIC(base, offset) *((uint32_t *)(base + (offset)))
#define BMC_REBOOT_BY_CMD(base, offset)        *((uint32_t *)(base + (offset + 0x4)))
#define BMC_REBOOT_SIG(base, offset)           *((uint32_t *)(base + (offset + 0x8)))
#define BOOT_MAGIC                    0xFB420054
#define BIT_RECORD_LOG                (1 << 8)
// kernel panic
#define FLAG_KERN_PANIC               (1 << 0)
// reboot command
#define FLAG_REBOOT_CMD               (1 << 0)
#define FLAG_CFG_UTIL                 (1 << 1)
#define FLAG_UBIFS_ERROR              (1 << 2)

#define HB_SLEEP_TIME (5 * 60)
#define HB_TIMESTAMP_COUNT (60 * 60 / HB_SLEEP_TIME)
#define SLED_TS_TIMEOUT 100    //SLED Time Sync Timeout

#define KV_KEY_IMAGE_VERSIONS "image_versions"
#define KV_KEY_HEALTHD_REARM  "healthd_rearm"

#define BIC_HEALTH_INTERVAL 60 //seconds
#define BIC_RESET_ERR_CNT   3

#define LOG_REARM_CHECK_INTERVAL 3 // seconds

#define DEFAULT_UBIFS_HEALTH_INTERVAL 30 // seconds

#define MAX_LOG_SIZE 128

struct i2c_bus_s {
  uint32_t offset;
  char     *name;
  bool     enabled;
};
struct i2c_bus_s ast_i2c_dev_offset[I2C_BUS_NUM] = {
  {0x040,  "I2C DEV1 OFFSET", false},
  {0x080,  "I2C DEV2 OFFSET", false},
  {0x0C0,  "I2C DEV3 OFFSET", false},
  {0x100,  "I2C DEV4 OFFSET", false},
  {0x140,  "I2C DEV5 OFFSET", false},
  {0x180,  "I2C DEV6 OFFSET", false},
  {0x1C0,  "I2C DEV7 OFFSET", false},
  {0x300,  "I2C DEV8 OFFSET", false},
  {0x340,  "I2C DEV9 OFFSET", false},
  {0x380,  "I2C DEV10 OFFSET", false},
  {0x3C0,  "I2C DEV11 OFFSET", false},
  {0x400,  "I2C DEV12 OFFSET", false},
  {0x440,  "I2C DEV13 OFFSET", false},
  {0x480,  "I2C DEV14 OFFSET", false},
};

struct threshold_s {
  float value;
  float hysteresis;
  bool asserted;
  bool log;
  int log_level;
  bool reboot;
  bool bmc_error_trigger;
  bool bmc_mem_clear;
};

enum {
  BUS_LOCK_RECOVER_ERROR = 0,
  BUS_LOCK_RECOVER_TIMEOUT,
  BUS_LOCK_RECOVER_SUCCESS,
  BUS_LOCK_PRESERVE,
  SLAVE_DEAD_RECOVER_ERROR,
  SLAVE_DEAD_RECOVER_TIMEOUT,
  SLAVE_DEAD_RECOVER_SUCCESS,
  SLAVE_DEAD_PRESERVE,
  UNDEFINED_CASE,
};

enum ASSERT_BIT {
  BIT_CPU_OVER_THRESHOLD = 0,
  BIT_MEM_OVER_THRESHOLD = 1,
  BIT_RECOVERABLE_ECC    = 2,
  BIT_UNRECOVERABLE_ECC  = 3,
};

enum BIC_ERROR {
  BIC_HB_ERR = 0,
  BIC_IPMB_PLDM_ERR,
  BIC_READY_ERR,
  BIC_ERR_TYPE_CNT,
};

/* Heartbeat configuration */
static unsigned int hb_interval = 500;

/* CPU configuration */
static char *cpu_monitor_name = "BMC CPU utilization";
static bool cpu_monitor_enabled = false;
static unsigned int cpu_window_size = DEFAULT_WINDOW_SIZE;
static unsigned int cpu_monitor_interval = DEFAULT_MONITOR_INTERVAL;
static struct threshold_s *cpu_threshold;
static size_t cpu_threshold_num = 0;

/* Memory monitor enabled */
static char *mem_monitor_name = "BMC Memory utilization";
static bool mem_monitor_enabled = false;
static bool mem_enable_panic = false;
static int mem_min_free_kbytes = 0;
static unsigned int mem_window_size = DEFAULT_WINDOW_SIZE;
static unsigned int mem_monitor_interval = DEFAULT_MONITOR_INTERVAL;
static struct threshold_s *mem_threshold;
static size_t mem_threshold_num = 0;

static pthread_mutex_t global_error_mutex = PTHREAD_MUTEX_INITIALIZER;
static int bmc_health = 0; // CPU/MEM/ECC error flag

/* I2C Monitor enabled */
static bool i2c_monitor_enabled = false;

/* ECC configuration */
static char *recoverable_ecc_name = "ECC Recoverable Error";
static char *unrecoverable_ecc_name = "ECC Unrecoverable Error";
static bool ecc_monitor_enabled = false;
static unsigned int ecc_monitor_interval = DEFAULT_MONITOR_INTERVAL;
// to show the address of ecc error or not. supported chip: AST2500 serials
static bool ecc_addr_log = false;
static struct threshold_s *recov_ecc_threshold;
static size_t recov_ecc_threshold_num = 0;
static struct threshold_s *unrec_ecc_threshold;
static size_t unrec_ecc_threshold_num = 0;
static unsigned int ecc_recov_max_counter = MAX_ECC_RECOVERABLE_ERROR_COUNTER;
static unsigned int ecc_unrec_max_counter = MAX_ECC_UNRECOVERABLE_ERROR_COUNTER;

/* EDAC configuration */
static int ecc_source = ECC_SOURCE_MCR;  // default: direct MCR polling
static char edac_sysfs_path[PATH_MAX] = EDAC_SYSFS_PATH_DEFAULT;

/* BMC Health Monitor */
static bool regen_log_enabled = false;
static unsigned int bmc_health_monitor_interval = DEFAULT_MONITOR_INTERVAL;
static int regen_interval = 1200;

/* Node Manager Monitor enabled */
static bool nm_monitor_enabled = false;
static int nm_monitor_interval = DEFAULT_MONITOR_INTERVAL;
static unsigned char nm_retry_threshold = 0;
static bool nm_transmission_via_bic = false;
static uint8_t is_duplicated_unaccess_event[MAX_NUM_FRUS] = {false};
static uint8_t is_duplicated_abnormal_event[MAX_NUM_FRUS] = {false};

/* Verified-boot state check */
static bool vboot_state_check = false;

/* BMC time stamp enabled */
static bool bmc_timestamp_enabled = false;

/* BIC health monitor */
static bool bic_health_enabled = false;
static size_t bic_num_fru = 0;
static uint8_t bic_fru[MAX_NUM_FRUS] = {0};
static int bic_monitor_interval = BIC_HEALTH_INTERVAL;

/* healthd log rearm monitor */
static bool log_rearm_enabled = false;

/*do fw update pre and post action */
static bool do_fw_update_pre_post_action_enabled = false;

/* UBIFS Health Monitor */
struct ubifs_health_monitor_config
{
  bool enabled;
  int monitor_interval;
};
static struct ubifs_health_monitor_config uhm_config = {
  .enabled          = false,
  .monitor_interval = DEFAULT_UBIFS_HEALTH_INTERVAL,
};

int __attribute__((weak))
pre_fw_update_action() {
  return PAL_EOK;
}

int __attribute__((weak))
post_fw_update_action() {
  return PAL_EOK;
}

static void
initialize_do_fw_update_pre_post_action(json_t *obj) {
  json_t *tmp = NULL;

  if (obj == NULL) {
    return;
  }

  tmp = json_object_get(obj, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }

  do_fw_update_pre_post_action_enabled = json_is_true(tmp);
}

static void
initialize_threshold(const char *target, json_t *thres, struct threshold_s *t) {
  json_t *tmp;
  size_t i;
  size_t act_size;

  tmp = json_object_get(thres, "value");
  if (!tmp || !json_is_real(tmp)) {
    return;
  }
  t->value = json_real_value(tmp);

  /* Do not let the value (CPU/MEM thresholds) exceed these safe ranges */
  if ((strcasestr(target, "CPU") != 0ULL) ||
      (strcasestr(target, "MEM") != 0ULL)) {
    if (t->value > MAX_THRESHOLD) {
      syslog(LOG_WARNING,
             "%s: user setting %s threshold %f is too high and set threshold as %f",
             __func__, target, t->value, MAX_THRESHOLD);
      t->value = MAX_THRESHOLD;
    }
    if (t->value < MIN_THRESHOLD) {
      syslog(LOG_WARNING,
             "%s: user setting %s threshold %f is too low and set threshold as %f",
             __func__, target, t->value, MIN_THRESHOLD);
      t->value = MIN_THRESHOLD;
    }
  }

  tmp = json_object_get(thres, "hysteresis");
  if (tmp && json_is_real(tmp)) {
    t->hysteresis = json_real_value(tmp);
  }
  tmp = json_object_get(thres, "action");
  if (!tmp || !json_is_array(tmp)) {
    return;
  }
  act_size = json_array_size(tmp);
  for(i = 0; i < act_size; i++) {
    const char *act;
    json_t *act_o = json_array_get(tmp, i);
    if (!act_o || !json_is_string(act_o)) {
      continue;
    }
    act = json_string_value(act_o);
    if (!strcmp(act, "log-warning")) {
      t->log_level = LOG_WARNING;
      t->log = true;
    } else if(!strcmp(act, "log-critical")) {
      t->log_level = LOG_CRIT;
      t->log = true;
    } else if (!strcmp(act, "reboot")) {
      t->reboot = true;
    } else if(!strcmp(act, "bmc-error-trigger")) {
      t->bmc_error_trigger = true;
    } else if(!strcmp(act, "bmc-mem-clear")) {
      t->bmc_mem_clear = true;
    }
  }
}

static void initialize_thresholds(const char *target, json_t *array, struct threshold_s **out_arr, size_t *out_len) {
  size_t size = json_array_size(array);
  size_t i;
  struct threshold_s *thres;

  if (size == 0) {
    return;
  }
  thres = *out_arr = calloc(size, sizeof(struct threshold_s));
  if (!thres) {
    return;
  }
  *out_len = size;

  for(i = 0; i < size; i++) {
    json_t *e = json_array_get(array, i);
    if (!e) {
      continue;
    }
    initialize_threshold(target, e, &thres[i]);
  }
}

static void
initialize_hb_config(json_t *conf) {
  json_t *tmp;

  tmp = json_object_get(conf, "interval");
  if (!tmp || !json_is_number(tmp)) {
    return;
  }
  hb_interval = json_integer_value(tmp);
}

static void
initialize_cpu_config(json_t *conf) {
  json_t *tmp;

  tmp = json_object_get(conf, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  cpu_monitor_enabled = json_is_true(tmp);
  if (!cpu_monitor_enabled) {
    return;
  }
  tmp = json_object_get(conf, "window_size");
  if (tmp && json_is_number(tmp)) {
    cpu_window_size = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    cpu_monitor_interval = json_integer_value(tmp);
    if (cpu_monitor_interval <= 0)
      cpu_monitor_interval = DEFAULT_MONITOR_INTERVAL;
  }
  tmp = json_object_get(conf, "threshold");
  if (!tmp || !json_is_array(tmp)) {
    cpu_monitor_enabled = false;
    return;
  }
  initialize_thresholds(cpu_monitor_name, tmp, &cpu_threshold, &cpu_threshold_num);
}

static void
initialize_mem_config(json_t *conf) {
  json_t *tmp;

  tmp = json_object_get(conf, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  mem_monitor_enabled = json_is_true(tmp);
  if (!mem_monitor_enabled) {
    return;
  }
  tmp = json_object_get(conf, "enable_panic_on_oom");
  if (tmp && json_is_true(tmp)) {
    mem_enable_panic = true;
  }
  tmp = json_object_get(conf, "min_free_kbytes");
  if (tmp && json_is_number(tmp)) {
    mem_min_free_kbytes = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "window_size");
  if (tmp && json_is_number(tmp)) {
    mem_window_size = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    mem_monitor_interval = json_integer_value(tmp);
    if (mem_monitor_interval <= 0)
      mem_monitor_interval = DEFAULT_MONITOR_INTERVAL;
  }
  tmp = json_object_get(conf, "threshold");
  if (!tmp || !json_is_array(tmp)) {
    mem_monitor_enabled = false;
    return;
  }
  initialize_thresholds(mem_monitor_name, tmp, &mem_threshold, &mem_threshold_num);
}

static void
initialize_i2c_config(json_t *conf) {
  json_t *tmp;
  size_t i;
  size_t i2c_num_busses;

  tmp = json_object_get(conf, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  i2c_monitor_enabled = json_is_true(tmp);
  if (!i2c_monitor_enabled) {
    return;
  }
  tmp = json_object_get(conf, "busses");
  if (!tmp || !json_is_array(tmp)) {
    goto error_bail;
  }
  i2c_num_busses = json_array_size(tmp);
  if (!i2c_num_busses) {
    /* Nothing to monitor */
    goto error_bail;
  }
  for(i = 0; i < i2c_num_busses; i++) {
    size_t bus;
    json_t *ind = json_array_get(tmp, i);
    if (!ind || !json_is_number(ind)) {
      goto error_bail;
    }
    bus = json_integer_value(ind);
    if (bus >= I2C_BUS_NUM) {
      syslog(LOG_CRIT, "HEALTHD: Warning: Ignoring unsupported I2C Bus:%u\n", (unsigned int)bus);
      continue;
    }
    ast_i2c_dev_offset[bus].enabled = true;
  }
  return;
error_bail:
  i2c_monitor_enabled = false;
}

static void
initialize_ecc_config(json_t *conf) {
  json_t *tmp;

  tmp = json_object_get(conf, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  ecc_monitor_enabled = json_is_true(tmp);
  if (!ecc_monitor_enabled) {
    return;
  }
  // Parse ecc_source: "edac" or "mcr" (default)
  tmp = json_object_get(conf, "ecc_source");
  if (tmp && json_is_string(tmp)) {
    const char *source = json_string_value(tmp);
    if (!strcmp(source, "edac")) {
      ecc_source = ECC_SOURCE_EDAC;
      syslog(LOG_INFO, "ECC monitoring source: EDAC sysfs");
    } else {
      ecc_source = ECC_SOURCE_MCR;
      syslog(LOG_INFO, "ECC monitoring source: MCR direct");
    }
  }
  // Parse EDAC sysfs path, default is /sys/devices/system/edac/mc/mc0
  tmp = json_object_get(conf, "edac_sysfs_path");
  if (tmp && json_is_string(tmp)) {
    const char *path = json_string_value(tmp);

    if (path && path[0] != '\0') {
      if (snprintf(edac_sysfs_path, sizeof(edac_sysfs_path), "%s", path) >=
          (int)sizeof(edac_sysfs_path)) {
        syslog(LOG_WARNING, "%s: edac_sysfs_path is too long, using default path", __func__);
        snprintf(edac_sysfs_path, sizeof(edac_sysfs_path), "%s", EDAC_SYSFS_PATH_DEFAULT);
      }
      syslog(LOG_INFO, "ECC EDAC sysfs path: %s", edac_sysfs_path);
    }
  }
  tmp = json_object_get(conf, "ecc_address_log");
  if (tmp || json_is_boolean(tmp)) {
    ecc_addr_log = json_is_true(tmp);
  }
  tmp = json_object_get(conf, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    ecc_monitor_interval = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "recov_max_counter");
  if (tmp && json_is_number(tmp)) {
    ecc_recov_max_counter = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "unrec_max_counter");
  if (tmp && json_is_number(tmp)) {
    ecc_unrec_max_counter = json_integer_value(tmp);
  }
  tmp = json_object_get(conf, "recov_threshold");
  if (tmp || json_is_array(tmp)) {
    initialize_thresholds(recoverable_ecc_name, tmp, &recov_ecc_threshold, &recov_ecc_threshold_num);
  }
  tmp = json_object_get(conf, "unrec_threshold");
  if (tmp || json_is_array(tmp)) {
    initialize_thresholds(unrecoverable_ecc_name, tmp, &unrec_ecc_threshold, &unrec_ecc_threshold_num);
  }
}

static void
initialize_bmc_health_config(json_t *conf) {
  json_t *tmp;

  tmp = json_object_get(conf, "enabled");
  if (tmp || json_is_boolean(tmp)) {
    regen_log_enabled = json_is_true(tmp);
  }
  tmp = json_object_get(conf, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    bmc_health_monitor_interval = json_integer_value(tmp);
    if (bmc_health_monitor_interval <= 0)
      bmc_health_monitor_interval = DEFAULT_MONITOR_INTERVAL;
  }
  tmp = json_object_get(conf, "regenerating_interval");
  if (tmp && json_is_number(tmp)) {
    regen_interval = json_integer_value(tmp);
  }
}

static void
initialize_nm_monitor_config(json_t *conf)
{
  json_t *tmp;

  tmp = json_object_get(conf, "enabled");
  if (!tmp || !json_is_boolean(tmp))
  {
    goto error_bail;
  }

  nm_monitor_enabled = json_is_true(tmp);
  if ( !nm_monitor_enabled )
  {
    goto error_bail;
  }

  tmp = json_object_get(conf, "monitor_interval");
  if (tmp && json_is_number(tmp))
  {
    nm_monitor_interval = json_integer_value(tmp);
    if (nm_monitor_interval <= 0)
    {
      nm_monitor_interval = DEFAULT_MONITOR_INTERVAL;
    }
  }

  tmp = json_object_get(conf, "retry_threshold");
  if (tmp && json_is_number(tmp))
  {
    nm_retry_threshold = json_integer_value(tmp);
  }

  tmp = json_object_get(conf, "nm_transmission_via_bic");
  if (tmp && json_is_boolean(tmp))
  {
    nm_transmission_via_bic = json_is_true(tmp);
  }

#ifdef DEBUG
  syslog(LOG_WARNING, "enabled:%d, monitor_interval:%d, retry_threshold:%d", nm_monitor_enabled, nm_monitor_interval, nm_retry_threshold);
#endif
  return;

error_bail:
  nm_monitor_enabled = false;
}

static void initialize_vboot_config(json_t *obj)
{
  json_t *tmp;

  if (!obj) {
    return;
  }
  tmp = json_object_get(obj, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  vboot_state_check = json_is_true(tmp);
}

static void initialize_bmc_timestamp_config(json_t *obj) {
  json_t *tmp;

  if (!obj) {
    return;
  }
  tmp = json_object_get(obj, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  bmc_timestamp_enabled = json_is_true(tmp);
}

static void initialize_bic_health_config(json_t *obj) {
  json_t *tmp = NULL;
  size_t i;

  if (obj == NULL) {
    return;
  }
  tmp = json_object_get(obj, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  bic_health_enabled = json_is_true(tmp);

  tmp = json_object_get(obj, "fru");
  if (!tmp || !json_is_array(tmp)) {
    goto error;
  }
  bic_num_fru = json_array_size(tmp);
  if (!bic_num_fru) {
    /* Nothing to monitor */
    goto error;
  }
  for (i = 0; i < bic_num_fru; i++) {
    json_t *ind = json_array_get(tmp, i);
    if (!ind || !json_is_number(ind)) {
      goto error;
    }
    bic_fru[i] = json_integer_value(ind);
  }

  tmp = json_object_get(obj, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    bic_monitor_interval = json_integer_value(tmp);
    if (bic_monitor_interval <= 0) {
      bic_monitor_interval = BIC_HEALTH_INTERVAL;
    }
  }
  return;
error:
  bic_health_enabled = false;
}

static void initialize_ubifs_health_config(json_t *obj) {
  json_t *tmp = NULL;

  if (obj == NULL) {
    return;
  }
  tmp = json_object_get(obj, "enabled");
  if (!tmp || !json_is_boolean(tmp)) {
    return;
  }
  uhm_config.enabled = json_is_true(tmp);

  tmp = json_object_get(obj, "monitor_interval");
  if (tmp && json_is_number(tmp)) {
    uhm_config.monitor_interval = json_integer_value(tmp);
    if (uhm_config.monitor_interval <= 0)
      uhm_config.monitor_interval = DEFAULT_UBIFS_HEALTH_INTERVAL;
  }
}

static int
initialize_configuration(void) {
  json_error_t error;
  json_t *conf;
  json_t *v;

  conf = json_load_file(CONFIG_PATH, 0, &error);
  if (!conf) {
    syslog(LOG_CRIT, "HEALTHD configuration load failed");
    return -1;
  }
  v = json_object_get(conf, "version");
  if (v && json_is_string(v)) {
    syslog(LOG_INFO, "Loaded configuration version: %s\n", json_string_value(v));
  }
  v = json_object_get(conf, "log_rearm");
  if (v && json_is_boolean(v)) {
    log_rearm_enabled = json_is_true(v);
  }
  initialize_hb_config(json_object_get(conf, "heartbeat"));
  initialize_cpu_config(json_object_get(conf, "bmc_cpu_utilization"));
  initialize_mem_config(json_object_get(conf, "bmc_mem_utilization"));
  initialize_i2c_config(json_object_get(conf, "i2c"));
  initialize_ecc_config(json_object_get(conf, "ecc_monitoring"));
  initialize_bmc_health_config(json_object_get(conf, "bmc_health"));
  initialize_nm_monitor_config(json_object_get(conf, "nm_monitor"));
  initialize_vboot_config(json_object_get(conf, "verified_boot"));
  initialize_bmc_timestamp_config(json_object_get(conf, "bmc_timestamp"));
  initialize_bic_health_config(json_object_get(conf, "bic_health"));
  initialize_ubifs_health_config(json_object_get(conf, "ubifs_health"));
  initialize_do_fw_update_pre_post_action(json_object_get(conf, "fw_update_pre_post_action"));

  json_decref(conf);

  return 0;
}

static void threshold_assert_check(const char *target, float value, struct threshold_s *thres) {

  struct sysinfo info;

  if (!thres->asserted && value >= thres->value) {
    thres->asserted = true;
    if (thres->log) {
      syslog(thres->log_level, "ASSERT: %s (%.2f%%) exceeds the threshold (%.2f%%).\n", target, value, thres->value);
    }
    if (thres->reboot) {
      sysinfo(&info);
      syslog(thres->log_level, "Rebooting BMC; latest uptime: %ld sec", info.uptime);

      sleep(1);
      pal_bmc_reboot(RB_AUTOBOOT);
    }
    if (thres->bmc_error_trigger) {
      pthread_mutex_lock(&global_error_mutex);
      if (!bmc_health) { // assert bmc_health key only when not yet set
        pal_set_key_value(BMC_HEALTH_FILE, NOT_HEALTH);
      }
      if (strcasestr(target, "CPU") != 0ULL) {
        bmc_health = SETBIT(bmc_health, BIT_CPU_OVER_THRESHOLD);
      } else if (strcasestr(target, "Mem") != 0ULL) {
        bmc_health = SETBIT(bmc_health, BIT_MEM_OVER_THRESHOLD);
      } else {
        pthread_mutex_unlock(&global_error_mutex);
        return;
      }
      pthread_mutex_unlock(&global_error_mutex);
      pal_bmc_err_enable(target);
    }
    if (thres->bmc_mem_clear) {
      if (system("sync;/sbin/sysctl vm.drop_caches=3 > /dev/null")){
        syslog(LOG_ERR, "Clear BMC Memory failed\n");
      }
    }
  }
}

static void threshold_deassert_check(const char *target, float value, struct threshold_s *thres) {
  if (thres->asserted && value < (thres->value - thres->hysteresis)) {
    thres->asserted = false;
    if (thres->log) {
      syslog(thres->log_level, "DEASSERT: %s (%.2f%%) is under the threshold (%.2f%%).\n", target, value, thres->value);
    }
    if (thres->bmc_error_trigger) {
      pthread_mutex_lock(&global_error_mutex);
      if (strcasestr(target, "CPU") != 0ULL) {
        bmc_health = CLEARBIT(bmc_health, BIT_CPU_OVER_THRESHOLD);
      } else if (strcasestr(target, "Mem") != 0ULL) {
        bmc_health = CLEARBIT(bmc_health, BIT_MEM_OVER_THRESHOLD);
      } else {
        pthread_mutex_unlock(&global_error_mutex);
        return;
      }
      if (!bmc_health) { // deassert bmc_health key if no any error bit assertion
        pal_set_key_value(BMC_HEALTH_FILE, HEALTH);
      }
      pthread_mutex_unlock(&global_error_mutex);
      pal_bmc_err_disable(target);
    }
  }
}

static void
threshold_check(const char *target, float value, struct threshold_s *thresholds, size_t num) {
  size_t i;

  for(i = 0; i < num; i++) {
    threshold_assert_check(target, value, &thresholds[i]);
    threshold_deassert_check(target, value, &thresholds[i]);
  }
}

static void ecc_threshold_assert_check(const char *target, int value,
                                       struct threshold_s *thres, uint32_t ecc_err_addr) {
  int thres_counter = 0;
  bool is_unrecoverable = false;
  bool is_recoverable = false;

  if (strcasestr(target, "Unrecover") != 0ULL) {
    is_unrecoverable = true;
    thres_counter = (ecc_unrec_max_counter * thres->value / 100);
  } else if (strcasestr(target, "Recover") != 0ULL) {
    is_recoverable = true;
    thres_counter = (ecc_recov_max_counter * thres->value / 100);
  } else {
    return;
  }

  if (!thres->asserted && value > thres_counter) {
    thres->asserted = true;

    if (thres->log) {
      if (ecc_addr_log) {
        if (is_unrecoverable) {
          syslog(thres->log_level, "%s occurred (over %d%%) "
              "Counter = %d Address of first unrecoverable ECC error = 0x%x",
              target, (int)thres->value, value,
              (ecc_err_addr >> 4) & 0xFFFFFFFF);
        } else if (is_recoverable) {
          syslog(thres->log_level, "%s occurred (over %d%%) "
              "Counter = %d Address of last recoverable ECC error = 0x%x",
              target, (int)thres->value, value,
              (ecc_err_addr >> 4) & 0xFFFFFFFF);
        }
      } else {
        syslog(thres->log_level, "ECC occurred (over %d%%): %s Counter = %d",
            (int)thres->value, target, value);
      }
    }

    if (thres->reboot) {
      pal_bmc_reboot(RB_AUTOBOOT);
    }

    if (thres->bmc_error_trigger) {
      pthread_mutex_lock(&global_error_mutex);

      if (!bmc_health) {
        pal_set_key_value(BMC_HEALTH_FILE, NOT_HEALTH);
      }

      if (is_unrecoverable) {
        bmc_health = SETBIT(bmc_health, BIT_UNRECOVERABLE_ECC);
      } else if (is_recoverable) {
        bmc_health = SETBIT(bmc_health, BIT_RECOVERABLE_ECC);
      } else {
        pthread_mutex_unlock(&global_error_mutex);
        return;
      }

      pthread_mutex_unlock(&global_error_mutex);
      pal_bmc_err_enable(target);
    }
  }
}
static void
ecc_threshold_check(const char *target, int value, struct threshold_s *thresholds,
                    size_t num, uint32_t ecc_err_addr) {
  size_t i;

  for(i = 0; i < num; i++) {
    ecc_threshold_assert_check(target, value, &thresholds[i], ecc_err_addr);
  }
}

static void
initilize_all_kv() {
  pal_set_def_key_value();
}

static void *
hb_handler(void *unused __attribute__((unused))) {
  // set flag to notice BMC healthd hb_handler is ready
  kv_set("flag_healthd_hb_led", "1", 0, 0);

  while(1) {
    /* Turn ON the HB Led*/
    pal_set_hb_led(1);
    msleep(hb_interval);

    /* Turn OFF the HB led */
    pal_set_hb_led(0);
    msleep(hb_interval);
  }
  return NULL;
}

static void *
watchdog_handler(void *unused __attribute__((unused))) {

  /* Start watchdog in manual mode */
  open_watchdog(0, 0);

  /* Set watchdog to persistent mode so timer expiry will happen independent
   * of this process's liveliness.
   */
  watchdog_disable_magic_close();

  // set flag to notice BMC healthd watchdog_handler is ready
  kv_set("flag_healthd_wtd", "1", 0, 0);

  while(1) {

    sleep(5);

    /*
     * Restart the watchdog countdown. If this process is terminated,
     * the persistent watchdog setting will cause the system to reboot after
     * the watchdog timeout.
     */
    kick_watchdog();
  }
  return NULL;
}

static void *
i2c_mon_handler(void *unused __attribute__((unused))) {
  char i2c_bus_device[16];
  int dev;
  int bus_status = 0;
  int asserted_flag[I2C_BUS_NUM] = { 0 };
  bool assert_handle = 0;
  int i;

  while (1) {
    for (i = 0; i < I2C_BUS_NUM; i++) {
      if (!ast_i2c_dev_offset[i].enabled) {
        continue;
      }
      sprintf(i2c_bus_device, "/dev/i2c-%d", i);
      dev = open(i2c_bus_device, O_RDWR);
      if (dev < 0) {
        syslog(LOG_DEBUG, "%s(): open() failed", __func__);
        continue;
      }
      bus_status = i2c_smbus_status(dev);
      close(dev);

      assert_handle = 0;
      if (bus_status == 0) {
        /* Bus status is normal */
        if (asserted_flag[i] != 0) {
          asserted_flag[i] = 0;
          syslog(LOG_CRIT, "DEASSERT: I2C(%d) Bus recoveried. (I2C bus index base 0)", i);
          pal_i2c_crash_deassert_handle(i);
        }
      } else {
        /* Check each case */
        if (GETBIT(bus_status, BUS_LOCK_RECOVER_ERROR)
            && !GETBIT(asserted_flag[i], BUS_LOCK_RECOVER_ERROR)) {
          asserted_flag[i] = SETBIT(asserted_flag[i], BUS_LOCK_RECOVER_ERROR);
          syslog(LOG_CRIT, "ASSERT: I2C(%d) bus is locked (Master Lock or Slave Clock Stretch). "
                           "Recovery error. (I2C bus index base 0)", i);
          assert_handle = 1;
        }
        bus_status = CLEARBIT(bus_status, BUS_LOCK_RECOVER_ERROR);
        if (GETBIT(bus_status, BUS_LOCK_RECOVER_TIMEOUT)
            && !GETBIT(asserted_flag[i], BUS_LOCK_RECOVER_TIMEOUT)) {
          asserted_flag[i] = SETBIT(asserted_flag[i], BUS_LOCK_RECOVER_TIMEOUT);
          syslog(LOG_CRIT, "ASSERT: I2C(%d) bus is locked (Master Lock or Slave Clock Stretch). "
                           "Recovery timed out. (I2C bus index base 0)", i);
          assert_handle = 1;
        }
        bus_status = CLEARBIT(bus_status, BUS_LOCK_RECOVER_TIMEOUT);
        if (GETBIT(bus_status, BUS_LOCK_RECOVER_SUCCESS)) {
          syslog(LOG_CRIT, "I2C(%d) bus had been locked (Master Lock or Slave Clock Stretch) "
                           "and has been recoveried successfully. (I2C bus index base 0)", i);
        }
        bus_status = CLEARBIT(bus_status, BUS_LOCK_RECOVER_SUCCESS);
        if (GETBIT(bus_status, SLAVE_DEAD_RECOVER_ERROR)
            && !GETBIT(asserted_flag[i], SLAVE_DEAD_RECOVER_ERROR)) {
          asserted_flag[i] = SETBIT(asserted_flag[i], SLAVE_DEAD_RECOVER_ERROR);
          syslog(LOG_CRIT, "ASSERT: I2C(%d) Slave is dead (SDA keeps low). "
                           "Bus recovery error. (I2C bus index base 0)", i);
          assert_handle = 1;
        }
        bus_status = CLEARBIT(bus_status, SLAVE_DEAD_RECOVER_ERROR);
        if (GETBIT(bus_status, SLAVE_DEAD_RECOVER_TIMEOUT)
            && !GETBIT(asserted_flag[i], SLAVE_DEAD_RECOVER_TIMEOUT)) {
          asserted_flag[i] = SETBIT(asserted_flag[i], SLAVE_DEAD_RECOVER_TIMEOUT);
          syslog(LOG_CRIT, "ASSERT: I2C(%d) Slave is dead (SDAs keep low). "
                           "Bus recovery timed out. (I2C bus index base 0)", i);
          assert_handle = 1;
        }
        bus_status = CLEARBIT(bus_status, SLAVE_DEAD_RECOVER_TIMEOUT);
        if (GETBIT(bus_status, SLAVE_DEAD_RECOVER_SUCCESS)) {
          syslog(LOG_CRIT, "I2C(%d) Slave was dead. and bus has been recoveried successfully. "
                           "(I2C bus index base 0)", i);
        }
        bus_status = CLEARBIT(bus_status, SLAVE_DEAD_RECOVER_SUCCESS);
        /* Check if any undefined bit remain in bus_status */
        if ((bus_status != 0) && !GETBIT(asserted_flag[i], UNDEFINED_CASE)) {
          asserted_flag[i] = SETBIT(asserted_flag[i], 8);
          syslog(LOG_CRIT, "ASSERT: I2C(%d) Undefined case. (I2C bus index base 0)", i);
          assert_handle = 1;
        }

        if (assert_handle) {
          pal_i2c_crash_assert_handle(i);
        }
      }
    }
    sleep(30);
  }
  return NULL;
}

static void *
CPU_usage_monitor(void *unused __attribute__((unused))) {
  unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
  unsigned long long total_diff, idle_diff, non_idle, idle_time = 0, total = 0, pre_total = 0, pre_idle = 0;
  char cpu[CPU_NAME_LENGTH] = {0};
  size_t i, timer = 0;
  int ready_flag = 0, retry = 0;
  float cpu_util_avg, cpu_util_total;
  float cpu_utilization[cpu_window_size];
  FILE *fp;
  int ret;

  sleep(180); //Wait 180s for BMC to idle stage.

  memset(cpu_utilization, 0, sizeof(float) * cpu_window_size);

  // set flag to notice BMC healthd CPU_usage_monitor is ready
  kv_set("flag_healthd_cpu", "1", 0, 0);

  while (1) {

    if (retry > HEALTHD_MAX_RETRY) {
      syslog(LOG_CRIT, "Cannot get CPU statistics. Stop %s\n", __func__);
      return NULL;
    }

    // Get CPU statistics. Time unit: jiffies
    fp = fopen(CPU_INFO_PATH, "r");
    if(!fp) {
      syslog(LOG_WARNING, "Failed to get CPU statistics.\n");
      retry++;
      continue;
    }

    ret = fscanf(fp, "%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    if (ret != 11) {
      syslog(LOG_WARNING, "Cannot parse CPU statistic. Stop %s\n", __func__);
      retry++;
      continue;
    }
    retry = 0;

    fclose(fp);

    timer %= cpu_window_size;

    // Need more data to cacluate the avg. utilization. We average 60 records here.
    if (timer == (cpu_window_size-1) && !ready_flag)
      ready_flag = 1;


    // guset and guest_nice are already accounted in user and nice so they are not included in total caculation
    idle_time = idle + iowait;
    non_idle = user + nice + system + irq + softirq + steal;
    total = idle_time + non_idle;

    // For runtime caculation, we need to take into account previous value.
    total_diff = total - pre_total;
    idle_diff = idle_time - pre_idle;

    // These records are used to caculate the avg. utilization.
    cpu_utilization[timer] = (float) (total_diff - idle_diff)/total_diff;

    // Start to average the cpu utilization
    if (ready_flag) {
      cpu_util_total = 0;
      for (i=0; i<cpu_window_size; i++) {
        cpu_util_total += cpu_utilization[i];
      }
      cpu_util_avg = (cpu_util_total/cpu_window_size) * 100.0;
      threshold_check(cpu_monitor_name, cpu_util_avg, cpu_threshold, cpu_threshold_num);
    }

    // Record current value for next caculation
    pre_total = total;
    pre_idle  = idle_time;

    timer++;
    sleep(cpu_monitor_interval);
  }
  return NULL;
}

static int set_panic_on_oom(void) {

  FILE *fp;
  int ret;
  int tmp_value;

  fp = fopen(VM_PANIC_ON_OOM_FILE, "r+");
  if (fp == NULL) {
    syslog(LOG_CRIT, "%s: failed to open file: %s", __func__, VM_PANIC_ON_OOM_FILE);
    return -1;
  }

  ret = fscanf(fp, "%d", &tmp_value);
  if (ret != 1) {
    syslog(LOG_CRIT, "%s: failed to read file: %s", __func__, VM_PANIC_ON_OOM_FILE);
    fclose(fp);
    return -1;
  }

  // if /proc/sys/vm/panic_on_oom is 0; set it to 1
  if (tmp_value == 0) {
    fseek(fp, 0, SEEK_SET);
    ret = fputs("1", fp);
    if (ret < 0) {
      syslog(LOG_CRIT, "%s: failed to write to file: %s", __func__, VM_PANIC_ON_OOM_FILE);
      fclose(fp);
      return -1;
    }
  }

  fclose(fp);
  return 0;
}

static void *
memory_usage_monitor(void *unused __attribute__((unused))) {
  struct sysinfo s_info;
  size_t i, timer = 0;
  int error, ready_flag = 0, retry = 0;
  float mem_util_avg, mem_util_total;
  float mem_utilization[mem_window_size];
  char cmd[128];

  memset(mem_utilization, 0, sizeof(float) * mem_window_size);

  if (mem_enable_panic) {
    set_panic_on_oom();
  }

  if (mem_min_free_kbytes > 0) {
    snprintf(cmd, sizeof(cmd), "/sbin/sysctl -w vm.min_free_kbytes=%d >/dev/null", mem_min_free_kbytes);
    if (system(cmd)) {
      syslog(LOG_ERR, "set min_free_kbytes failed");
    }
  }

  // set flag to notice BMC healthd memory_usage_monitor is ready
  kv_set("flag_healthd_mem", "1", 0, 0);

  while (1) {

    if (retry > HEALTHD_MAX_RETRY) {
      syslog(LOG_CRIT, "Cannot get sysinfo. Stop the %s\n", __func__);
      return NULL;
    }

    timer %= mem_window_size;

    // Need more data to cacluate the avg. utilization. We average 60 records here.
    if (timer == (mem_window_size-1) && !ready_flag)
      ready_flag = 1;

    // Get sys info
    error = sysinfo(&s_info);
    if (error) {
      syslog(LOG_WARNING, "%s Failed to get sys info. Error: %d\n", __func__, error);
      retry++;
      continue;
    }
    retry = 0;

    // These records are used to caculate the avg. utilization.
    mem_utilization[timer] = (float) (s_info.totalram - s_info.freeram)/s_info.totalram;

    // Start to average the memory utilization
    if (ready_flag) {
      mem_util_total = 0;
      for (i=0; i<mem_window_size; i++)
        mem_util_total += mem_utilization[i];

      mem_util_avg = (mem_util_total/mem_window_size) * 100.0;

      threshold_check(mem_monitor_name, mem_util_avg, mem_threshold, mem_threshold_num);
    }

    timer++;
    sleep(mem_monitor_interval);
  }
  return NULL;
}

static int write_edac_reset_counter(void) {
  char path[PATH_MAX];
  FILE *fp;
  int ret;

  ret = snprintf(path, sizeof(path), "%s/%s", edac_sysfs_path, EDAC_RESET_COUNTERS_FILE);

  if (ret < 0 || ret >= (int)sizeof(path)) {
    return -1;
  }

  fp = fopen(path, "w");
  if (!fp) {
    return -1;
  }

  if (fputs("1", fp) < 0) {
    fclose(fp);
    return -1;
  }

  fclose(fp);
  return 0;
}

static int read_edac_counter(const char *counter_file, uint32_t *value) {
  char path[PATH_MAX];
  char buf[32] = {0};
  FILE *fp;
  int ret;

  if (counter_file == NULL || value == NULL) {
    return -1;
  }

  ret = snprintf(path, sizeof(path), "%s/%s", edac_sysfs_path, counter_file);
  if (ret < 0 || ret >= (int)sizeof(path)) {
    return -1;
  }

  fp = fopen(path, "r");
  if (!fp) {
    return -1;
  }

  if (fgets(buf, sizeof(buf), fp) == NULL) {
    fclose(fp);
    return -1;
  }

  fclose(fp);
  *value = (uint32_t)strtoul(buf, NULL, 10);

  return 0;
}

// Thread to monitor the ECC counter
static void *
ecc_mon_handler(void *unused __attribute__((unused))) {
  int mcr_fd = 0;
  uint32_t ecc_status = 0;
  uint32_t unrecover_ecc_err_addr = 0;
  uint32_t recover_ecc_err_addr = 0;
  uint32_t ecc_recoverable_error_counter = 0;
  uint32_t ecc_unrecoverable_error_counter = 0;
  void *mcr_base_addr;
  void *mcr50_addr;
  void *mcr58_addr;
  void *mcr5c_addr;
  int retry_err = 0;

  // EDAC mode: reset EDAC counters
  if (ecc_source == ECC_SOURCE_EDAC) {
    if (write_edac_reset_counter() != 0) {
      syslog(LOG_WARNING, "%s: cannot reset EDAC counters", __func__);
    } else {
      syslog(LOG_INFO, "%s: EDAC ECC counters reset", __func__);
    }

    syslog(LOG_INFO, "%s: EDAC ECC monitoring started", __func__);
  }

  // set flag to notice BMC healthd ecc_mon_handler is ready
  kv_set("flag_healthd_ecc", "1", 0, 0);

  while (1) {
    mcr_fd = open("/dev/mem", O_RDWR | O_SYNC );
    if (mcr_fd < 0) {
      // In case of error opening the file, sleep for 2 sec and retry.
      // During continuous failures, log the error every 20 minutes.
      sleep(2);
      if (++retry_err >= 600) {
        syslog(LOG_ERR, "%s - cannot open /dev/mem", __func__);
        retry_err = 0;
      }
      continue;
    }

    retry_err = 0;

    mcr_base_addr = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mcr_fd, AST_MCR_BASE);
    if (mcr_base_addr == MAP_FAILED) {
      syslog(LOG_ERR, "%s - cannot mmap MCR", __func__);
      close(mcr_fd);
      sleep(ecc_monitor_interval);
      continue;
    }

    // Read counter: EDAC from sysfs, MCR from register
    if (ecc_source == ECC_SOURCE_EDAC) {
      uint32_t ce_count = 0, ue_count = 0;

      if (read_edac_counter(EDAC_CE_COUNT_FILE, &ce_count) != 0 ||
          read_edac_counter(EDAC_UE_COUNT_FILE, &ue_count) != 0) {
        syslog(LOG_WARNING, "%s: cannot read EDAC ECC counters", __func__);
        munmap(mcr_base_addr, PAGE_SIZE);
        close(mcr_fd);
        sleep(ecc_monitor_interval);
        continue;
      }

      ecc_recoverable_error_counter = ce_count;
      ecc_unrecoverable_error_counter = ue_count;
    } else {
      mcr50_addr = (char*)mcr_base_addr + INTR_CTRL_STS_OFFSET;
      ecc_status = *(volatile uint32_t*) mcr50_addr;
      ecc_recoverable_error_counter = (ecc_status >> 16) & 0xFF;
      ecc_unrecoverable_error_counter = (ecc_status >> 12) & 0xF;
    }

    if (ecc_recoverable_error_counter > ecc_recov_max_counter) {
      ecc_recoverable_error_counter = ecc_recov_max_counter;
    }
    if (ecc_unrecoverable_error_counter > ecc_unrec_max_counter) {
      ecc_unrecoverable_error_counter = ecc_unrec_max_counter;
    }

    if (ecc_addr_log) {
      mcr58_addr = (char*)mcr_base_addr + ADDR_FIRST_UNRECOVER_ECC_OFFSET;
      unrecover_ecc_err_addr = *(volatile uint32_t*) mcr58_addr;
      mcr5c_addr = (char*)mcr_base_addr + ADDR_LAST_RECOVER_ECC_OFFSET;
      recover_ecc_err_addr = *(volatile uint32_t*) mcr5c_addr;
    }
    munmap(mcr_base_addr, PAGE_SIZE);
    close(mcr_fd);

    // Check ECC recoverable error counter
    ecc_threshold_check(recoverable_ecc_name, ecc_recoverable_error_counter,
                        recov_ecc_threshold, recov_ecc_threshold_num, recover_ecc_err_addr);

    // Check ECC un-recoverable error counter
    ecc_threshold_check(unrecoverable_ecc_name, ecc_unrecoverable_error_counter,
                        unrec_ecc_threshold, unrec_ecc_threshold_num, unrecover_ecc_err_addr);

    sleep(ecc_monitor_interval);
  }
  return NULL;
}

static void *
bmc_health_monitor(void *unused __attribute__((unused)))
{
  int bmc_health_last_state = 1;
  int bmc_health_kv_state = 1;
  char tmp_health[MAX_VALUE_LEN];
  int relog_counter = 0;
  int relog_counter_criteria = regen_interval / bmc_health_monitor_interval;
  size_t i;
  int ret = 0;

  while(1) {
    // get current health status from kv_store
    memset(tmp_health, 0, MAX_VALUE_LEN);
    ret = pal_get_key_value(BMC_HEALTH_FILE, tmp_health);
    if (ret){
      syslog(LOG_ERR, " %s - kv get bmc_health status failed", __func__);
    }
    bmc_health_kv_state = atoi(tmp_health);

    // If log-util clear all fru, cleaning CPU/MEM/ECC error status
    // After doing it, daemon will regenerate asserted log
    // Generage a syslog every regen_interval loop counter
    if ((relog_counter >= relog_counter_criteria) ||
        ((bmc_health_last_state == 0) && (bmc_health_kv_state == 1))) {

      for(i = 0; i < cpu_threshold_num; i++)
        cpu_threshold[i].asserted = false;
      for(i = 0; i < mem_threshold_num; i++)
        mem_threshold[i].asserted = false;
      for(i = 0; i < recov_ecc_threshold_num; i++)
        recov_ecc_threshold[i].asserted = false;
      for(i = 0; i < unrec_ecc_threshold_num; i++)
        unrec_ecc_threshold[i].asserted = false;

      if (ecc_source == ECC_SOURCE_EDAC) {
        if (write_edac_reset_counter() != 0) {
          syslog(LOG_WARNING, "Failed to reset EDAC ECC counters");
        } else {
          syslog(LOG_INFO, "EDAC ECC counters reset");
        }
      }

      pthread_mutex_lock(&global_error_mutex);
      bmc_health = 0;
      pthread_mutex_unlock(&global_error_mutex);
      relog_counter = 0;
    }
    bmc_health_last_state = bmc_health_kv_state;
    relog_counter++;
    sleep(bmc_health_monitor_interval);
  }
  return NULL;
}

void check_nm_selftest_result(uint8_t fru, int result, uint8_t *selftest_result)
{
  static uint8_t no_response_retry[MAX_NUM_FRUS] = {0};
  static uint8_t abnormal_status_retry[MAX_NUM_FRUS] = {0};
  int fru_index = fru - 1;//fru id is start from 1.

  enum
  {
    NM_NORMAL_STATUS = 0,
  };

#ifdef DEBUG
  syslog(LOG_WARNING, "fruid:%d, result:%d, nm_retry_threshold:%d", fru, result, nm_retry_threshold);
#endif

  if ( PAL_ENOTSUP == result )
  {
    if ( no_response_retry[fru_index] >= nm_retry_threshold )
    {
      if ( !is_duplicated_unaccess_event[fru_index] )
      {
        is_duplicated_unaccess_event[fru_index] = true;
        syslog(LOG_CRIT, "FRU: %d ASSERT: ME Status - Controller Unavailable", fru);
      }
    }
    else
    {
      no_response_retry[fru_index]++;
    }
  }
  else
  {
    if ( NM_NORMAL_STATUS != result )
    {
      if ( abnormal_status_retry[fru_index] >=  nm_retry_threshold )
      {
        if ( !is_duplicated_abnormal_event[fru_index] )
        {
          is_duplicated_abnormal_event[fru_index] = true;
          syslog(LOG_CRIT, "FRU: %d ASSERT: ME Status - Controller Access Degraded or Unavailable, result: %02Xh, %02Xh", fru, selftest_result[0], selftest_result[1]);
        }
      }
      else
      {
        abnormal_status_retry[fru_index]++;
      }
    }
    else
    {
      if ( is_duplicated_abnormal_event[fru_index] )
      {
        is_duplicated_abnormal_event[fru_index] = false;
        syslog(LOG_CRIT, "FRU: %d DEASSERT: ME Status - Controller Access Degraded or Unavailable", fru);
      }

      if ( is_duplicated_unaccess_event[fru_index] )
      {
        is_duplicated_unaccess_event[fru_index] = false;
        syslog(LOG_CRIT, "FRU: %d DEASSERT: ME Status - Controller Unavailable", fru);
      }

      no_response_retry[fru_index] = 0;
      abnormal_status_retry[fru_index] = 0;
    }
  }
}

void
nm_selftest(uint8_t fru) {
  int ret = 0;
  int result = 0;
  const uint8_t normal_status[SIZE_SELF_TEST_RESULT] = {0x55, 0x00}; // If the selftest result is 55 00, the status of the controller is okay
  uint8_t data[SIZE_SELF_TEST_RESULT] = {0x0};

  if (pal_is_slot_server(fru) == 1) {
    if (pal_is_fw_update_ongoing(fru) == true) {
      return;
    }

    ret = pal_get_nm_selftest_result(fru, data);
    if (PAL_EOK == ret) {
      //if nm has the response, check the status
      result = memcmp(data, normal_status, sizeof(normal_status));
    } else {
      //if nm has no response, suppose it is in the not support state
      result = PAL_ENOTSUP;
    }
    check_nm_selftest_result(fru, result, data);
  }
}


static void *
nm_monitor(void *unused __attribute__((unused)))
{
  int fru;

  while (1)
  {
    for ( fru = 1; fru <= MAX_NUM_FRUS; fru++)
    {
      nm_selftest(fru);
    }
    sleep(nm_monitor_interval);
  }

  return NULL;
}

void
crit_proc_ongoing_handle(bool is_crit_proc_updating)
{
  static bool last_is_crit_proc_updating = false;

  if (last_is_crit_proc_updating == is_crit_proc_updating) {
  /* Nothing changed from last time. Just return */
    return;
  }
  last_is_crit_proc_updating = is_crit_proc_updating;

  if ( true == is_crit_proc_updating ) { // forbid the execution permission
    if (do_fw_update_pre_post_action_enabled) {
      pre_fw_update_action();
    }
    if (system("chmod 666 /sbin/shutdown.sysvinit") != 0) {
      syslog(LOG_ERR, "Disabling shutdown failed\n");
    }
    if (system("chmod 666 /sbin/halt.sysvinit") != 0) {
      syslog(LOG_ERR, "Disabling halt failed\n");
    }
    if (system("chmod 666 /sbin/init") != 0) {
      syslog(LOG_ERR, "Disabling init failed\n");
    }
  }
  else {
    if (do_fw_update_pre_post_action_enabled) {
      post_fw_update_action();
    }
    if (system("chmod 4755 /sbin/shutdown.sysvinit") != 0) {
      syslog(LOG_ERR, "Enabling shutdown failed\n");
    }
    if (system("chmod 4755 /sbin/halt.sysvinit") != 0) {
      syslog(LOG_ERR, "Enabling halt failed\n");
    }
    if (system("chmod 4755 /sbin/init") != 0) {
      syslog(LOG_ERR, "Enabling init failed\n");
    }
  }
}

//Block reboot and shutdown commands in BMC during any FW updating
static void *
crit_proc_monitor(void *unused __attribute__((unused))) {

  bool is_fw_updating = false;
  bool is_crashdump_ongoing = false;
  bool is_cplddump_ongoing = false;

  // set flag to notice BMC healthd crit_proc_monitor is ready
  kv_set("flag_healthd_crit_proc", "1", 0, 0);

  while(1)
  {
    //if is_fw_updating == true, means BMC is Updating a Device FW
    is_fw_updating = pal_is_fw_update_ongoing_system();

    //if is_autodump_ongoing == true, modify the permission
    is_crashdump_ongoing = pal_is_crashdump_ongoing_system();

    //if is_cplddump_ongoing == true, modify the permission
    is_cplddump_ongoing = pal_is_cplddump_ongoing_system();

    if ( (true == is_fw_updating) || (true == is_crashdump_ongoing) || (true == is_cplddump_ongoing) )
    {
      crit_proc_ongoing_handle(true);
    }

    if ( (false == is_fw_updating) && (false == is_crashdump_ongoing) && (false == is_cplddump_ongoing) )
    {
      crit_proc_ongoing_handle(false);
    }

    sleep(1);
  }
  return NULL;
}

static int log_count(const char *str)
{
  char cmd[512];
  FILE *fp;
  int ret;
  snprintf(cmd, sizeof(cmd), "grep \"%s\" /mnt/data/logfile /mnt/data/logfile.0 2> /dev/null | wc -l", str);
  fp = popen(cmd, "r");
  if (!fp) {
    return 0;
  }
  if (fscanf(fp, "%d", &ret) != 1) {
    ret = 0;
  }
  pclose(fp);
  return ret;
}

/* parser curr_version from /etc/issue
  and return version string length on success
  otherwise return 0 as failure */
static size_t get_curr_version(char *buf, size_t buflen)
{
#define STR_VALUE(x)   #x
#define STRINGIFY(x)   STR_VALUE(x)

  FILE *fp = NULL;
  size_t ret = 0;
  char *vers = NULL;

  vers = malloc(MAX_VALUE_LEN);
  if (!vers) return 0;

  fp = fopen("/etc/issue", "r");
  if (fp) {
    if(fscanf(fp, "OpenBMC Release %" STRINGIFY(MAX_VALUE_LEN) "s\n", vers) == 1) {
      ret = strnlen(vers, MAX_VALUE_LEN);
      if ( ret < buflen) {
        snprintf(buf, buflen, "%s", vers);
      } else {
        ret = 0;
      }
    }
    fclose(fp);
  }
  if (vers) free(vers);
  return ret;
}

static char*
get_latest_n_versions(char* orig, size_t orig_len, size_t cnt)
{
  char* p = orig;
  if (p == 0)
    return p;
  p += orig_len;
  while (cnt && p > orig) {
    p--;
    if (*p == ',') cnt--;
  }
  if (*p == ',') {
    p++;
  }
  return p;
}

static void store_curr_version(void)
{
  static char old_versions[MAX_VALUE_LEN*2];
  static char curr_version[MAX_VALUE_LEN];
  char *new_versions;
  size_t old_versions_len;
  size_t new_versions_len;

  if (0 == get_curr_version(curr_version, sizeof(curr_version))) {
    syslog(LOG_ERR, "Getting version failed!\n");
    return;
  }

  if (kv_get(KV_KEY_IMAGE_VERSIONS, old_versions, &old_versions_len, KV_FPERSIST)) {
    if (kv_set(KV_KEY_IMAGE_VERSIONS, curr_version, 0, KV_FPERSIST)) {
      syslog(LOG_ERR, "Setting image version failed");
    }
    return;
  }

  /* kv-store won't gurantee null terminate when request to return value len
   * so terminate it here explicitly */
  old_versions[old_versions_len] = '\0';
  /* check whether current version is the same as last version. */
  if (strcmp( get_latest_n_versions(old_versions, old_versions_len, 1),
              curr_version) == 0) {
    /* version not changed */
    return;
  }

  /* otherwise append curr_version */
  new_versions = old_versions;
  strcat(new_versions, ",");
  strncat(new_versions, curr_version, MAX_VALUE_LEN);
  new_versions_len = strnlen(new_versions, sizeof(old_versions));

  /* chop off oldest versions until it fits MAX_VALUE_LEN */
  while (new_versions_len >= MAX_VALUE_LEN) {
    while (*new_versions && *new_versions != ',') {
      new_versions++;
      new_versions_len--;
    }
    if (*new_versions == ',') {
      new_versions++;
      new_versions_len--;
    }
  }

  /* chop off oldest version and only keep at most VERSION_HISTORY_COUNT
   * this is to backwards comptable with healthd which will crash
   * when parser file contain history contain version more than VERSION_HISTORY_COUNT
   */
  new_versions = get_latest_n_versions(new_versions, new_versions_len,
    VERSION_HISTORY_COUNT);
  /* save the new versions string */
  if (kv_set(KV_KEY_IMAGE_VERSIONS, new_versions, 0, KV_FPERSIST)) {
    syslog(LOG_ERR, "Setting image version failed");
  }
}

/* Called when we have booted into the golden image
 * because of verified boot failure */
static void check_vboot_recovery(uint8_t error_type, uint8_t error_code)
{
  char log[512];
  char curr_err[MAX_VALUE_LEN] = {0};
  int assert_count, deassert_count;

  /* Technically we can get this from the kv store vboot_error. But
   * we cannot trust it since it could be compromised. Hence try to
   * infer this by counting ASSERT and DEASSERT logs from the persistent
   * log file */
  snprintf(log, sizeof(log), " ASSERT: Verified boot failure (%d,%d)", error_type, error_code);
  assert_count = log_count(log);
  snprintf(log, sizeof(log), " DEASSERT: Verified boot failure (%d,%d)", error_type, error_code);
  deassert_count = log_count(log);
  if (assert_count <= deassert_count) {
    /* This is the first time we are seeing this error. Log it */
    syslog(LOG_CRIT, "ASSERT: Verified boot failure (%d,%d)", error_type, error_code);
    syslog(LOG_CRIT, "Verified boot failure reason: %s", vboot_error(error_code));
  }
  /* Set the error so main can deassert with the correct error code */
  snprintf(curr_err, sizeof(curr_err), "(%d,%d)", error_type, error_code);
  kv_set("vboot_error", curr_err, 0, KV_FPERSIST);
}

/* Called when we have booted into CS1. Note verified boot
 * could have still failed if we are not in SW enforcement */
static void check_vboot_main(uint8_t error_type, uint8_t error_code)
{
  char last_err[MAX_VALUE_LEN] = {0};

  if (error_type != 0 || error_code != 0) {
    /* The only way we will boot into BMC (CS1) while there
     * is an active vboot error is if we are not enforcing.
     * Act as if we are in recovery */
    check_vboot_recovery(error_type, error_code);
  } else {
    /* We have successfully booted into a verified BMC! */
    if (0 != kv_get("vboot_error", last_err, NULL, KV_FPERSIST)) {
      /* We do not have info of the previous error. Not much we can do
       * log an info message and carry on */
      syslog(LOG_INFO, "Verified boot successful!");

      /* Create the key */
      kv_set("vboot_error", "(0,0)", 0, KV_FPERSIST);
    } else if (strcmp(last_err, "(0,0)")) {
      /* We just recovered from a previous error! */
      syslog(LOG_CRIT, "DEASSERT: Verified boot failure %s", last_err);
      /* Do not deassert again on reboot */
      kv_set("vboot_error", "(0,0)", 0, KV_FPERSIST);
    }
    store_curr_version();
  }
}

static void check_vboot_state(void)
{
  struct vbs *v = vboot_status();

  if (!v) {
    syslog(LOG_CRIT, "%s: Getting verified boot status failed", __func__);
    return;
  }
  if (v->recovery_boot) {
    check_vboot_recovery(v->error_type, v->error_code);
  } else {
    check_vboot_main(v->error_type, v->error_code);
  }
}

static void
log_reboot_cause(char *sled_off_time)
{
  int mem_fd;
  uint8_t *bmc_reboot_base;
  uint32_t reboot_detected_flag = 0;
  uint32_t kern_panic_flag = 0;
  uint32_t sram_bmc_reboot_base = 0x0;
  uint32_t sram_offset = 0x0;

  if (get_soc_model() == SOC_MODEL_ASPEED_G7) {
    /* No AST2700 mapping for the reboot-cause handoff is established. */
    syslog(LOG_CRIT, "BMC Reboot detected (cause handoff not yet defined for AST2700)");
    return;
  }

  if (get_soc_model() == SOC_MODEL_ASPEED_G6) {
    sram_bmc_reboot_base = AST_G6_SRAM_BMC_REBOOT_BASE;
    sram_offset = AST_G6_SRAM_BMC_REBOOT_OFFSET;
  } else {
    sram_bmc_reboot_base = AST_SRAM_BMC_REBOOT_BASE;
    sram_offset = AST_SRAM_BMC_REBOOT_OFFSET;
  }

  mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (mem_fd < 0) {
    syslog(LOG_CRIT, "devmem open failed");
    return;
  }
  bmc_reboot_base = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, sram_bmc_reboot_base);
  if (bmc_reboot_base == NULL) {
    syslog(LOG_CRIT, "Mapping SRAM_BMC_REBOOT_BASE failed");
    close(mem_fd);
    return;
  }

  // If this reset is due to Power-On-Reset, we detected SLED power OFF event
  if (pal_is_bmc_por()) {
    syslog(LOG_CRIT, "SLED Powered OFF at %s", sled_off_time);
    pal_add_cri_sel("BMC AC lost");

    // Initial BMC reboot flag
    BMC_REBOOT_BY_KERN_PANIC(bmc_reboot_base, sram_offset) = 0x0;
    BMC_REBOOT_BY_CMD(bmc_reboot_base, sram_offset) = 0x0;
    BMC_REBOOT_SIG(bmc_reboot_base, sram_offset) = BOOT_MAGIC;
  } else {
    kern_panic_flag = BMC_REBOOT_BY_KERN_PANIC(bmc_reboot_base, sram_offset);
    reboot_detected_flag = BMC_REBOOT_BY_CMD(bmc_reboot_base, sram_offset);

    // Check the SRAM to make sure the reboot cause
    // ===== kernel panic =====
    if ((kern_panic_flag & BIT_RECORD_LOG)) {
      // Clear the flag of record log and reserve the kernel panic flag
      kern_panic_flag &= ~(BIT_RECORD_LOG);

      if ((kern_panic_flag & FLAG_KERN_PANIC)) {
        syslog(LOG_CRIT, "BMC Reboot detected - caused by kernel panic");
      } else {
        syslog(LOG_CRIT, "BMC Reboot detected - unknown kernel flag (0x%08x)", kern_panic_flag);
      }

      BMC_REBOOT_BY_KERN_PANIC(bmc_reboot_base, sram_offset) = 0;
    // ===== reboot command =====
    } else if ((reboot_detected_flag & BIT_RECORD_LOG)) {
      // Clear the flag of record log and reserve the reboot command flag
      reboot_detected_flag &= ~(BIT_RECORD_LOG);

      if ((reboot_detected_flag & FLAG_CFG_UTIL)) {
        reboot_detected_flag &= ~(FLAG_CFG_UTIL);
        syslog(LOG_CRIT, "Reset BMC data to default factory settings");
      }

      if ((reboot_detected_flag & FLAG_UBIFS_ERROR)) {
        reboot_detected_flag &= ~(FLAG_UBIFS_ERROR);
        syslog(LOG_CRIT, "UBIFS Error detected during boot");
      }

      if ((reboot_detected_flag & FLAG_REBOOT_CMD)) {
        syslog(LOG_CRIT, "BMC Reboot detected - caused by reboot command");
      } else {
        syslog(LOG_CRIT, "BMC Reboot detected - unknown reboot command flag (0x%08x)", reboot_detected_flag);
      }

      BMC_REBOOT_BY_CMD(bmc_reboot_base, sram_offset) = 0;
    // ===== others =====
    } else {
      syslog(LOG_CRIT, "BMC Reboot detected");
    }
  }
  munmap(bmc_reboot_base, PAGE_SIZE);
  close(mem_fd);
}

// Thread to monitor SLED Cycles by using time stamp
static void *
timestamp_handler(void *unused __attribute__((unused)))
{
  int count = 0;
  struct timespec ts;
  struct timespec mts;
  char tstr[MAX_VALUE_LEN] = {0};
  char buf[128] = {0};
  uint8_t time_init = 0;
  time_t time_sled_on;
  time_t time_sled_off;

  // Read the last timestamp from KV storage
  pal_get_key_value("timestamp_sled", tstr);
  time_sled_off = (long) strtol(tstr, NULL, 10);
  ctime_r(&time_sled_off, buf);
  log_reboot_cause(buf);

  // set flag to notice BMC healthd timestamp_handler is ready
  kv_set("flag_healthd_bmc_timestamp", "1", 0, 0);

  while (1) {

    // Make sure the time is initialized properly
    // Since there is no battery backup, the time could be reset to build time
    // wait 100s at most, to prevent infinite waiting
    if ( time_init < SLED_TS_TIMEOUT ) {
      // Read current time
      clock_gettime(CLOCK_REALTIME, &ts);

      if ( (ts.tv_sec < time_sled_off) && (++time_init < SLED_TS_TIMEOUT) ) {
        sleep(1);
        continue;
      }

      // If get the correct time or time sync timeout
      time_init = SLED_TS_TIMEOUT;

      // Need to log SLED ON event, if this is Power-On-Reset
      if (pal_is_bmc_por()) {
        // Get uptime
        clock_gettime(CLOCK_MONOTONIC, &mts);
        // To find out when SLED was on, subtract the uptime from current time
        time_sled_on = ts.tv_sec - mts.tv_sec;

        ctime_r(&time_sled_on, buf);
        // Log an event if this is Power-On-Reset
        syslog(LOG_CRIT, "SLED Powered ON at %s", buf);
      }
      pal_update_ts_sled();
    }

    // Store timestamp every one hour to keep track of SLED power
    if (count++ == HB_TIMESTAMP_COUNT) {
      pal_update_ts_sled();
      count = 0;
    }

    sleep(HB_SLEEP_TIME);
  }

  return NULL;
}

static void *
bic_health_monitor(void* bic_fru_id) {
  int err_cnt = 0;
  int i = 0;
  uint8_t status = 0;
  uint8_t err_type[BIC_RESET_ERR_CNT] = {0};
  uint8_t type = 0;
  const char* err_str[BIC_ERR_TYPE_CNT] = {
    "heartbeat", "IPMB/PLDM", "BIC ready"
  };
  char err_log[MAX_LOG_SIZE] = "\0";
  char bic_health_key[MAX_KEY_LEN] = "\0";
  bool is_already_reset = false;
  bool is_log = false;

  uint8_t fru = *((uint8_t *) bic_fru_id);

  // set flag to notice BMC healthd bic_health_monitor is ready
  snprintf(bic_health_key, sizeof(bic_health_key), "flag_healthd_bic_fru%u_health", fru);
  kv_set(bic_health_key, "1", 0, 0);

  while (1) {
    if ((pal_get_server_12v_power(fru, &status) < 0) || (status == SERVER_12V_OFF)) {
      goto next_run;
    }

    // Check if bic is updating
    if (pal_is_fw_update_ongoing(fru) == true) {
      err_cnt = 0;
      sleep(bic_monitor_interval);
      continue;
    }

    // Read BIC ready pin to check BIC boots up completely
    if ((pal_is_bic_ready(fru, &status) == PAL_EOK) && (status == false)) {
      err_type[err_cnt++] = BIC_READY_ERR;
      goto next_run;
    }

    // Check whether BIC heartbeat works
    if (pal_is_bic_heartbeat_ok(fru) == false) {
      err_type[err_cnt++] = BIC_HB_ERR;
      goto next_run;
    }

    // Send a IPMB/PLDM command to check IPMB/PLDM service works normal
    if (pal_bic_self_test(fru) < 0) {
      err_type[err_cnt++] = BIC_IPMB_PLDM_ERR;
      goto next_run;
    }
    // if all check pass, clear error counter and reset flag
    err_cnt = 0;
    is_already_reset = false;

    // The ME commands are transmit via BIC on Grand Canyon, so check ME health when BIC health is good.
    if ((nm_monitor_enabled == true) && (nm_transmission_via_bic == true)) {
      nm_selftest(fru);
    }
next_run:
    if ((err_cnt >= BIC_RESET_ERR_CNT) && (is_already_reset == false) && (is_log == false)) {
      // if error counter over 3, reset BIC by hardware
      memset(err_log, 0, sizeof(err_log));
      strcat(err_log, "ERR Order: ");
      for (i = 0; i < BIC_RESET_ERR_CNT; i++) {
        type = err_type[i];
        strcat(err_log, err_str[type]);
        if (i != BIC_RESET_ERR_CNT - 1) { // last one
          strcat(err_log, ", ");
        }
      }
      // Support BIC HW RESET
      if (pal_bic_hw_reset() == PAL_EOK) {
        syslog(LOG_CRIT, "ASSERT: FRU: %u BIC_HEALTH, BIC HW RESET, %s", fru, err_log);
        err_cnt = 0;
        is_already_reset = true;
        // Not Support BIC HW RESET, Print SEL
      } else if (pal_bic_hw_reset() == PAL_ENOTSUP) {
        syslog(LOG_CRIT, "ASSERT: FRU: %u BIC_HEALTH, %s", fru, err_log);
        is_log = true;
      } else {
        syslog(LOG_CRIT, "ASSERT: FRU: %u BIC_HEALTH, BIC HW RESET Failed, %s", fru, err_log);
      }
    }
    sleep(bic_monitor_interval);
  }

  return NULL;
}

static void *
log_rearm_check(void *unused __attribute__((unused))) {
  int ret = 0;
  size_t i = 0;
  char val[MAX_KEY_LEN] = {0};

  while (1) {
    ret = kv_get(KV_KEY_HEALTHD_REARM, val, NULL, 0);
    if (ret < 0) {
      sleep(LOG_REARM_CHECK_INTERVAL);
      continue;
    }
    if (strcmp(val, "1") == 0) {
      if (nm_monitor_enabled == true) {
        memset(is_duplicated_unaccess_event, 0, sizeof(is_duplicated_unaccess_event));
        memset(is_duplicated_abnormal_event, 0, sizeof(is_duplicated_abnormal_event));
      }
      if (vboot_state_check && vboot_supported()) {
        check_vboot_state();
      }
      // Log re-arm for CPU usage monitoring
      if (cpu_monitor_enabled == true) {
        for (i = 0; i < cpu_threshold_num; i++) {
          if (cpu_threshold[i].asserted == true && cpu_threshold[i].log_level == LOG_CRIT) {
            cpu_threshold[i].asserted = false;
          }
        }
      }
      // Log re-arm for Memory usage monitoring
      if (mem_monitor_enabled == true) {
        for (i = 0; i < mem_threshold_num; i++) {
          if (mem_threshold[i].asserted == true && mem_threshold[i].log_level == LOG_CRIT) {
            mem_threshold[i].asserted = false;
          }
        }
      }

      kv_set(KV_KEY_HEALTHD_REARM, "0", 0, 0);
    }
    sleep(LOG_REARM_CHECK_INTERVAL);
  }

  return NULL;
}

static void *
ubifs_health_monitor(void *unused __attribute__((unused))) {
  const char ubifs_ro_error[] = "/sys/kernel/debug/ubifs/ubi0_0/ro_error";
  FILE *fp;
  int val;
  int mem_fd;
  uint8_t *bmc_reboot_base;
  uint32_t sram_bmc_reboot_base = 0x0;
  uint32_t sram_offset = 0x0;

  if (get_soc_model() == SOC_MODEL_ASPEED_G6) {
    sram_bmc_reboot_base = AST_G6_SRAM_BMC_REBOOT_BASE;
    sram_offset = AST_G6_SRAM_BMC_REBOOT_OFFSET;
  } else {
    sram_bmc_reboot_base = AST_SRAM_BMC_REBOOT_BASE;
    sram_offset = AST_SRAM_BMC_REBOOT_OFFSET;
  }

  while (1) {
    fp = fopen(ubifs_ro_error, "r");
    if (fp == NULL) {
      syslog(LOG_ERR, "%s: open %s failed", __func__, ubifs_ro_error);
    } else if (fscanf(fp, "%d", &val) != 1) {
      syslog(LOG_ERR, "%s: read %s failed", __func__, ubifs_ro_error);
    } else if (val != 0) {
      syslog(LOG_CRIT, "%s: ubifs (/dev/ubi0_0) in read-only mode (ro_error=%d)", __func__, val);

      if (get_soc_model() != SOC_MODEL_ASPEED_G7) {
        mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (mem_fd < 0) {
          syslog(LOG_ERR, "devmem open failed");
        } else {
          bmc_reboot_base = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, sram_bmc_reboot_base);
          if (bmc_reboot_base == NULL) {
            syslog(LOG_ERR, "Mapping SRAM_BMC_REBOOT_BASE failed");
          } else {
            BMC_REBOOT_BY_CMD(bmc_reboot_base, sram_offset) |= BIT_RECORD_LOG | FLAG_UBIFS_ERROR;
          }
          close(mem_fd);
        }
      } else {
        syslog(LOG_INFO, "%s: no AST2700 reboot-cause handoff is defined", __func__);
      }
      fclose(fp);
      break;
    }
    if (fp != NULL)
      fclose(fp);
    sleep(uhm_config.monitor_interval);
  }

  pal_bmc_reboot(RB_AUTOBOOT);
  return NULL;
}

void sig_handler(int signo __attribute__((unused))) {
  // Catch SIGALRM and SIGTERM. If recived signal record BMC log
  syslog(LOG_CRIT, "BMC health daemon stopped.");
  exit(0);
}

int main() {
  pthread_t tid_watchdog;
  pthread_t tid_hb_led;
  pthread_t tid_i2c_mon;
  pthread_t tid_cpu_monitor;
  pthread_t tid_mem_monitor;
  pthread_t tid_crit_proc_monitor;
  pthread_t tid_ecc_monitor;
  pthread_t tid_bmc_health_monitor;
  pthread_t tid_nm_monitor;
  pthread_t tid_timestamp_handler;
  pthread_t tid_bic_health_monitor[MAX_NUM_FRUS];
  pthread_t tid_log_rearm_check;
  pthread_t tid_ubifs_health_monitor;

  //Catch signals
  signal(SIGALRM, sig_handler);
  signal(SIGTERM, sig_handler);

  initilize_all_kv();

  initialize_configuration();

  if (vboot_state_check && vboot_supported()) {
    check_vboot_state();
    /* curr version is stored only when vboot is
     * successful */
  } else {
    store_curr_version();
  }

// For current platforms, we are using WDT from either fand or fscd
// TODO: keeping this code until we make healthd as central daemon that
//  monitors all the important daemons for the platforms.
  if (pthread_create(&tid_watchdog, NULL, watchdog_handler, NULL)) {
    syslog(LOG_WARNING, "pthread_create for watchdog error\n");
    exit(1);
  }

  if (pthread_create(&tid_hb_led, NULL, hb_handler, NULL)) {
    syslog(LOG_WARNING, "pthread_create for heartbeat error\n");
    exit(1);
  }

  if (cpu_monitor_enabled) {
    if (pthread_create(&tid_cpu_monitor, NULL, CPU_usage_monitor, NULL)) {
      syslog(LOG_WARNING, "pthread_create for monitor CPU usage\n");
      exit(1);
    }
  }

  if (mem_monitor_enabled) {
    if (pthread_create(&tid_mem_monitor, NULL, memory_usage_monitor, NULL)) {
      syslog(LOG_WARNING, "pthread_create for monitor memory usage\n");
      exit(1);
    }
  }

  if (i2c_monitor_enabled) {
    // Add a thread for monitoring all I2C buses crash or not
    if (pthread_create(&tid_i2c_mon, NULL, i2c_mon_handler, NULL)) {
      syslog(LOG_WARNING, "pthread_create for I2C errorr\n");
      exit(1);
    }
  }

  if (ecc_monitor_enabled) {
    if (pthread_create(&tid_ecc_monitor, NULL, ecc_mon_handler, NULL)) {
      syslog(LOG_WARNING, "pthread_create for ECC monitoring errorr\n");
      exit(1);
    }
  }

  if (regen_log_enabled) {
    if (pthread_create(&tid_bmc_health_monitor, NULL, bmc_health_monitor, NULL)) {
      syslog(LOG_WARNING, "pthread_create for BMC Health monitoring errorr\n");
      exit(1);
    }
  }

  if ((nm_monitor_enabled == true) && (nm_transmission_via_bic == false)) {
    if (pthread_create(&tid_nm_monitor, NULL, nm_monitor, NULL)) {
      syslog(LOG_WARNING, "pthread_create for nm monitor error\n");
      exit(1);
    }
  }

  if (pthread_create(&tid_crit_proc_monitor, NULL, crit_proc_monitor, NULL)) {
    syslog(LOG_WARNING, "pthread_create for FW Update Monitor error\n");
    exit(1);
  }

  if (bmc_timestamp_enabled) {
    if (pthread_create(&tid_timestamp_handler, NULL, timestamp_handler, NULL)) {
      syslog(LOG_WARNING, "pthread_create for time stamp handler error\n");
      exit(1);
    }
  }

  if (bic_health_enabled) {
    for (size_t i = 0; i < bic_num_fru; i ++) {
      if (pthread_create(&tid_bic_health_monitor[i], NULL, bic_health_monitor, &bic_fru[i])) {
        syslog(LOG_WARNING, "pthread_create for bic health monitor error\n");
        exit(1);
      }
    }
  }
  if (pthread_create(&tid_log_rearm_check, NULL, log_rearm_check, NULL)) {
    syslog(LOG_WARNING, "pthread_create for log re-arm monitor error\n");
    exit(1);
  }
  if (uhm_config.enabled) {
    if (pthread_create(&tid_ubifs_health_monitor, NULL, ubifs_health_monitor, NULL)) {
      syslog(LOG_WARNING, "pthread_create for ubifs health monitor error\n");
      exit(1);
    }
  }

  pthread_join(tid_watchdog, NULL);

  pthread_join(tid_hb_led, NULL);

  if (i2c_monitor_enabled) {
    pthread_join(tid_i2c_mon, NULL);
  }
  if (cpu_monitor_enabled) {
    pthread_join(tid_cpu_monitor, NULL);
  }

  if (mem_monitor_enabled) {
    pthread_join(tid_mem_monitor, NULL);
  }

  if (ecc_monitor_enabled) {
    pthread_join(tid_ecc_monitor, NULL);
  }

  if (regen_log_enabled) {
    pthread_join(tid_bmc_health_monitor, NULL);
  }

  if ((nm_monitor_enabled == true) && (nm_transmission_via_bic == false)) {
    pthread_join(tid_nm_monitor, NULL);
  }

  pthread_join(tid_crit_proc_monitor, NULL);

  if (bmc_timestamp_enabled) {
    pthread_join(tid_timestamp_handler, NULL);
  }

  if (bic_health_enabled) {
    for (size_t i = 0; i < bic_num_fru; i ++) {
      pthread_join(tid_bic_health_monitor[i], NULL);
    }
  }

  if (log_rearm_enabled){
    pthread_join(tid_log_rearm_check, NULL);
  }

  if (uhm_config.enabled) {
    pthread_join(tid_ubifs_health_monitor, NULL);
  }

  return 0;
}
