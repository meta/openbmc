/*
 *
 * Copyright 2014-present Facebook. All Rights Reserved.
 *
 * This file contains code to support IPMI2.0 Specificaton available @
 * http://www.intel.com/content/www/us/en/servers/ipmi/ipmi-specifications.html
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

#include "sdr.h"
#include "sel.h"
#include "fruid.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <openbmc/ipmi.h>
#include <openbmc/kv.h>
#include <openbmc/pal.h>
#include <openbmc/pal_sensors.h>
#include <sys/reboot.h>
#include <openbmc/obmc-i2c.h>
#include <openbmc/ipc.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zlib.h>
#include "sensor.h"

#define MAX_REQUESTS 64
#define SIZE_IANA_ID 3
#define SIZE_GUID 16

//declare for clearing BIOS flag
#define BIOS_Timeout 600
// Boot valid flag
#define BIOS_BOOT_VALID_FLAG (1U << 7)
#define CMOS_VALID_FLAG      (1U << 1)
#define FORCE_BOOT_BIOS_SETUP_VALID_FLAG (1U << 2)
//#define CHASSIS_GET_BOOT_OPTION_SUPPORT
//#define CHASSIS_SET_BOOT_OPTION_SUPPORT

#define OBMC_DUMP_STAT_KEY "obmc_dump_stat"

#define HTTPS_BOOT_CERT_PATH "/mnt/data/host/bios-rootcert"

// PPR definition
#define PPR_MAX_ROW_COUNT 100
#define PPR_MAX_HISTORY_COUNT 100
#define PPR_ROW_ADDR_DATA_LEN 8
#define PPR_HISTORY_DATA_LEN 17

static unsigned char IsTimerStart[MAX_NODES] = {0};

static unsigned char bmc_global_enable_setting[] = {0x0c,0x0c,0x0c,0x0c};

extern void plat_lan_init(lan_config_t *lan);

// TODO: Once data storage is finalized, the following structure needs
// to be retrieved/updated from persistent backend storage
static lan_config_t g_lan_config = { 0 };

// TODO: Need to store this info after identifying proper storage
static sys_info_param_t g_sys_info_params;

// enable IPMI cmd logging
static uint8_t gLogEnable = 0;

// IPMI Watchdog Timer Structure
struct watchdog_data {
  pthread_mutex_t mutex;
  pthread_t tid;
  uint8_t slot;
  uint8_t valid;
  uint8_t run;
  uint8_t no_log;
  uint8_t use;
  uint8_t pre_action;
  uint8_t action;
  uint8_t pre_interval;
  uint8_t expiration;
  uint16_t init_count_down;
  uint16_t present_count_down;
};

static struct watchdog_data *g_wdt[MAX_NUM_FRUS];

static char* wdt_use_name[8] = {
  "reserved",
  "BIOS FRB2",
  "BIOS/POST",
  "OS Load",
  "SMS/OS",
  "OEM",
  "reserved",
  "reserved",
};

static char* wdt_action_name[8] = {
  "Timer expired",
  "Hard Reset",
  "Power Down",
  "Power Cycle",
  "reserved",
  "reserved",
  "reserved",
  "reserved",
};

static char *cpu_info_key[] =
{
  "",
  "product_name",
  "basic_info",
  "type",
  "micro_code",
  "turbo_mode"
};

static char *dimm_info_key[] =
{
  "",
  "location",
  "type",
  "speed",
  "part_name",
  "serial_num",
  "manufacturer_id",
  "status",
  "present_bit"
};

static char *drive_info_key[] =
{
  "location",
  "serial_num",
  "model_name",
  "fw_version",
  "capacity",
  "quantity",
  "type",
  "wwn"
};

// obmc-dump status
enum {
  DUMP_DONE = 0x0,
  DUMP_FAIL = 0x1,
  DUMP_NEVER = 0x2,
  DUMP_ONGOING = 0x3,
};

// TODO: Based on performance testing results, might need fine grained locks
// Since the global data is specific to a NetFunction, adding locs at NetFn level
static pthread_mutex_t m_chassis;
static pthread_mutex_t m_sensor;
static pthread_mutex_t m_app;
static pthread_mutex_t m_storage;
static pthread_mutex_t m_transport;
static pthread_mutex_t m_group_extension;
static pthread_mutex_t m_oem;
static pthread_mutex_t m_oem_storage;
static pthread_mutex_t m_oem_1s;
static pthread_mutex_t m_oem_usb_dbg;
static pthread_mutex_t m_oem_q;
static pthread_mutex_t m_oem_zion;

extern int plat_udbg_get_frame_info();
extern int plat_udbg_get_updated_frames(uint8_t *count, uint8_t *buffer);
extern int plat_udbg_get_post_desc(uint8_t index, uint8_t *next, uint8_t phase,  uint8_t *end, uint8_t *length, uint8_t *buffer);
extern int plat_udbg_get_gpio_desc(uint8_t index, uint8_t *next, uint8_t *level, uint8_t *def,
                            uint8_t *count, uint8_t *buffer);
extern int plat_udbg_get_frame_data(uint8_t frame, uint8_t page, uint8_t *next, uint8_t *count, uint8_t *buffer);
extern int plat_udbg_control_panel(uint8_t panel, uint8_t operation, uint8_t item, uint8_t *count, uint8_t *buffer);

static void ipmi_handle(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len);
extern int bbv_power_cycle(int delay_time);

static struct watchdog_data *get_watchdog(int slot_id)
{
  if (slot_id > MAX_NUM_FRUS)
    return NULL;
  return g_wdt[slot_id - 1];
}

static int length_check(unsigned char cmd_len, unsigned char req_len, unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  // req_len = cmd_len + 3 (payload_id, cmd and netfn)
  if( req_len != (cmd_len + IPMI_MN_REQ_HDR_SIZE) ){
    res->cc = CC_INVALID_LENGTH;
    *res_len = 0;
    return 1;
  }
  return 0;
}

/*
 **Function to handle with clearing BIOS flag
 */
void
*clear_bios_data_timer(void *ptr)
{
  int timer = 0;
  int slot_id = (int)ptr;
  int oldstate;
  unsigned char boot[SIZE_BOOT_ORDER]={0};
  unsigned char res_len;
  pthread_detach(pthread_self());

  while( timer <= BIOS_Timeout )
  {
#ifdef DEBUG
    syslog(LOG_WARNING, "[%s][%lu] Timer: %d\n", __func__, pthread_self(), timer);
#endif
    sleep(1);

    timer++;
  }

  //get boot order setting
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
  pal_get_boot_order(slot_id, NULL, boot, &res_len);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);

#ifdef DEBUG
  syslog(LOG_WARNING, "[%s][%lu] Get: %x %x %x %x %x %x\n", __func__, pthread_self() ,boot[0], boot[1], boot[2], boot[3], boot[4], boot[5]);
#endif

  //clear boot-valid and cmos bits due to timeout:
  boot[0] &= ~(BIOS_BOOT_VALID_FLAG | CMOS_VALID_FLAG);

#ifdef DEBUG
  syslog(LOG_WARNING, "[%s][%lu] Set: %x %x %x %x %x %x\n", __func__, pthread_self() , boot[0], boot[1], boot[2], boot[3], boot[4], boot[5]);
#endif

  //set data
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
  pal_set_boot_order(slot_id, boot, NULL, &res_len);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);

  IsTimerStart[slot_id - 1] = false;

  pthread_exit(0);
}

/*
 * Function(s) to handle IPMI messages with NetFn: Chassis
 */
// Get Chassis Status (IPMI/Section 28.2)
static void
chassis_get_status (unsigned char *request, unsigned char req_len,
                    unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = CC_SUCCESS;

  pal_get_chassis_status(req->payload_id, req->data, res->data, res_len);
}

static void
chassis_control(unsigned char *request, unsigned char req_len,
                unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_chassis_control(req->payload_id, req->data, (req_len - 3));
  if (ret == PAL_ENOTSUP) {
    res->cc = CC_INVALID_CMD;
  } else {
    res->cc = ret;
  }

  *res_len = 0;
  return;
}

// Set Power Restore Policy (IPMI/Section 28.8)
static void
chassis_set_power_restore_policy(unsigned char *request, unsigned char req_len,
                                 unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  *data++ = 0x07;  // Power restore policy support(bitfield)

  res->cc = pal_set_power_restore_policy(req->payload_id, req->data, res->data);
  if (res->cc == CC_SUCCESS) {
    *res_len = data - &res->data[0];
  }
}

// Set Power Restore Policy (IPMI/Section 28.11)
static void
chassis_get_system_restart_cause(unsigned char *request, unsigned char req_len,
                                 unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  *res_len = 0;
  if (pal_get_restart_cause(req->payload_id, &data[0])) {
    res->cc = CC_UNSPECIFIED_ERROR;
  }
  data[1] = 0; // Channel number
  res->cc = CC_SUCCESS;
  *res_len = 2;
}

static void
chassis_identify(unsigned char *request, unsigned char req_len,
                 unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_set_slot_led(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

#ifdef CHASSIS_GET_BOOT_OPTION_SUPPORT
// Get System Boot Options (IPMI/Section 28.12)
static void
chassis_get_boot_options (unsigned char *request, unsigned char req_len,
                          unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char param = req->data[0];

  if(param >= 8) {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    return;
  }

  // Fill response with default values
  res->cc = CC_SUCCESS;
  *data++ = 0x01;   // Parameter Version
  *data++ = req->data[0]; // Parameter

  *res_len = pal_get_boot_option(param, data) + 2;
}
#endif

#ifdef CHASSIS_SET_BOOT_OPTION_SUPPORT
// Set System Boot Options (IPMI/Section 28)
static void
chassis_set_boot_options (unsigned char *request, unsigned char req_len,
                          unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char param = req->data[0];

  // Fill response with default values
  if(param >= 8) {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    return;
  }

  res->cc = CC_SUCCESS;

  pal_set_boot_option(param,req->data+1);

  *res_len = data - &res->data[0];
}
#endif

// Handle Chassis Commands (IPMI/Section 28)
static void
ipmi_handle_chassis (unsigned char *request, unsigned char req_len,
         unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_chassis);
  switch (cmd)
  {
    case CMD_CHASSIS_GET_STATUS:
      chassis_get_status (request, req_len, response, res_len);
      break;
    case CMD_CHASSIS_CONTROL:
      chassis_control(request, req_len, response, res_len);
      break;
    case CMD_CHASSIS_IDENTIFY:
      chassis_identify (request, req_len, response, res_len);
      break;
    case CMD_CHASSIS_SET_POWER_RESTORE_POLICY:
      chassis_set_power_restore_policy(request, req_len, response, res_len);
      break;
    case CMD_CHASSIS_GET_SYSTEM_RESTART_CAUSE:
      chassis_get_system_restart_cause(request, req_len, response, res_len);
      break;
#ifdef CHASSIS_GET_BOOT_OPTION_SUPPORT
    case CMD_CHASSIS_GET_BOOT_OPTIONS:
      chassis_get_boot_options(request, req_len, response, res_len);
      break;
#endif
#ifdef CHASSIS_SET_BOOT_OPTION_SUPPORT
    case CMD_CHASSIS_SET_BOOT_OPTIONS:
      chassis_set_boot_options(request, req_len, response, res_len);
      break;
#endif
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_chassis);
}

/*
 * Function(s) to handle IPMI messages with NetFn: Sensor
 */
// Platform Event Message (IPMI/Section 29.3)
static void
sensor_plat_event_msg(unsigned char *request, unsigned char req_len,
                      unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int record_id;    // Record ID for added entry
  int ret;

  sel_msg_t entry;

  entry.msg[2] = 0x02;  /* Set Record Type to be system event record.*/

  if (req_len == 11) { // For messaging from system interface
    entry.msg[7] = req->data[0];  //Store Generator ID
    memcpy(&entry.msg[9], req->data + 1, 7);
  } else {
    memcpy(&entry.msg[9], req->data, 7);  // Platform event provides only last 7 bytes of SEL's 16-byte entry
  }

  // Use platform APIs to add the new SEL entry
  ret = sel_add_entry (req->payload_id, &entry, &record_id);
  if (ret)
  {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  res->cc = CC_SUCCESS;
}

// Alert Immediate Command (IPMI/Section 30.7)
static void
sensor_alert_immediate_msg(unsigned char *request, unsigned char req_len,
                      unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int record_id;    // Record ID for added entry
  int ret;
  sel_msg_t entry;

  entry.msg[2] = 0x02;  /* Set Record Type to be system event record.*/

  memcpy(&entry.msg[10], &req->data[5], 6);

  // Use platform APIs to add the new SEL entry
  ret = sel_add_entry (req->payload_id, &entry, &record_id);
  if (ret)
  {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  res->cc = CC_SUCCESS;
}

// Set sensor reading (IPMI/Section 35.17)
static void
sensor_set_reading(unsigned char *request, unsigned char req_len,
                      unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t sensor_num;
  uint8_t flags;
  float value;
  bool available = true;

  // We do not support the command in full.
  if (length_check(3, req_len, response, res_len))
    return;
  sensor_num = req->data[0];
  flags = req->data[1];
  value = req->data[2];
  // We support the case only when flags == 1 (write given value
  // to sensor reading byte ([1:0] - sensor reading operation))
  if (flags != 0x1) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    return;
  }
  if (!pal_sensor_is_source_host(req->payload_id, sensor_num)) {
    res->cc = CC_INVALID_PARAM;
    return;
  }
  // may use NVMe-MI CTemp spec or other spec to decode cache send from IPMI command
  if (pal_correct_sensor_reading_from_cache(req->payload_id, sensor_num, &value) != 0) {
    available = false;
  }

  if (sensor_cache_write(req->payload_id, sensor_num, available, value)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }
  res->cc = CC_SUCCESS;
}



// Handle Sensor/Event Commands (IPMI/Section 29)
static void
ipmi_handle_sensor(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_sensor);
  switch (cmd)
  {
    case CMD_SENSOR_PLAT_EVENT_MSG:
      sensor_plat_event_msg(request, req_len, response, res_len);
      break;
    case CMD_SENSOR_ALERT_IMMEDIATE_MSG:
      sensor_alert_immediate_msg(request, req_len, response, res_len);
      break;
    case CMD_SENSOR_SET_SENSOR_READING:
      sensor_set_reading(request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_sensor);
}

/*
 * Function(s) to handle IPMI messages with NetFn: Application
 */
// Get Device ID (IPMI/Section 20.1)
static void
app_get_device_id (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{

  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  FILE *fp=NULL;
  int fv_major = 0x01, fv_minor = 0x03, fv_patch = 0x00;
  char buffer[64];

  if(length_check(0, req_len, response, res_len))
  {
    return;
  }

  fp = fopen("/etc/issue","r");
  if (fp != NULL)
  {
    if (fgets(buffer, sizeof(buffer), fp)) {
      char *version = strstr(buffer, "-v");
      if (version != NULL) {
        sscanf(version, "-v%d.%d.%d", &fv_major, &fv_minor, &fv_patch);
      }
    }
    fclose(fp);
  }

  // If we are using date based versioning, return
  // the two digit year instead of the 4 digit year.
  // Look out for year 2127 when this overflows the
  // 7 bit allowance for major version :-)
  if (fv_major > 2000) {
    fv_major -= 2000;
  }

  res->cc = CC_SUCCESS;

  //TODO: Following data needs to be updated based on platform
  *data++ = 0x20;   // Device ID
  *data++ = 0x81;   // Device Revision
  *data++ = fv_major & 0x7f;      // Firmware Revision Major
  *data++ = ((fv_minor / 10) << 4) | (fv_minor % 10);      // Firmware Revision Minor
  *data++ = 0x02;   // IPMI Version
  *data++ = 0xBF;   // Additional Device Support
  *data++ = 0x15;   // Manufacturer ID1
  *data++ = 0xA0;   // Manufacturer ID2
  *data++ = 0x00;   // Manufacturer ID3
  *data++ = 0x46;   // Product ID1
  *data++ = 0x31;   // Product ID2
  *data++ = (uint8_t) fv_patch; // Aux. Firmware Version1 (patch ver)
  *data++ = 0x00;               // Aux. Firmware Version2
  *data++ = 0x00;               // Aux. Firmware Version3
  *data++ = 0x00;               // Aux. Firmware Version4

  *res_len = data - &res->data[0];
}

static void *
wait_and_reboot(void *unused __attribute__((unused))) {
  sleep(1);
  pal_bmc_reboot(RB_AUTOBOOT);
  return NULL;
}

// Cold Reset (IPMI/Section 20.2)
static void
app_cold_reset(unsigned char *request, unsigned char req_len,
              unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  int i;
  pthread_t do_reboot;

  *res_len = 0;
  res->cc = CC_SUCCESS;
  if (pal_is_fw_update_ongoing_system()) {
    res->cc = CC_NODE_BUSY;
    return;
  }
  for (i = 1; i < MAX_NUM_FRUS; i++) {
    if (pal_is_crashdump_ongoing(i)) {
      res->cc = CC_NODE_BUSY;
      return;
    }
  }
  for (i = 1; i < MAX_NUM_FRUS; i++) {
    if (pal_is_cplddump_ongoing(i)) {
      res->cc = CC_NODE_BUSY;
      return;
    }
  }

  syslog(LOG_CRIT, "BMC Cold Reset.");
  if (pthread_create(&do_reboot, NULL, wait_and_reboot, NULL) < 0) {
    syslog(LOG_WARNING, "pthread_create for doing reset failed\n");
  }
}


// Get Self Test Results (IPMI/Section 20.4)
static void
app_get_selftest_results (unsigned char *request, unsigned char req_len,
              unsigned char *response, unsigned char *res_len)
{

  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  res->cc = CC_SUCCESS;

  //TODO: Following data needs to be updated based on self-test results
  *data++ = 0x55;   // Self-Test result
  *data++ = 0x00;   // Extra error info in case of failure

  *res_len = data - &res->data[0];
}

// Manufacturing Test On (IPMI/Section 20.5)
static void
app_manufacturing_test_on (unsigned char *request, unsigned char req_len,
                           unsigned char *response, unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  char stat_buf[10] = {0};
  size_t len = 0;
  bool is_first_exc = false;

  res->cc = CC_SUCCESS;

  if ((!memcmp(req->data, "sled-cycle", strlen("sled-cycle"))) &&
      (req_len - ((void*)req->data - (void*)req)) == strlen("sled-cycle")) {
    if (system("/usr/local/bin/power-util sled-cycle") != 0) {
      res->cc = CC_UNSPECIFIED_ERROR;
    }
  } else if ((!memcmp(req->data, "obmc-dump", strlen("obmc-dump"))) &&
      (req_len - ((void*)req->data - (void*)req)) == strlen("obmc-dump")) {
    if (kv_get(OBMC_DUMP_STAT_KEY, stat_buf, &len, 0) < 0) {
      is_first_exc = true;
    }
    // If obmc-dump is ongiong, skip the request
    if ((is_first_exc == true) || strncmp(stat_buf, "Ongoing", strlen("Ongoing")) != 0) {
      if (system("/usr/local/bin/obmc-dump -y &") != 0) {
        res->cc = CC_NOT_SUPP_IN_CURR_STATE;
      }
    }
  } else if ((!memcmp(req->data, "obmc-dump -s", strlen("obmc-dump -s"))) &&
      (req_len - ((void*)req->data - (void*)req)) == strlen("obmc-dump -s")) {
    if (kv_get(OBMC_DUMP_STAT_KEY, stat_buf, &len, 0) < 0) {
      *data++ = DUMP_NEVER;
      goto exit;
    }
    if (strncmp(stat_buf, "Ongoing", strlen("Ongoing")) == 0) {
      *data++ = DUMP_ONGOING;
    } else if (strncmp(stat_buf, "Done", strlen("Done")) == 0) {
      *data++ = DUMP_DONE;
    } else if (strncmp(stat_buf, "Fail", strlen("Fail")) == 0) {
      *data++ = DUMP_FAIL;
    } else {
      res->cc = CC_UNSPECIFIED_ERROR;
    }
  } else {
    res->cc = CC_INVALID_PARAM;
  }

exit:
  *res_len = data - &res->data[0];
}

// Get Device GUID (IPMI/Section 20.8)
static void
app_get_device_guid (unsigned char *request, unsigned char req_len,
                    unsigned char *response, unsigned char *res_len)
{
  int ret;

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  // Get the 16 bytes of Device GUID from PAL library
  ret = pal_get_dev_guid(req->payload_id, (char *)res->data);
  if (ret) {
      res->cc = CC_UNSPECIFIED_ERROR;
      *res_len = 0x00;
  } else {
      res->cc = CC_SUCCESS;
      *res_len = SIZE_GUID;
  }
}

static void
app_get_device_sys_guid (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  int ret;

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  // Get the 16 bytes of System GUID from PAL library
  ret = pal_get_sys_guid(req->payload_id, (char *)res->data);
  if (ret) {
      res->cc = CC_UNSPECIFIED_ERROR;
      *res_len = 0x00;
  } else {
      res->cc = CC_SUCCESS;
      *res_len = SIZE_GUID;
  }
}

static void
oem_set_device_sys_guid (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  int ret;

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  // Get the 16 bytes of System GUID from PAL library
  ret = pal_set_sys_guid(req->payload_id, (char *)req->data);
  if (ret) {
      res->cc = CC_UNSPECIFIED_ERROR;
      *res_len = 0x00;
  } else {
      res->cc = CC_SUCCESS;
      *res_len = 0x00;
  }
}

// Reset Watchdog Timer (IPMI/Section 27.5)
static void
app_reset_watchdog_timer (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  struct watchdog_data *wdt = get_watchdog(req->payload_id);

  if (!wdt) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  pthread_mutex_lock(&wdt->mutex);
  if (wdt->valid) {
    res->cc = CC_SUCCESS;
    wdt->present_count_down = wdt->init_count_down;
    wdt->run = 1;
  }
  else
    res->cc = CC_INVALID_PARAM; // un-initialized watchdog
  pthread_mutex_unlock(&wdt->mutex);
  *res_len = data - &res->data[0];
}

// Set Watchdog Timer (IPMI/Section 27.6)
static void
app_set_watchdog_timer (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t*) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  struct watchdog_data *wdt = get_watchdog(req->payload_id);

  if (!wdt) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  // no support pre-itmeout interrupt
  if (req->data[1] & 0xF0) {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  pthread_mutex_lock(&wdt->mutex);
  wdt->no_log = req->data[0] >> 7;
  wdt->use = req->data[0] & 0x7;
  wdt->pre_action = 0; // no support
  wdt->action = req->data[1] & 0x7;
  wdt->pre_interval = req->data[2];
  wdt->expiration &= ~(req->data[3]);
  wdt->init_count_down = (req->data[5]<<8 | req->data[4]);
  wdt->present_count_down = wdt->init_count_down;
  if (!(req->data[0] & 0x40)) // 'do not stop timer' bit
    wdt->run = 0;
  wdt->valid = 1;
  pthread_mutex_unlock(&wdt->mutex);
  res->cc = CC_SUCCESS;

  *res_len = data - &res->data[0];
}

// Get Watchdog Timer (IPMI/Section 27.7)
static void
app_get_watchdog_timer (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char byte;
  struct watchdog_data *wdt = get_watchdog(req->payload_id);

  if (!wdt) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  pthread_mutex_lock(&wdt->mutex);
  byte = wdt->use;
  if (wdt->no_log)
    byte |= 0x80;
  if (wdt->run)
    byte |= 0x40;
  *data++ = byte;
  *data++ = (wdt->pre_action << 4 | wdt->action);
  *data++ = wdt->pre_interval;
  *data++ = wdt->expiration;
  *data++ = wdt->init_count_down & 0xFF;
  *data++ = (wdt->init_count_down >> 8) & 0xFF;
  *data++ = wdt->present_count_down & 0xFF;
  *data++ = (wdt->present_count_down >> 8) & 0xFF;
  pthread_mutex_unlock(&wdt->mutex);
  res->cc = CC_SUCCESS;

  *res_len = data - &res->data[0];
}

// Set BMC Global Enables (IPMI/Section 22.1)
static void
app_set_global_enables (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  bmc_global_enable_setting[req->payload_id - 1] = req->data[0];

  res->cc = CC_SUCCESS;

  // Do nothing

  *res_len = 0;
}

// Get BMC Global Enables (IPMI/Section 22.2)
static void
app_get_global_enables (unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  res->cc = CC_SUCCESS;

  *data++ = bmc_global_enable_setting[req->payload_id - 1];    // Global Enable

  *res_len = data - &res->data[0];
}

// Clear Message flags Command (IPMI/Section 22.3)
static void
app_clear_message_flags (unsigned char *request, unsigned char req_len,
                         unsigned char *response, unsigned char *res_len)
{

  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  res->cc = CC_SUCCESS;

  // Do nothing

  *res_len = data - &res->data[0];
}

// Set System Info Params (IPMI/Section 22.14a)
static void
app_set_sys_info_params (unsigned char *request, unsigned char req_len,
                         unsigned char *response, unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char param = req->data[0];

  res->cc = CC_SUCCESS;

  switch (param)
  {
    case SYS_INFO_PARAM_SET_IN_PROG:
      g_sys_info_params.set_in_prog = req->data[1];
      break;
    case SYS_INFO_PARAM_SYSFW_VER:
      memcpy(g_sys_info_params.sysfw_ver, &req->data[1], SIZE_SYSFW_VER);
      pal_set_sysfw_ver(req->payload_id, g_sys_info_params.sysfw_ver);
      break;
    case SYS_INFO_PARAM_SYS_NAME:
      memcpy(g_sys_info_params.sys_name, &req->data[1], SIZE_SYS_NAME);
      break;
    case SYS_INFO_PARAM_PRI_OS_NAME:
      memcpy(g_sys_info_params.pri_os_name, &req->data[1], SIZE_OS_NAME);
      break;
    case SYS_INFO_PARAM_PRESENT_OS_NAME:
      memcpy(g_sys_info_params.present_os_name, &req->data[1], SIZE_OS_NAME);
      break;
    case SYS_INFO_PARAM_PRESENT_OS_VER:
      memcpy(g_sys_info_params.present_os_ver, &req->data[1], SIZE_OS_VER);
      break;
    case SYS_INFO_PARAM_BMC_URL:
      memcpy(g_sys_info_params.bmc_url, &req->data[1], SIZE_BMC_URL);
      break;
    case SYS_INFO_PARAM_OS_HV_URL:
      memcpy(g_sys_info_params.os_hv_url, &req->data[1], SIZE_OS_HV_URL);
      break;
    case SYS_INFO_PARAM_BIOS_CURRENT_BOOT_LIST:
      memcpy(g_sys_info_params.bios_current_boot_list, &req->data[1], req_len-4); // boot list length = req_len-4 (payload_id, cmd, netfn, param)
      pal_set_bios_current_boot_list(req->payload_id, g_sys_info_params.bios_current_boot_list, req_len-4, &res->cc);
      break;
    case SYS_INFO_PARAM_BIOS_FIXED_BOOT_DEVICE:
      if(length_check(SIZE_BIOS_FIXED_BOOT_DEVICE+1, req_len, response, res_len))
        break;
      memcpy(g_sys_info_params.bios_fixed_boot_device, &req->data[1], SIZE_BIOS_FIXED_BOOT_DEVICE);
      pal_set_bios_fixed_boot_device(req->payload_id, g_sys_info_params.bios_fixed_boot_device);
      break;
    case SYS_INFO_PARAM_BIOS_RESTORES_DEFAULT_SETTING:
      if(length_check(SIZE_BIOS_RESTORES_DEFAULT_SETTING+1, req_len, response, res_len))
        break;
      memcpy(g_sys_info_params.bios_restores_default_setting, &req->data[1], SIZE_BIOS_RESTORES_DEFAULT_SETTING);
      pal_set_bios_restores_default_setting(req->payload_id, g_sys_info_params.bios_restores_default_setting);
      break;
    case SYS_INFO_PARAM_LAST_BOOT_TIME:
      if(length_check(SIZE_LAST_BOOT_TIME+1, req_len, response, res_len))
        break;
      memcpy(g_sys_info_params.last_boot_time, &req->data[1], SIZE_LAST_BOOT_TIME);
      pal_set_last_boot_time(req->payload_id, g_sys_info_params.last_boot_time);
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }

  return;
}

// Get System Info Params (IPMI/Section 22.14b)
static void
app_get_sys_info_params (unsigned char *request, unsigned char req_len,
                         unsigned char *response, unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char param = req->data[1];

  // Fill default return values
  res->cc = CC_SUCCESS;
  *data++ = 1;		// Parameter revision
  if(!length_check(4, req_len, response, res_len))
  {
    switch (param)
    {
      case SYS_INFO_PARAM_SET_IN_PROG:
        *data++ = g_sys_info_params.set_in_prog;
        break;
      case SYS_INFO_PARAM_SYSFW_VER:
        pal_get_sysfw_ver(req->payload_id, g_sys_info_params.sysfw_ver);
        memcpy(data, g_sys_info_params.sysfw_ver, SIZE_SYSFW_VER);
        data += SIZE_SYSFW_VER;
        break;
      case SYS_INFO_PARAM_SYS_NAME:
        memcpy(data, g_sys_info_params.sys_name, SIZE_SYS_NAME);
        data += SIZE_SYS_NAME;
        break;
      case SYS_INFO_PARAM_PRI_OS_NAME:
        memcpy(data, g_sys_info_params.pri_os_name, SIZE_OS_NAME);
        data += SIZE_OS_NAME;
        break;
      case SYS_INFO_PARAM_PRESENT_OS_NAME:
        memcpy(data, g_sys_info_params.present_os_name, SIZE_OS_NAME);
        data += SIZE_OS_NAME;
        break;
      case SYS_INFO_PARAM_PRESENT_OS_VER:
        memcpy(data, g_sys_info_params.present_os_ver, SIZE_OS_VER);
        data += SIZE_OS_VER;
        break;
      case SYS_INFO_PARAM_BMC_URL:
        memcpy(data, g_sys_info_params.bmc_url, SIZE_BMC_URL);
        data += SIZE_BMC_URL;
        break;
      case SYS_INFO_PARAM_OS_HV_URL:
        memcpy(data, g_sys_info_params.os_hv_url, SIZE_OS_HV_URL);
        data += SIZE_OS_HV_URL;
        break;
      case SYS_INFO_PARAM_BIOS_CURRENT_BOOT_LIST:
        if(pal_get_bios_current_boot_list(req->payload_id, g_sys_info_params.bios_current_boot_list, res_len))
        {
          res->cc = CC_UNSPECIFIED_ERROR;
          break;
        }
        memcpy(data, g_sys_info_params.bios_current_boot_list, *res_len);
        data += *res_len;
        break;
      case SYS_INFO_PARAM_BIOS_FIXED_BOOT_DEVICE:
        if(pal_get_bios_fixed_boot_device(req->payload_id, g_sys_info_params.bios_fixed_boot_device))
        {
          res->cc = CC_UNSPECIFIED_ERROR;
          break;
        }
        memcpy(data, g_sys_info_params.bios_fixed_boot_device, SIZE_BIOS_FIXED_BOOT_DEVICE);
        data += SIZE_BIOS_FIXED_BOOT_DEVICE;
        break;
      case SYS_INFO_PARAM_BIOS_RESTORES_DEFAULT_SETTING:
        if(pal_get_bios_restores_default_setting(req->payload_id, g_sys_info_params.bios_restores_default_setting))
        {
          res->cc = CC_UNSPECIFIED_ERROR;
          break;
        }
        memcpy(data, g_sys_info_params.bios_restores_default_setting, SIZE_BIOS_RESTORES_DEFAULT_SETTING);
        data += SIZE_BIOS_RESTORES_DEFAULT_SETTING;
        break;
      case SYS_INFO_PARAM_LAST_BOOT_TIME:
        if(pal_get_last_boot_time(req->payload_id, g_sys_info_params.last_boot_time))
        {
          res->cc = CC_UNSPECIFIED_ERROR;
          break;
        }
        memcpy(data, g_sys_info_params.last_boot_time, SIZE_LAST_BOOT_TIME);
        data += SIZE_LAST_BOOT_TIME;
        break;
      default:
        res->cc = CC_INVALID_PARAM;
        break;
    }
  }

  if (res->cc == CC_SUCCESS) {
    *res_len = data - &res->data[0];
  }

  return;
}

static void
app_master_write_read (unsigned char *request, unsigned char req_len,
                         unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t *ptr = req->data;
  uint8_t writeCnt, readCnt;
  uint8_t buf[42];
  uint8_t bus_num, ret;
  int fd;
  char fn[32];

  if( req_len < 6 ){
    res->cc = CC_INVALID_LENGTH;
    return;
  }

  readCnt = req->data[2];
  writeCnt = req_len - 6;
  if ( ptr[0] & 0x01 ) {
    if ( (readCnt > 35) || (writeCnt > 36) ) {
      res->cc = CC_INVALID_DATA_FIELD;
      return;
    } else {
      if ( pal_is_cmd_valid(req->data) ) {
        res->cc = CC_INVALID_DATA_FIELD;
        return;
      }

      memcpy(buf, &req->data[3], writeCnt);
      bus_num = ((req->data[0] & 0x7E) >> 1); //extend bit[7:1] for bus ID
      //ret = pal_i2c_write_read(bus_num, req->data[1], buf, writeCnt, res->data, readCnt);
      snprintf(fn, sizeof(fn), "/dev/i2c-%d", bus_num);

      fd = open(fn, O_RDWR);
      if (fd < 0) {
        res->cc = CC_UNSPECIFIED_ERROR;
        return;
      }

      ret = i2c_rdwr_msg_transfer(fd, req->data[1], buf, writeCnt, res->data, readCnt);
      if(!ret) {
         *res_len = readCnt;
         res->cc = CC_SUCCESS;
      } else {
        syslog(LOG_WARNING, "app_master_write_read: master read fail, bus %d, addr %x", bus_num, req->data[1]);
          res->cc = CC_UNSPECIFIED_ERROR;
      }
      close(fd);
    }
  }
  else
  {
    res->cc = CC_INVALID_DATA_FIELD;
  }
}

static void
app_get_sys_intf_caps (unsigned char *request, unsigned char req_len,
                         unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  res->cc = CC_SUCCESS;

  switch (req->data[0]) {
    case 0:  // SSIF
      *data++ = 0x00;
      *data++ = 0x80;
      *data++ = 255;
      *data++ = 125;
      break;
    case 1:  // KCS
      *data++ = 0x00;
      *data++ = 0x00;
      *data++ = 255;
      break;
    default:
      res->cc = CC_INVALID_DATA_FIELD;
      break;
  }

  *res_len = data - &res->data[0];
  pal_get_sys_intf_caps(req->payload_id, req->data, res->data, res_len);
}

// Handle Appliction Commands (IPMI/Section 20)
static void
ipmi_handle_app (unsigned char *request, unsigned char req_len,
                unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_app);
  switch (cmd)
  {
    case CMD_APP_GET_DEVICE_ID:
      app_get_device_id (request, req_len, response, res_len);
      break;
    case CMD_APP_COLD_RESET:
      if(pal_is_fw_update_ongoing_system())
        res->cc = CC_NODE_BUSY;
      else
        app_cold_reset (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_SELFTEST_RESULTS:
      app_get_selftest_results (request, req_len, response, res_len);
      break;
    case CMD_APP_MANUFACTURING_TEST_ON:
      if(pal_is_fw_update_ongoing_system())
        res->cc = CC_NODE_BUSY;
      else
        app_manufacturing_test_on (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_DEVICE_GUID:
      app_get_device_guid (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_SYSTEM_GUID:
      app_get_device_sys_guid (request, req_len, response, res_len);
      break;
    case CMD_APP_RESET_WDT:
      app_reset_watchdog_timer (request, req_len, response, res_len);
      break;
    case CMD_APP_SET_WDT:
      app_set_watchdog_timer (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_WDT:
      app_get_watchdog_timer (request, req_len, response, res_len);
      break;
    case CMD_APP_SET_GLOBAL_ENABLES:
      app_set_global_enables (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_GLOBAL_ENABLES:
      app_get_global_enables (request, req_len, response, res_len);
      break;
    case CMD_APP_SET_SYS_INFO_PARAMS:
      app_set_sys_info_params (request, req_len, response, res_len);
      break;
    case CMD_APP_CLEAR_MESSAGE_FLAGS:
      app_clear_message_flags (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_SYS_INFO_PARAMS:
      app_get_sys_info_params (request, req_len, response, res_len);
      break;
    case CMD_APP_MASTER_WRITE_READ:
      app_master_write_read (request, req_len, response, res_len);
      break;
    case CMD_APP_GET_SYS_INTF_CAPS:
      app_get_sys_intf_caps (request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_app);
}

/*
 * Function(s) to handle IPMI messages with NetFn: Storage
 */

static void
storage_get_fruid_info(unsigned char *request, unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int size = plat_fruid_size(req->payload_id);

  res->cc = CC_SUCCESS;

  *data++ = size & 0xFF; // FRUID size LSB
  *data++ = (size >> 8) & 0xFF; // FRUID size MSB
  *data++ = 0x00; // Device accessed by bytes

  *res_len = data - &res->data[0];

  return;
}

static void
storage_get_fruid_data(unsigned char *request, unsigned char *response,
    unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  int fru_id = req->data[0];
  int offset = req->data[1] + (req->data[2] << 8);
  int count = req->data[3];

  int ret = plat_fruid_data(req->payload_id, fru_id, offset, count, &(res->data[1]));
  if (ret) {
    res->cc = CC_UNSPECIFIED_ERROR;
  } else {
    res->cc = CC_SUCCESS;
    *data++ = count;
    data += count;
  }

  if (res->cc == CC_SUCCESS) {
    *(unsigned short*)res_len = data - &res->data[0];
  }
  return;
}

static void
storage_get_sdr_info (unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int num_entries;    // number of sdr records
  int free_space;   // free space in SDR device in bytes
  time_stamp_t ts_recent_add; // Recent Addition Timestamp
  time_stamp_t ts_recent_erase; // Recent Erasure Timestamp

  // Use platform APIs to get SDR information
  num_entries = sdr_num_entries ();
  free_space = sdr_free_space ();
  sdr_ts_recent_add (&ts_recent_add);
  sdr_ts_recent_erase (&ts_recent_erase);

  res->cc = CC_SUCCESS;

  *data++ = IPMI_SDR_VERSION; // SDR version
  *data++ = num_entries & 0xFF; // number of sdr entries
  *data++ = (num_entries >> 8) & 0xFF;
  *data++ = free_space & 0xFF;  // Free SDR Space
  *data++ = (free_space >> 8) & 0xFF;

  memcpy(data, ts_recent_add.ts, SIZE_TIME_STAMP);
  data += SIZE_TIME_STAMP;

  memcpy(data, ts_recent_erase.ts, SIZE_TIME_STAMP);
  data += SIZE_TIME_STAMP;

  *data++ = 0x02;   // Operations supported

  *res_len = data - &res->data[0];

  return;
}

static void
storage_rsv_sdr (unsigned char *request, unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int rsv_id;     // SDR reservation ID

  // Use platform APIs to get a SDR reservation ID
  rsv_id = sdr_rsv_id(req->payload_id);
  if (rsv_id < 0)
  {
      res->cc = CC_UNSPECIFIED_ERROR;
      return;
  }

  res->cc = CC_SUCCESS;
  *data++ = rsv_id & 0xFF;  // Reservation ID
  *data++ = (rsv_id >> 8) & 0XFF;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_get_sdr (unsigned char *request, unsigned char *response,
     unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  int read_rec_id;    //record ID to be read
  int next_rec_id;    //record ID for the next entry
  int rsv_id;     // Reservation ID for the request
  int rec_offset;   // Read offset into the record
  int rec_bytes;    // Number of bytes to be read
  sdr_rec_t entry;    // SDR record entry
  int ret;

  rsv_id = (req->data[1] << 8) | req->data[0];
  read_rec_id = (req->data[3] << 8) | req->data[2];
  rec_offset = req->data[4];
  rec_bytes = req->data[5];

  // Use platform API to read the record Id and get next ID
  ret = sdr_get_entry (req->payload_id, rsv_id, read_rec_id, &entry, &next_rec_id);
  if (ret)
  {
      res->cc = CC_UNSPECIFIED_ERROR;
      return;
  }

  res->cc = CC_SUCCESS;
  *data++ = next_rec_id & 0xFF; // next record ID
  *data++ = (next_rec_id >> 8) & 0xFF;

  memcpy (data, &entry.rec[rec_offset], rec_bytes);
  data += rec_bytes;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_get_sel_info (unsigned char *request, unsigned char *response,
                      unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int num_entries;    // number of log entries
  int free_space;   // free space in SEL device in bytes
  time_stamp_t ts_recent_add; // Recent Addition Timestamp
  time_stamp_t ts_recent_erase; // Recent Erasure Timestamp

  // Use platform APIs to get SEL information
  num_entries = sel_num_entries (req->payload_id);
  free_space = sel_free_space (req->payload_id);
  sel_ts_recent_add (req->payload_id, &ts_recent_add);
  sel_ts_recent_erase (req->payload_id, &ts_recent_erase);

  res->cc = CC_SUCCESS;

  *data++ = IPMI_SEL_VERSION; // SEL version
  *data++ = num_entries & 0xFF; // number of log entries
  *data++ = (num_entries >> 8) & 0xFF;
  *data++ = free_space & 0xFF;  // Free SEL Space
  *data++ = (free_space >> 8) & 0xFF;

  memcpy(data, ts_recent_add.ts, SIZE_TIME_STAMP);
  data += SIZE_TIME_STAMP;

  memcpy(data, ts_recent_erase.ts, SIZE_TIME_STAMP);
  data += SIZE_TIME_STAMP;

  *data++ = 0x02;   // Operations supported

  *res_len = data - &res->data[0];

  return;
}

static void
storage_rsv_sel (unsigned char * request, unsigned char *response,
                  unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int rsv_id;     // SEL reservation ID

  // Use platform APIs to get a SEL reservation ID
  rsv_id = sel_rsv_id (req->payload_id);
  if (rsv_id < 0)
  {
      res->cc = CC_SEL_ERASE_PROG;
      return;
  }

  res->cc = CC_SUCCESS;
  *data++ = rsv_id & 0xFF;  // Reservation ID
  *data++ = (rsv_id >> 8) & 0XFF;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_get_sel (unsigned char *request, unsigned char *response,
     unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  int read_rec_id;    //record ID to be read
  int next_rec_id;    //record ID for the next msg
  sel_msg_t entry;    // SEL log entry
  int ret;
  unsigned char offset = req->data[4];
  unsigned char len = req->data[5];

  if (len == 0xFF) {  // FFh means read entire record
    offset = 0;
    len = SIZE_SEL_REC;
  } else if ((offset >= SIZE_SEL_REC) || (len > SIZE_SEL_REC) || ((offset+len) > SIZE_SEL_REC)) {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    return;
  }

  read_rec_id = (req->data[3] << 8) | req->data[2];

  // Use platform API to read the record Id and get next ID
  ret = sel_get_entry (req->payload_id, read_rec_id, &entry, &next_rec_id);
  if (ret)
  {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  res->cc = CC_SUCCESS;
  *data++ = next_rec_id & 0xFF; // next record ID
  *data++ = (next_rec_id >> 8) & 0xFF;

  memcpy(data, &entry.msg[offset], len);
  data += len;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_add_sel (unsigned char *request, unsigned char *response,
     unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];


  int record_id;    // Record ID for added entry
  int ret;

  sel_msg_t entry;

  memcpy(entry.msg, req->data, SIZE_SEL_REC);

  // Use platform APIs to add the new SEL entry
  ret = sel_add_entry (req->payload_id, &entry, &record_id);
  if (ret)
  {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  res->cc = CC_SUCCESS;
  *data++ = record_id & 0xFF;
  *data++ = (record_id >> 8) & 0xFF;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_clr_sel (unsigned char *request, unsigned char *response,
     unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  sel_erase_stat_t status = SEL_ERASE_DONE;
  int ret;
  int rsv_id;

  // Verify the request to contain 'CLR' characters
  if ((req->data[2] != 'C') || (req->data[3] != 'L') || (req->data[4] != 'R'))
  {
    res->cc = CC_INVALID_PARAM;
    return;
  }

  // Populate reservation ID given in request
  rsv_id = (req->data[1] << 8) | req->data[0];

  // Use platform APIs to clear or get status
  if (req->data[5] == IPMI_SEL_INIT_ERASE)
  {
    ret = sel_erase (req->payload_id, rsv_id);
  }
  else if (req->data[5] == IPMI_SEL_ERASE_STAT)
  {
    ret = sel_erase_status (req->payload_id, rsv_id, &status);
  }
  else
  {
    res->cc = CC_INVALID_PARAM;
    return;
  }

  // Handle platform error and return
  if (ret)
    {
      res->cc = CC_UNSPECIFIED_ERROR;
      return;
    }

  res->cc = CC_SUCCESS;
  *data++ = status;

  *res_len = data - &res->data[0];

  return;
}

static void
storage_get_sel_time (unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = CC_SUCCESS;

  time_stamp_fill(res->data);

  *res_len = SIZE_TIME_STAMP;

  return;
}

#if defined(CONFIG_FBY2_ND)
static void
storage_set_sel_time (unsigned char *request, unsigned char *response, unsigned char req_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  static uint8_t time_sync = 0;

  if (req_len != 7) { // payload_id netfn_lun cmd data[4];
    res->cc = CC_INVALID_LENGTH;
  }

  if (!time_sync) {
    if (pal_set_time_sync(req->data,req_len-3)) {
      res->cc = CC_UNSPECIFIED_ERROR;
      return;
    }
    res->cc = CC_SUCCESS;
    time_sync = 1;
  } else {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
  }
  return;
}
#endif

static void
storage_get_sel_utc (unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];

  res->cc = CC_SUCCESS;

  // TODO: For now, the SEL time stamp is based on UTC time,
  // so return 0x0000 as offset. Might need to change once
  // supporting zones in SEL time stamps
  *data++ = 0x00;
  *data++ = 0x00;

  *res_len = data - &res->data[0];
}

static void
ipmi_handle_storage (unsigned char *request, unsigned char req_len,
         unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;

  res->cc = CC_SUCCESS;
  *res_len = 0;

  pthread_mutex_lock(&m_storage);
  switch (cmd)
  {
    case CMD_STORAGE_GET_FRUID_INFO:
      storage_get_fruid_info (request, response, res_len);
      break;
    case CMD_STORAGE_READ_FRUID_DATA:
      storage_get_fruid_data (request, response, res_len);
      break;
    case CMD_STORAGE_GET_SEL_INFO:
      storage_get_sel_info (request, response, res_len);
      break;
    case CMD_STORAGE_RSV_SEL:
      storage_rsv_sel (request, response, res_len);
      break;
    case CMD_STORAGE_ADD_SEL:
      storage_add_sel (request, response, res_len);
      break;
    case CMD_STORAGE_GET_SEL:
      storage_get_sel (request, response, res_len);
      break;
    case CMD_STORAGE_CLR_SEL:
      storage_clr_sel (request, response, res_len);
      break;
     // To avoid BIOS using this command to update RTC
     // TBD: Respond only if BMC's time has synced with NTP
    case CMD_STORAGE_GET_SEL_TIME:
      storage_get_sel_time (response, res_len);
      break;
#if defined(CONFIG_FBY2_ND)
    case CMD_STORAGE_SET_SEL_TIME:
      storage_set_sel_time (request, response, req_len);
      break;
#endif
    case CMD_STORAGE_GET_SEL_UTC:
      storage_get_sel_utc (response, res_len);
      break;
    case CMD_STORAGE_GET_SDR_INFO:
      storage_get_sdr_info (response, res_len);
      break;
    case CMD_STORAGE_RSV_SDR:
      storage_rsv_sdr (request, response, res_len);
      break;
    case CMD_STORAGE_GET_SDR:
      storage_get_sdr (request, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }

  pthread_mutex_unlock(&m_storage);
  return;
}

/*
 * Function(s) to handle IPMI messages with NetFn: Transport
 */

// Set LAN Configuration (IPMI/Section 23.1)
static void
transport_set_lan_config (unsigned char *request, unsigned char *response,
        unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char param = req->data[1];

  // Fill the response with default values
  res->cc = CC_SUCCESS;

  switch (param)
  {
    case LAN_PARAM_SET_IN_PROG:
      g_lan_config.set_in_prog = req->data[2];
      break;
    case LAN_PARAM_AUTH_SUPPORT:
      g_lan_config.auth_support = req->data[2];
      break;
    case LAN_PARAM_AUTH_ENABLES:
      memcpy(g_lan_config.auth_enables, &req->data[2], SIZE_AUTH_ENABLES);
      break;
    case LAN_PARAM_IP_ADDR:
      memcpy(g_lan_config.ip_addr, &req->data[2], SIZE_IP_ADDR);
      break;
    case LAN_PARAM_IP_SRC:
      g_lan_config.ip_src = req->data[2];
      break;
    case LAN_PARAM_MAC_ADDR:
      memcpy(g_lan_config.mac_addr, &req->data[2], SIZE_MAC_ADDR);
      break;
    case LAN_PARAM_NET_MASK:
      memcpy(g_lan_config.net_mask, &req->data[2], SIZE_NET_MASK);
      break;
    case LAN_PARAM_IP_HDR:
      memcpy(g_lan_config.ip_hdr, &req->data[2], SIZE_IP_HDR);
      break;
    case LAN_PARAM_PRI_RMCP_PORT:
      g_lan_config.pri_rmcp_port[0] = req->data[2];
      g_lan_config.pri_rmcp_port[1] = req->data[3];
      break;
    case LAN_PARAM_SEC_RMCP_PORT:
      g_lan_config.sec_rmcp_port[0] = req->data[2];
      g_lan_config.sec_rmcp_port[1] = req->data[3];
      break;
    case LAN_PARAM_ARP_CTRL:
      g_lan_config.arp_ctrl = req->data[2];
      break;
    case LAN_PARAM_GARP_INTERVAL:
      g_lan_config.garp_interval = req->data[2];
      break;
    case LAN_PARAM_DF_GW_IP_ADDR:
      memcpy(g_lan_config.df_gw_ip_addr, &req->data[2], SIZE_IP_ADDR);
      break;
    case LAN_PARAM_DF_GW_MAC_ADDR:
      memcpy(g_lan_config.df_gw_mac_addr, &req->data[2], SIZE_MAC_ADDR);
      break;
    case LAN_PARAM_BACK_GW_IP_ADDR:
      memcpy(g_lan_config.back_gw_ip_addr, &req->data[2], SIZE_IP_ADDR);
      break;
    case LAN_PARAM_BACK_GW_MAC_ADDR:
      memcpy(g_lan_config.back_gw_mac_addr, &req->data[2], SIZE_MAC_ADDR);
      break;
    case LAN_PARAM_COMMUNITY_STR:
      memcpy(g_lan_config.community_str, &req->data[2], SIZE_COMMUNITY_STR);
      break;
    case LAN_PARAM_NO_OF_DEST:
      g_lan_config.no_of_dest = req->data[2];
      break;
    case LAN_PARAM_DEST_TYPE:
      memcpy(g_lan_config.dest_type, &req->data[2], SIZE_DEST_TYPE);
      break;
    case LAN_PARAM_DEST_ADDR:
      memcpy(g_lan_config.dest_addr, &req->data[2], SIZE_DEST_ADDR);
      break;
    case LAN_PARAM_IP6_ADDR:
      memcpy(g_lan_config.ip6_addr, &req->data[2], SIZE_IP6_ADDR);
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }
}

// Get LAN Configuration (IPMI/Section 23.2)
static void
transport_get_lan_config (unsigned char *request, unsigned char *response,
        unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char param = req->data[1];

  // Fill the response with default values
  res->cc = CC_SUCCESS;
  *data++ = 0x11;   // Parameter revision

  switch (param)
  {
    case LAN_PARAM_SET_IN_PROG:
      *data++ = g_lan_config.set_in_prog;
      break;
    case LAN_PARAM_AUTH_SUPPORT:
      *data++ = g_lan_config.auth_support;
      break;
    case LAN_PARAM_AUTH_ENABLES:
      memcpy(data, g_lan_config.auth_enables, SIZE_AUTH_ENABLES);
      data += SIZE_AUTH_ENABLES;
      break;
    case LAN_PARAM_IP_ADDR:
      plat_lan_init(&g_lan_config);
      memcpy(data, g_lan_config.ip_addr, SIZE_IP_ADDR);
      data += SIZE_IP_ADDR;
      break;
    case LAN_PARAM_IP_SRC:
      *data++ = g_lan_config.ip_src;
      break;
    case LAN_PARAM_MAC_ADDR:
      plat_lan_init(&g_lan_config);
      memcpy(data, g_lan_config.mac_addr, SIZE_MAC_ADDR);
      data += SIZE_MAC_ADDR;
      break;
    case LAN_PARAM_NET_MASK:
      memcpy(data, g_lan_config.net_mask, SIZE_NET_MASK);
      data += SIZE_NET_MASK;
      break;
    case LAN_PARAM_IP_HDR:
      memcpy(data, g_lan_config.ip_hdr, SIZE_IP_HDR);
      data += SIZE_IP_HDR;
      break;
    case LAN_PARAM_PRI_RMCP_PORT:
      *data++ = g_lan_config.pri_rmcp_port[0];
      *data++ = g_lan_config.pri_rmcp_port[1];
      break;
    case LAN_PARAM_SEC_RMCP_PORT:
      *data++ = g_lan_config.sec_rmcp_port[0];
      *data++ = g_lan_config.sec_rmcp_port[1];
      break;
    case LAN_PARAM_ARP_CTRL:
      *data++ = g_lan_config.arp_ctrl;
      break;
    case LAN_PARAM_GARP_INTERVAL:
      *data++ = g_lan_config.garp_interval;
      break;
    case LAN_PARAM_DF_GW_IP_ADDR:
      memcpy(data, g_lan_config.df_gw_ip_addr, SIZE_IP_ADDR);
      data += SIZE_IP_ADDR;
      break;
    case LAN_PARAM_DF_GW_MAC_ADDR:
      memcpy(data, g_lan_config.df_gw_mac_addr, SIZE_MAC_ADDR);
      data += SIZE_MAC_ADDR;
      break;
    case LAN_PARAM_BACK_GW_IP_ADDR:
      memcpy(data, g_lan_config.back_gw_ip_addr, SIZE_IP_ADDR);
      data += SIZE_IP_ADDR;
      break;
    case LAN_PARAM_BACK_GW_MAC_ADDR:
      memcpy(data, g_lan_config.back_gw_mac_addr, SIZE_MAC_ADDR);
      data += SIZE_MAC_ADDR;
      break;
    case LAN_PARAM_COMMUNITY_STR:
      memcpy(data, g_lan_config.community_str, SIZE_COMMUNITY_STR);
      data += SIZE_COMMUNITY_STR;
      break;
    case LAN_PARAM_NO_OF_DEST:
      *data++ = g_lan_config.no_of_dest;
      break;
    case LAN_PARAM_DEST_TYPE:
      memcpy(data, g_lan_config.dest_type, SIZE_DEST_TYPE);
      data += SIZE_DEST_TYPE;
      break;
    case LAN_PARAM_DEST_ADDR:
      memcpy(data, g_lan_config.dest_addr, SIZE_DEST_ADDR);
      data += SIZE_DEST_ADDR;
      break;
    case LAN_PARAM_IP6_ADDR:
      plat_lan_init(&g_lan_config);
      memcpy(data, g_lan_config.ip6_addr, SIZE_IP6_ADDR);
      data += SIZE_IP6_ADDR;
      break;
   case LAN_PARAM_IP6_DYNAMIC_ADDR:
      plat_lan_init(&g_lan_config);
      data[0] = 0; // Selector
      data[1] = 0x02; // DHCPv6
      memcpy(&data[2], g_lan_config.ip6_addr, SIZE_IP6_ADDR);
      data[18] = g_lan_config.ip6_prefix;
      data[19] = 0x00; // Active
      data += SIZE_IP6_ADDR + 4;
      break;
    case LAN_PARAM_ADDR_ENABLES:
      data[0] = 0x02; // Enable both IPv4 and IPv6
      data ++;
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }

  if (res->cc == CC_SUCCESS) {
    *res_len = data - &res->data[0];
  }
}

// Get SoL Configuration (IPMI/Section 26.3)
static void
transport_get_sol_config (unsigned char *request, unsigned char *response,
        unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  unsigned char param = req->data[1];

  // Fill the response with default values
  res->cc = CC_SUCCESS;
  *data++ = 0x01;   // Parameter revision

  switch (param)
  {
    case SOL_PARAM_SET_IN_PROG:
      *data++ = 0x00;
      break;
    case SOL_PARAM_SOL_ENABLE:
      *data++ = 0x01;
      break;
    case SOL_PARAM_SOL_AUTH:
      *data++ = 0x02;
      break;
    case SOL_PARAM_SOL_THRESHOLD:
      *data++ = 0x00;
      *data++ = 0x01;
      break;
    case SOL_PARAM_SOL_RETRY:
      *data++ = 0x00;
      *data++ = 0x00;
      break;
    case SOL_PARAM_SOL_BITRATE:
    case SOL_PARAM_SOL_NV_BITRATE:
      *data++ = 0x09;
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }

  if (res->cc == CC_SUCCESS) {
    *res_len = data - &res->data[0];
  }
}

// Handle Transport Commands (IPMI/Section 23)
static void
ipmi_handle_transport (unsigned char *request, unsigned char req_len,
           unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_transport);
  switch (cmd)
  {
    case CMD_TRANSPORT_SET_LAN_CONFIG:
      transport_set_lan_config (request, response, res_len);
      break;
    case CMD_TRANSPORT_GET_LAN_CONFIG:
      transport_get_lan_config (request, response, res_len);
      break;
    case CMD_TRANSPORT_GET_SOL_CONFIG:
      transport_get_sol_config (request, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_transport);
}

/*
 * Function(s) to handle IPMI messages with NetFn: Group Extension
 */
static void
ipmi_handle_group_extension(unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  int ret;
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  pthread_mutex_lock(&m_group_extension);
  switch (req->data[0]) {
    case ARM_SBMR:
      ret = pal_handle_arm_sbmr(req->payload_id, &request[1], req_len-1, res->data, res_len);
      if (ret < 0) {
        res->cc = CC_UNSPECIFIED_ERROR;
        *res_len = 0;
        return;
      }
      res->cc = CC_SUCCESS;
      res->data[0] = ARM_SBMR;
      *res_len = 1;
      break;

    case DCMI:
      // Since DCMI handling is specific to platform, call PAL to process
      ret = pal_handle_dcmi(req->payload_id, &request[1], req_len-1, res->data, res_len);
      if (ret < 0) {
        res->cc = CC_UNSPECIFIED_ERROR;
        *res_len = 0;
        return;
      }
      res->cc = CC_SUCCESS;
      break;

    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_group_extension);

}

/*
 * Function(s) to handle IPMI messages with NetFn: OEM
 */
static void
oem_add_ras_sel (unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  *res_len = 0;

  ras_sel_msg_t entry;
  memcpy(entry.msg, req->data, SIZE_RAS_SEL);

  res->cc = ras_sel_add_entry (req->payload_id, &entry);

  return;
}

static void
oem_add_imc_log (unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  *res_len = 0;

  res->cc = pal_add_imc_log (req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_q_sled_cycle_prepare_request (unsigned char *request, unsigned char req_len, unsigned char *response,
      unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[32] = "to_blk_sled_cycle";
  char value[8] = {0};

  if (pal_is_fw_update_ongoing_system()) {
    res->cc = CC_NODE_BUSY;
    return;
  }
  if ( req->data[0] > 1 ) {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    return;
  }
  if ( req_len != 4 ) {
    res->cc = CC_INVALID_LENGTH;
    return;
  }

  sprintf(value, "%02x", req->data[0]);
  if( kv_set(key, value, 0, 0) ) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_q_sled_cycle_prepare_status (unsigned char *request, unsigned char req_len, unsigned char *response,
      unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[32] = "to_blk_sled_cycle";
  char value[8] = {0};

  if ( req_len != 3 ) {
    res->cc = CC_INVALID_LENGTH;
    return;
  }

  if ( kv_get(key, value, 0, 0) != 0 ) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  res->data[0] = (uint8_t)atoi(value);
  *res_len = 1;
  res->cc = CC_SUCCESS;
}

static void
oem_get_power_reading(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  *res_len = 0;
  res->cc = CC_UNSPECIFIED_ERROR;
  res->cc = pal_oem_get_power_reading(req->payload_id, req->data, req_len, res->data, res_len);
}

static void
oem_set_proc_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
  unsigned int index = req->data[0];

  *res_len = 0;
  res->cc = CC_UNSPECIFIED_ERROR;

  sprintf(key, "sys_config/fru%u_cpu%u_basic_info", req->payload_id, index);
  value[0] = 1; // Num cores (Unknown?)
  value[1] = 1; // Thread count (Unknown?)
  value[2] = 0;
  value[3] = req->data[2]; // Frequency
  value[4] = req->data[3];
  value[5] = req->data[1]; // Type stored as revision.
  value[6] = 0;
  if(kv_set(key, value, 7, KV_FPERSIST) != 0) {
    return;
  }
  res->cc = CC_SUCCESS;
}

static void
oem_get_proc_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
  unsigned int index = req->data[0];
  size_t ret;

  *res_len = 0;
  res->cc = CC_UNSPECIFIED_ERROR;

  sprintf(key, "sys_config/fru%u_cpu%u_basic_info", req->payload_id, index);
  if(kv_get(key, value, &ret, KV_FPERSIST) != 0 || ret < 7) {
    return;
  }
  res->data[0] = value[5]; // Return Processor revision as Type
  res->data[1] = value[3]; // Processor Frequency
  res->data[2] = value[4];
  res->data[3] = 0x1;      // Processor Present (0x1, 0xff Not present)
  *res_len = 4;
  res->cc = CC_SUCCESS;
}

static void
oem_set_dimm_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN] = {0};
  char value[MAX_VALUE_LEN] = {0};
  unsigned char index = req->data[0];

  *res_len = 0;
  res->cc = CC_UNSPECIFIED_ERROR;

  sprintf(key, "sys_config/fru%d_dimm%d_type", req->payload_id, index);
  if(kv_set(key, (char *)&req->data[1], 1, KV_FPERSIST)) {
    return;
  }
  sprintf(key, "sys_config/fru%d_dimm%d_speed", req->payload_id, index);
  memcpy(value, &req->data[2], 2);
  memcpy(value + 2, &req->data[4], 4);
  if(kv_set(key, (char *)value, 6, KV_FPERSIST)) {
    return;
  }
  res->cc = CC_SUCCESS;
}

static void
oem_get_dimm_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN] = {0};
  char value[MAX_VALUE_LEN] = {0};
  unsigned char index = req->data[0];
  size_t ret;

  *res_len = 0;
  res->cc = CC_UNSPECIFIED_ERROR;

  sprintf(key, "sys_config/fru%d_dimm%d_type", req->payload_id, index);
  if(kv_get(key, value, &ret, KV_FPERSIST) != 0 || ret < 1) {
    return;
  }
  memcpy(&res->data[0], value, 1); // Type

  sprintf(key, "sys_config/fru%d_dimm%d_speed", req->payload_id, index);
  if(kv_get(key, value, &ret, KV_FPERSIST) != 0 || ret < 6) {
    return;
  }
  memcpy(&res->data[1], value, 2); // speed
  memcpy(&res->data[3], value + 2, 4); // size
  res->data[7] = 0x2; // 0x2 DIMM Present, 0x3 DIMM not Present
  *res_len = 8;
  res->cc = CC_SUCCESS;
}

/*
 * Function(s) to handle IPMI messages with NetFn: OEM 0x36
 */
static void
oem_q_set_proc_info (unsigned char *request, unsigned char req_len, unsigned char *response,
      unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  char payload[100] = {0};
  int numOfParam = sizeof(cpu_info_key)/sizeof(char *);

  if (req_len < 8 || req_len >= sizeof(payload) ||
      (req->data[4] >= numOfParam) ||
      (strlen(cpu_info_key[ (int) req->data[4]]) == 0))

  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  sprintf(key, "sys_config/fru%d_cpu%d_%s", req->payload_id, req->data[3], cpu_info_key[req->data[4]]);

  memcpy(payload, &req->data[5], req_len -8);
  if(kv_set(key, payload, req_len - 8, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_q_get_proc_info (unsigned char *request, unsigned char req_len, unsigned char *response,
      unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  size_t ret = 0;
  int numOfParam = sizeof(cpu_info_key)/sizeof(char *);

  if ((req->data[4] >= numOfParam) ||
      (strlen(cpu_info_key[ (int) req->data[4]]) <= 0) )
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  sprintf(key, "sys_config/fru%d_cpu%d_%s", req->payload_id, req->data[3], cpu_info_key[req->data[4]]);

  if (kv_get(key, (char *)res->data, &ret, KV_FPERSIST) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = (unsigned char)ret;
}

static void
oem_q_set_dimm_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  char payload[100] = {0};
  int numOfParam = sizeof(dimm_info_key)/sizeof(char *);

  if (req_len < 8 || req_len >= sizeof(payload) ||
      (req->data[4] >= numOfParam) ||
      (strlen(dimm_info_key[ (int) req->data[4]]) == 0))
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  sprintf(key, "sys_config/fru%d_dimm%d_%s", req->payload_id, req->data[3], dimm_info_key[req->data[4]]);

  memcpy(payload, &req->data[5], req_len -8);
  if(kv_set(key, payload, req_len - 8, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_q_get_dimm_info (unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  size_t ret = 0;
  int numOfParam = sizeof(dimm_info_key)/sizeof(char *);

  if ((req->data[4] >= numOfParam) ||
      (strlen(dimm_info_key[ (int) req->data[4]]) <= 0) )
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  sprintf(key, "sys_config/fru%d_dimm%d_%s", req->payload_id, req->data[3], dimm_info_key[req->data[4]]);

  if(kv_get(key, (char *)res->data, &ret, KV_FPERSIST) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = (unsigned char)ret;
}

static void
oem_q_set_drive_info(unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0}, cltrType;
  char payload[100] = {0};
  int numOfParam = sizeof(drive_info_key)/sizeof(char *);

  if (req_len < 9 || req_len >= sizeof(payload) ||
      (req->data[5] >= numOfParam) ||
      (strlen(drive_info_key[ (int) req->data[5]]) == 0))
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  if( (req->data[3] & 0x0f) ==0x00)
    cltrType = 'B'; // BIOS
  else if ((req->data[3] & 0x0f) ==0x01)
    cltrType = 'E'; // Expander
  else if  ((req->data[3] & 0x0f) ==0x02)
    cltrType = 'L'; // LSI
  else {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }
  sprintf(key, "sys_config/fru%d_%c_drive%d_%s", req->payload_id, cltrType, req->data[4], drive_info_key[req->data[5]]);

  memcpy(payload, &req->data[6], req_len - 9);
  if(kv_set(key, payload, req_len - 9, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_q_get_drive_info(unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0}, cltrType;
  size_t ret = 0;
  int numOfParam = sizeof(drive_info_key)/sizeof(char *);

  if ((req->data[5] >= numOfParam) ||
      (strlen(drive_info_key[ (int) req->data[5]]) <= 0) )
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  if( (req->data[3] & 0x0f) ==0x00)
    cltrType = 'B'; // BIOS
  else if ((req->data[3] & 0x0f) ==0x01)
    cltrType = 'E'; // Expander
  else if  ((req->data[3] & 0x0f) ==0x02)
    cltrType = 'L'; // LSI
  else {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }
  sprintf(key, "sys_config/fru%d_%c_drive%d_%s", req->payload_id, cltrType, req->data[4], drive_info_key[req->data[5]]);

  if(kv_get(key, (char *)res->data, &ret, KV_FPERSIST) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = (unsigned char)ret;
}

static void
oem_q_set_smu_psp_ver(unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  char payload[100] = {0};

  if (req_len < 8)
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  if( (req->data[4] & 0x0f) == 0x01)
    sprintf(key, "sys_config/fru%d_psp_ver", req->payload_id);
  else if ((req->data[4] & 0x0f) == 0x02)
    sprintf(key, "sys_config/fru%d_smu_ver", req->payload_id);

  memcpy(payload, &req->data[5], req_len - 8);
  if(kv_set(key, payload, req_len - 8, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_q_get_smu_psp_ver(unsigned char *request, unsigned char req_len, unsigned char *response,
       unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  size_t ret = 0;

  if (req_len < 5)
  {
    res->cc = CC_PARAM_OUT_OF_RANGE;
    *res_len = 0;
    return;
  }

  if( (req->data[4] & 0x0f) == 0x01)
    sprintf(key, "sys_config/fru%d_psp_ver", req->payload_id);
  else if ((req->data[4] & 0x0f) == 0x02)
    sprintf(key, "sys_config/fru%d_smu_ver", req->payload_id);

  if(kv_get(key, (char *)res->data, &ret, KV_FPERSIST) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = (unsigned char)ret;
}

static void
oem_set_boot_order(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  int ret;
  int slot_id = req->payload_id;
  static pthread_t bios_timer_tid[MAX_NODES];

  if ( IsTimerStart[req->payload_id - 1] )
  {
#ifdef DEBUG
    syslog(LOG_WARNING, "[%s] Close the previous thread\n", __func__);
#endif
    pthread_cancel(bios_timer_tid[req->payload_id - 1]);
    IsTimerStart[req->payload_id - 1] = false;
  }

  if (req->data[0] & (BIOS_BOOT_VALID_FLAG | CMOS_VALID_FLAG | FORCE_BOOT_BIOS_SETUP_VALID_FLAG)) {
    /*Create timer thread*/
    ret = pthread_create(&bios_timer_tid[req->payload_id - 1], NULL, clear_bios_data_timer, (void *)slot_id);
    if (ret < 0) {
      syslog(LOG_WARNING, "[%s] Create BIOS timer thread failed!\n", __func__);

      res->cc = CC_NODE_BUSY;
      *res_len = 0;
      return;
    }

    IsTimerStart[req->payload_id - 1] = true;
  }

  ret = pal_set_boot_order(req->payload_id, req->data, res->data, res_len);

  if(ret == 0)
  {
    res->cc = CC_SUCCESS;
  }
  else
  {
    res->cc = CC_INVALID_PARAM;
  }
}

static void
oem_get_boot_order(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_get_boot_order(req->payload_id, req->data, res->data, res_len);

#ifdef DEBUG
  syslog(LOG_WARNING, "[%s] Get: %x %x %x %x %x %x\n", __func__, res->data[0], res->data[1], res->data[2], res->data[3], res->data[4], res->data[5]);
#endif
  if(ret == 0)
  {
	res->cc = CC_SUCCESS;
  }
  else
  {
	res->cc = CC_INVALID_PARAM;
  }
}

static void
oem_set_power_limit(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  if ((request == NULL) || (response == NULL) ||(res_len == NULL)) {
    syslog(LOG_WARNING, "%s() Fail to set power limit due to null pointer check", __func__);
    return;
  }

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret = 0;

  ret = pal_set_power_limit(req->payload_id, req->data, res->data, res_len);

  if(ret == 0) {
    res->cc = CC_SUCCESS;
  } else {
    res->cc = CC_INVALID_PARAM;
  }
}

static void
oem_get_power_limit(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  if ((request == NULL) || (response == NULL) ||(res_len == NULL)) {
    syslog(LOG_WARNING, "%s() Fail to get power limit due to null pointer check", __func__);
    return;
  }

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret = 0;

  ret = pal_get_power_limit(req->payload_id, req->data, res->data, res_len);

  if(ret == 0) {
    res->cc = CC_SUCCESS;
  } else {
    res->cc = CC_INVALID_PARAM;
  }
}

static void
oem_set_tpm_presence(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  int ret;
  int slot_id = req->payload_id;
  int presence = req->data[0];

  if (presence == 0) {
    ret = pal_set_tpm_physical_presence(slot_id,presence);
    if (ret == 0) {
      res->cc = CC_SUCCESS;
    } else {
      res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    }
  } else {
    // do not set tpm physical presence flag to 1
    res->cc = CC_UNSPECIFIED_ERROR;
  }
}

static void
oem_get_tpm_presence(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int value;

  value = pal_get_tpm_physical_presence(req->payload_id);
  syslog(LOG_WARNING, "[%s] Get: %x\n", __func__, value);

  if ((value != 0) && (value != 1)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  // BMC auto-clear after BIOS retrieve it
  // if (value == 1) {
  //   int ret = pal_set_tpm_physical_presence(req->payload_id,0);
  //   if (ret != 0) {
  //     res->cc = CC_UNSPECIFIED_ERROR;
  //     *res_len = 0;
  //     return;
  //   }
  // }

  res->cc = CC_SUCCESS;
  res->data[0] = value;
  *res_len = 1;
}

static void
oem_set_ppr (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t selector = req->data[0];
  uint8_t ppr_enable, ppr_type, ppr_action = 0;
  int size = 0, offset;
  char temp[20]= {0};
  char filepath[128]= {0};
  bool append_mode = true;
  FILE *fp = NULL;

  if (access("/mnt/data/ppr/", F_OK) == -1)
     mkdir("/mnt/data/ppr/", 0777);

  *res_len = 0;
  res->cc = CC_SUCCESS;
  switch(selector) {
    case PPR_ACTION:
      ppr_enable = req->data[1] & 0x80;
      ppr_type = req->data[1] & 0x7f;
      if (ppr_enable) {
        if (ppr_type != PPR_SOFT && ppr_type != PPR_HARD  && ppr_type != PPR_TEST_MODE) {
          res->cc = CC_INVALID_DATA_FIELD;
          return;
        }
        ppr_action = req->data[1];
      }

      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_count", req->payload_id);
      fp = fopen(filepath, "r");
      if (!fp) {
        res->cc = CC_NOT_SUPP_IN_CURR_STATE;
        return;
      }
      if (fread(res->data, 1, 1, fp) != 1) {
        fclose(fp);
        res->cc = CC_NOT_SUPP_IN_CURR_STATE;
        return;
      }
      if(res->data[0] == 0) {
        fclose(fp);
        res->cc = CC_NOT_SUPP_IN_CURR_STATE;
        return;
      }
      fclose(fp);
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_action", req->payload_id);
      fp = fopen(filepath, "w");
      if (fp != NULL) {
        fwrite(&ppr_action, sizeof(uint8_t), 1, fp);
      }
      break;
    case PPR_ROW_COUNT:
      if (req->data[1] > PPR_MAX_ROW_COUNT) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      else
      {
        sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_count", req->payload_id);
        fp = fopen(filepath, "w");
        if(fp != NULL)
          fwrite(&req->data[1], sizeof(uint8_t), 1, fp);
      }
      break;
    case PPR_ROW_ADDR:
      if (req->data[1] > PPR_MAX_ROW_COUNT) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      if (req_len != (PPR_ROW_ADDR_DATA_LEN + 4)) {
        res->cc = CC_INVALID_LENGTH;
        return;
      }
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_addr", req->payload_id);
      fp = fopen(filepath, "a+");
      fseek(fp, 0, SEEK_END);
      size = ftell(fp);
      fseek(fp, 0, SEEK_SET);

      append_mode = true;
      offset = 0;
      if(size != 0) {
        while (fread(temp, sizeof(uint8_t), PPR_ROW_ADDR_DATA_LEN, fp)) {
          offset = ftell(fp);
          if(temp[0] == req->data[1]) {
            append_mode = false;
            fclose(fp);
            fp = fopen(filepath, "w+");
            offset = ftell(fp) - PPR_ROW_ADDR_DATA_LEN;
            if (offset < 0)
              offset = 0;
            break;
          }
        }
      }

      if (append_mode && size > PPR_MAX_ROW_COUNT * PPR_ROW_ADDR_DATA_LEN) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        fclose(fp);
        return;
      }
      fseek(fp, offset , SEEK_SET);
      fwrite(&req->data[1], sizeof(uint8_t), PPR_ROW_ADDR_DATA_LEN, fp);
      break;
    case PPR_HISTORY_DATA:
      if (req->data[1] > PPR_MAX_HISTORY_COUNT) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      if (req_len != (PPR_HISTORY_DATA_LEN + 4)) {
        res->cc = CC_INVALID_LENGTH;
        return;
      }
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_history_data", req->payload_id);
      fp = fopen(filepath, "a+");
      fseek(fp, 0, SEEK_END);
      size = ftell(fp);
      fseek(fp, 0, SEEK_SET);


      append_mode = true;
      offset = 0;
      if(size != 0) {
        while (fread(temp, sizeof(uint8_t), PPR_HISTORY_DATA_LEN, fp)) {
          offset = ftell(fp);
          if(temp[0] == req->data[1]) {
            append_mode = false;
            fclose(fp);
            fp = fopen(filepath, "w+");
            offset = ftell(fp) - PPR_HISTORY_DATA_LEN;
            if (offset < 0)
              offset = 0;
            break;
          }
        }
      }

      if (append_mode && size > PPR_MAX_HISTORY_COUNT * PPR_HISTORY_DATA_LEN) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        fclose(fp);
        return;
      }
      fseek(fp, offset , SEEK_SET);
      fwrite(&req->data[1], sizeof(uint8_t), PPR_HISTORY_DATA_LEN, fp);
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }
  if (fp) {
    fclose(fp);
  }
}

static void
oem_get_ppr (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t selector = req->data[0];
  FILE *fp = NULL;
  char filepath[128]= {0};
  char temp[20], kpath[20];

  sprintf(kpath, "%s", "/mnt/data/ppr");
  if (access(kpath, F_OK) == -1)
    mkdir(kpath, 0777);

  *res_len = 0;
  res->cc = CC_SUCCESS;
  switch(selector) {
    case PPR_ACTION:
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_count", req->payload_id);
      fp = fopen(filepath, "r");
      if (!fp) {
        fp = fopen(filepath, "w");
        sprintf(temp, "%c",0);
        fwrite(temp, sizeof(char), 1, fp);
      }
      else {
        if (fread(res->data, 1, 1, fp) != 1) {
          fclose(fp);
          res->cc = CC_UNSPECIFIED_ERROR;
          return;
        }
      }
      fclose(fp);
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_action", req->payload_id);
      fp = fopen(filepath, "r");
      if (!fp) {
        fp = fopen(filepath, "w");
        sprintf(temp, "%c",0);
        fwrite(temp, sizeof(char), 1, fp);
        fclose(fp);
        res->data[0] = 0;
        *res_len = 1;
        return;
      }
      if (res->data[0] != 0 ) {
        if (fread(res->data, 1, 1, fp) != 1) {
          fclose(fp);
          res->cc = CC_UNSPECIFIED_ERROR;
          return;
        }
        if((res->data[0] & 0x80) == 0 )
          res->data[0] = 0;
      }
      *res_len = 1;
      break;
    case PPR_ROW_COUNT:
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_count", req->payload_id);
      fp = fopen(filepath, "r");
      if (!fp) {
        fp = fopen(filepath, "w");
        sprintf(temp, "%c",0);
        fwrite(temp, sizeof(char), 1, fp);
        res->data[0] = 0;
      }
      else {
        if (fread(res->data, 1, 1, fp) != 1) {
          fclose(fp);
          res->cc = CC_UNSPECIFIED_ERROR;
          return;
        }
      }
      *res_len = 1;
      break;
    case PPR_ROW_ADDR:
      if (req->data[1] > PPR_MAX_ROW_COUNT) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_row_addr", req->payload_id);
      fp = fopen(filepath, "r+");
      if (!fp) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      while(fread(res->data, sizeof(uint8_t), PPR_ROW_ADDR_DATA_LEN, fp)) {
        if(res->data[0] == req->data[1]) {
          *res_len = PPR_ROW_ADDR_DATA_LEN;
          break;
        }
      }
      break;
    case PPR_HISTORY_DATA:
      if (req->data[1] > PPR_MAX_HISTORY_COUNT) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      sprintf(filepath, "/mnt/data/ppr/fru%d_ppr_history_data", req->payload_id);
      fp = fopen(filepath, "r+");
      if (!fp) {
        res->cc = CC_PARAM_OUT_OF_RANGE;
        return;
      }
      while(fread(res->data, sizeof(uint8_t), PPR_HISTORY_DATA_LEN, fp)) {
        if(res->data[0] == req->data[1]) {
          *res_len = PPR_HISTORY_DATA_LEN;
          break;
        }
      }
      break;
    default:
      res->cc = CC_INVALID_PARAM;
      break;
  }
  if (fp) {
    fclose(fp);
  }
}

static void
oem_set_post_start (unsigned char *request, unsigned char req_len, unsigned char *response,
                  unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  pal_set_post_start(req->payload_id, req->data, res->data, res_len);

  res->cc = CC_SUCCESS;
}

static void
oem_set_post_end (unsigned char *request, unsigned char req_len, unsigned char *response,
                unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  pal_update_ts_sled();
  pal_set_post_end(req->payload_id, req->data, res->data, res_len);

  res->cc = CC_SUCCESS;
}

static void
oem_set_ppin_info(unsigned char *request, unsigned char req_len,
		   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_set_ppin_info(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_set_adr_trigger(unsigned char *request, unsigned char req_len,
		   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  bool trigger = (bool)req->data[3]; // high/low = true/false
  int ret;

  ret = pal_set_adr_trigger(req->payload_id, trigger);
  if (!ret) {
    res->cc = CC_SUCCESS;
  } else if (ret == PAL_ENOTSUP) {
    // If not supported on this platform act like
    // the command is not implemented.
    res->cc = CC_INVALID_CMD;
  } else {
    res->cc = CC_UNSPECIFIED_ERROR;
  }
  *res_len = 0;
}

static void
oem_get_plat_info(unsigned char *request, unsigned char req_len, unsigned char *response,
                  unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  int ret;
  uint8_t pres  = 0x00;
  uint8_t pinfo = 0x00;

  // Platform info:
  // Bit[7]: Not Present:0/Present:1  (from pal)
  // Bit[6]: Test Board:1, Non Test Board:0 (TODO from pal)
  // Bit[5-3] : SKU ID:000:Yosemite, 010:Triton
  // Bit[2:0] : Slot Index, 1 based
  ret = pal_is_fru_prsnt(req->payload_id, &pres);
  if (ret) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0x00;
    return;
  }

  // Populate the presence bit[7]
  if (pres) {
    pinfo = 0x80;
  }

  // Bit[6]: Test Board:1, Non Test Board:0
  if (pal_is_test_board()) {
    pinfo |= 0x40;
  }

  // Get the SKU ID bit[5-3]
  ret = pal_get_plat_sku_id();
  if (ret == -1){
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0x00;
    return;
  }else{
    pinfo |= (ret << 3);
  }

  // Populate the slot Index bit[2:0]
  ret = pal_get_slot_index(req->payload_id);
  pinfo |= (ret & 0x7);

  // Prepare response buffer
  res->cc = CC_SUCCESS;
  res->data[0] = pinfo;
  *res_len = 0x01;
}

static void
oem_sled_ac_cycle(unsigned char *request, unsigned char req_len,
                  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_sled_ac_cycle(req->payload_id, req->data, (req_len - 3), res->data, res_len);
  if (ret == PAL_ENOTSUP) {
    res->cc = CC_INVALID_CMD;
  } else {
    res->cc = ret;
  }

  *res_len = 0;
  return;
}

static void
oem_get_poss_pcie_config(unsigned char *request, unsigned char req_len,
			  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_get_poss_pcie_config(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_set_imc_version(unsigned char *request, unsigned char req_len,
                    unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_set_imc_version(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_set_fw_update_state(unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret = -1;
  *res_len = 0;
  if (req_len == 5) {
    // Implicit FRU_ID
    ret = pal_set_fw_update_ongoing(req->payload_id, (req->data[1]<<8 | req->data[0]));
  } else if (req_len == 6) {
    // Explicit FRU_ID
    ret = pal_set_fw_update_ongoing(req->data[0], (req->data[1]<<8 | req->data[2]));
  } else {
    res->cc = CC_INVALID_LENGTH;
    return;
  }
  res->cc = ret == 0 ? CC_SUCCESS : CC_UNSPECIFIED_ERROR;
}

static void
oem_bypass_cmd(unsigned char *request, unsigned char req_len,
                          unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_bypass_cmd(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_bypass_dev_card(unsigned char *request, unsigned char req_len,
                          unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_bypass_dev_card(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_get_board_id(unsigned char *request, unsigned char req_len,
					unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_get_board_id(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_get_80port_record ( unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;
  // XXX ipmi_handle() defines res_len as a size_t.
  // The correct change (but a much bigger one) is to replace
  // all `unsigned char *res_len` with `size_t *res_len`.
  ret = pal_get_80port_record (req->payload_id, res->data, 256, (size_t *)res_len);
  switch(ret) {
    case PAL_EOK:
      res->cc = CC_SUCCESS;
      break;
    case PAL_ENOTSUP:
      res->cc = CC_INVALID_PARAM;
      break;
    case PAL_ENOTREADY:
      res->cc = CC_NOT_SUPP_IN_CURR_STATE;
      break;
    default:
      res->cc = CC_UNSPECIFIED_ERROR;
      break;
  }
}

static void
oem_get_fw_info ( unsigned char *request, unsigned char req_len,
                  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  if (req_len != 4) {
    res->cc = CC_INVALID_LENGTH;
    *res_len = 0;
    return;
  }

  ret = pal_get_fw_info(req->payload_id, req->data[0], res->data, res_len);

  if (ret != 0) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0x00;
    return;
  }

  // Prepare response buffer
  res->cc = CC_SUCCESS;
}

static void
oem_set_machine_config_info ( unsigned char *request, unsigned char req_len,
                              unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  pal_set_machine_configuration(req->payload_id, req->data, req_len-3, response, res_len);

  res->cc = CC_SUCCESS;
  *res_len = 0;
}

static void
oem_set_flash_info ( unsigned char *request, unsigned char req_len,
                  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
  char fruname[32];
  uint8_t mfg_id = 0;
  uint16_t dev_id = 0;
  uint16_t sts = 0;

  *res_len = 0x0;
  memcpy(&mfg_id, &req->data[3], 1);
  memcpy(&dev_id, &req->data[4], 2);
  memcpy(&sts, &req->data[6], 2);

  if (pal_get_fru_name(req->payload_id, fruname)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  sprintf(key, "%s_bios_flashinfo", fruname);
  sprintf(value, "%u %u %u", mfg_id, dev_id, sts);

  if (kv_set(key, value, 0, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }
  res->cc = CC_SUCCESS;
}

static void
oem_get_flash_info ( unsigned char *request, unsigned char req_len,
                  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[MAX_KEY_LEN];
  char value[MAX_VALUE_LEN];
  char fruname[32];
  unsigned int mfg_id = 0;
  unsigned int dev_id = 0;
  unsigned int sts = 0;

  *res_len = 0;

  if (pal_get_fru_name(req->payload_id, fruname)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  sprintf(key, "%s_bios_flashinfo", fruname);
  if (kv_get(key, value, NULL, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }
  if (sscanf(value, "%u %u %u", &mfg_id, &dev_id, &sts) != 3) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  // Prepare response buffer
  memcpy(&res->data[0], &mfg_id, 1);
  (*res_len)++;
  memcpy(&res->data[1], &dev_id, 2);
  (*res_len) += 2;
  memcpy(&res->data[3], &sts, 2);
  (*res_len) += 2;
  res->cc = CC_SUCCESS;
}

static void
oem_get_pcie_port_config(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_get_pcie_port_config(req->payload_id, req->data, req_len, res->data, res_len);

#ifdef DEBUG
  syslog(LOG_WARNING, "[%s] Get: %x %x\n", __func__, res->data[0], res->data[1]);
#endif
  if(ret == 0) {
    res->cc = CC_SUCCESS;
  } else {
    res->cc = CC_UNSPECIFIED_ERROR;
  }
}

static void
oem_set_pcie_port_config(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_set_pcie_port_config(req->payload_id, req->data, req_len, res->data, res_len);

  if(ret == 0) {
    res->cc = CC_SUCCESS;
  } else {
    res->cc = CC_UNSPECIFIED_ERROR;
  }
}

static void
oem_add_cper_log(unsigned char *request, unsigned char req_len,
            unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  *res_len = 0;

  res->cc = pal_add_cper_log(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_set_psb_info(unsigned char *request, unsigned char req_len,
            unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  *res_len = 0;

  res->cc = pal_set_psb_info(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
oem_set_m2_info (unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  char payload[100] = {0};
  unsigned char cmd_len = 8;

  if (length_check(cmd_len, req_len, response, res_len))
    return;

  sprintf(key, "sys_config/fru%d_m2_%d_info", req->payload_id, req->data[0]);

  memcpy(payload, &req->data[1], req_len - 4);
  if(kv_set(key, payload, req_len - 4, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;

  return;
}

static void
oem_get_80port_dword_record (unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  *res_len = 0;
  ret = pal_get_80port_page_record (req->payload_id, req->data[0], res->data, 256, (size_t *)res_len);
  switch(ret) {
    case PAL_EOK:
      res->cc = CC_SUCCESS;
      break;
    case PAL_ENOTSUP:
      res->cc = CC_INVALID_CMD;
      break;
    case PAL_ENOTREADY:
      res->cc = CC_NOT_SUPP_IN_CURR_STATE;
      break;
    default:
      res->cc = CC_UNSPECIFIED_ERROR;
      break;
  }
}

static void
oem_get_dev_card_sensor(unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_get_dev_card_sensor(req->payload_id, req->data, req_len, res->data, res_len);
}

static void
oem_get_sensor_real_reading(unsigned char *request, unsigned char req_len, unsigned char *response,
                 unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t fru, snr_num;
  uint8_t ret = 0;
  float val = 0;
  bool cached = false;

  if (req_len != 5 && req_len != 6) {
    res->cc = CC_INVALID_LENGTH;
    *res_len = 0;
    return;
  }
  if (req_len == 6 && req->data[2] != 0x00) {
    cached = true;
  }
  fru = req->data[0];
  snr_num = req->data[1];
  if (cached) {
    ret = sensor_cache_read(fru, snr_num, &val);
  } else {
    ret = pal_sensor_read_raw(fru, snr_num, &val);
  }

  if (ret == 0) {
    res->cc = CC_SUCCESS;
    *res_len = 3;
    res->data[0] = ((int)val) >> 8;
    res->data[1] = (int)val & 0xFF;
    res->data[2] = (((int)(val*100)) % 100);
  } else {
    *res_len = 0;
    res->cc = CC_UNSPECIFIED_ERROR;
  }
}

static void
oem_bbv_power_cycle ( unsigned char *request, unsigned char req_len,
                  unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int delay_time;
  *res_len = 0;

  delay_time = req->data[0];
  res->cc = bbv_power_cycle(delay_time);
}

static void
oem_stor_add_string_sel(unsigned char *request, unsigned char req_len,
                        unsigned char *response, unsigned char *res_len)
{
  // Byte0        Reserved
  // Byte1        String length
  // Byte2~ByteN  Event log string (ASCII)
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  char string_log[248] = {0};
  //Reserve byte[0:3] bytes for future implementation
  uint8_t string_log_len = req->data[4];
  *res_len = 0;

  if (string_log_len >= sizeof(string_log)){
    res->cc = CC_INVALID_LENGTH;
    syslog(LOG_ERR, "%s(): max supported string length is %d, but got %d",
                       __func__, sizeof(string_log)-1, string_log_len);
    return;
  }

  memcpy(string_log, &req->data[5], string_log_len+1);

  // To avoid repeat display when Expander executes reset test
  // will filter fan fru checksum SEL
  if (!pal_handle_fan_fru_checksum_sel(string_log, string_log_len)) {
    res->cc = CC_SUCCESS;
    return;
  }

  syslog(LOG_CRIT, "%s", string_log);

  if (!pal_handle_string_sel(string_log, string_log_len))
    res->cc = CC_SUCCESS;
  else
    res->cc = CC_UNSPECIFIED_ERROR;

  return;
}

static void
oem_set_bios_cap_fw_ver(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_set_bios_cap_fw_ver(req->payload_id, req->data, req_len, res->data, res_len);

  if(ret == 0) {
    res->cc = CC_SUCCESS;
  } else {
    res->cc = CC_UNSPECIFIED_ERROR;
  }
}

static void
oem_set_fscd(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char data = req->data[0];
  char cmd[100] = {0};

  switch (data)
  {
    case 0x00:
      if (access("/usr/bin/systemctl", X_OK) == 0) {
        sprintf(cmd, "systemctl stop fscd.service");
      } else {
        sprintf(cmd, "sv force-stop fscd");
      }
      break;
    case 0x01:
      if (access("/usr/bin/systemctl", X_OK) == 0) {
        sprintf(cmd, "systemctl start fscd.service");
      } else {
        sprintf(cmd, "sv start fscd");
      }
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }

  if (system(cmd)) {
    syslog(LOG_WARNING, "set fscd cmd failed (%s)\n", cmd);
    res->cc = CC_UNSPECIFIED_ERROR;
  } else {
    res->cc = CC_SUCCESS;
  }
}

static void
oem_set_slot_power_policy(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  *data++ = 0x07;  // Power restore policy support(bitfield)

  // Set specific slave addr device's power restore policy
  res->cc = pal_set_slot_power_policy(req->data, res->data);
  if (res->cc == CC_SUCCESS) {
    *res_len = data - &res->data[0];
  }
}

static void
oem_get_usb_cdc_status (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res= (ipmi_res_t *) response;
  int ret = 0;

  if (length_check(0, req_len, response, res_len)) {
    return;
  }

  ret = system("lsmod | grep -q g_cdc");
  if (ret != 0) {
    res->data[0] = DISABLE_USB_CDC;
  } else {
    res->data[0] = ENABLE_USB_CDC;
  }

  res->cc = CC_SUCCESS;
  *res_len = 1;
}

static void
oem_control_usb_cdc (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  uint8_t status_ctrl = req->data[0]; // 0 - disable, 1 - enable
  int ret = 0;

  if (length_check(1, req_len, response, res_len)) {
    return;
  }

  res->cc = CC_SUCCESS;

  if (status_ctrl == DISABLE_USB_CDC) {
    // Disable USB serial console by usbcons.sh, then unload USB CDC driver
    ret = run_command("/etc/init.d/usbcons.sh stop");
    if (ret == 0) {
      ret = system("modprobe -qr g_cdc");
      if (WIFEXITED(ret) == 0) {
        syslog(LOG_ERR, "%s: failed to disable ether-over-usb (usb0), ret: %d\n", __func__, ret);
        res->cc = CC_UNSPECIFIED_ERROR;
      }
    } else {
      syslog(LOG_ERR, "%s: failed to disable USB serial console, ret: %d\n", __func__, ret);
      res->cc = CC_UNSPECIFIED_ERROR;
    }
  } else if (status_ctrl == ENABLE_USB_CDC){
    // Enable USB serial console and reload USB CDC driver by usbcons.sh, then link up usb0
    // The 'restart' action will kill all usbmod.sh processes then start usbmon.sh
    ret = run_command("/etc/init.d/usbcons.sh restart");
    if (ret == 0) {
      ret = system("sleep 1; ifconfig usb0 up");
      if (WIFEXITED(ret) == 0) {
        syslog(LOG_ERR, "%s: failed to link up ether-over-usb (usb0), ret: %d\n", __func__, ret);
        res->cc = CC_UNSPECIFIED_ERROR;
      }
    } else {
      syslog(LOG_ERR, "%s: failed to enable ether-over-usb (usb0), ret: %d\n", __func__, ret);
      res->cc = CC_UNSPECIFIED_ERROR;
    }
  } else {
    res->cc = CC_INVALID_PARAM;
  }

  *res_len = 0;
}

static void
oem_stor_set_dam_pin_control (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  *res_len = 0;

  if (length_check(1, req_len, response, res_len) != 0) {
    return;
  }

  if (pal_set_dam_pin_status(req->payload_id, req->data[0]) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
  } else {
    res->cc = CC_SUCCESS;
  }
}

static void
oem_stor_get_remote_jumper_status (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  uint8_t dam_pin_status = BIOS_DAM_PIN_DISABLE;
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  ipmi_get_remote_jumper_status_res_t resp = {0};
  *res_len = 0;

  if (length_check(0, req_len, response, res_len) != 0) {
    return;
  }

  if (pal_get_dam_pin_status(req->payload_id, &dam_pin_status) < 0) {
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    return;
  }

  // Always respond remote validation jumper is enable.
  resp.remote_val_jumper_status = BIOS_REMOTE_VAL_JUMPER_ENABLE;
  resp.dam_pin_status = dam_pin_status;
  memcpy(res->data, &resp, sizeof(resp));
  *res_len = sizeof(resp);

  res->cc = CC_SUCCESS;
}

static void
oem_set_ioc_fw_recovery (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  *res_len = 0;

  if (length_check(2, req_len, response, res_len) != 0) {
    return;
  }

  // cmd_len = req_len - 3 (payload_id, cmd and netfn)
  res->cc = pal_set_ioc_fw_recovery(req->data, (req_len - IPMI_MN_REQ_HDR_SIZE), res->data, res_len);
}

static void
oem_get_ioc_fw_recovery (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res= (ipmi_res_t *) response;
  uint8_t component = 0;
  *res_len = 0;

  if (length_check(1, req_len, response, res_len) != 0) {
    return;
  }

  component = req->data[0];

  res->cc = pal_get_ioc_fw_recovery(component, res->data, res_len);
}

static void
oem_setup_exp_uart_bridging (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  *res_len = 0;

  if (length_check(0, req_len, response, res_len) != 0) {
    return;
  }

  res->cc = pal_setup_exp_uart_bridging();
}

static void
oem_teardown_exp_uart_bridging (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_res_t *res = (ipmi_res_t *) response;
  *res_len = 0;

  if (length_check(0, req_len, response, res_len) != 0) {
    return;
  }

  res->cc = pal_teardown_exp_uart_bridging();
}

static void
oem_set_ioc_wwid (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  *res_len = 0;

  if (length_check(9, req_len, response, res_len) != 0) {
    return;
  }

  res->cc = pal_set_ioc_wwid(req->data, (req_len - IPMI_MN_REQ_HDR_SIZE), res->data, res_len);
}

static void
oem_get_ioc_wwid (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t component = 0;
  *res_len = 0;

  if (length_check(1, req_len, response, res_len) != 0) {
    return;
  }

  component = req->data[0];

  res->cc = pal_get_ioc_wwid(component, res->data, res_len);
}

static void
oem_set_pcie_info (unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  char key[100] = {0};
  char payload[100] = {0};
  size_t data_len = req_len - IPMI_MN_REQ_HDR_SIZE;

  sprintf(key, "sys_config/fru%d_pcie_i%02X_s%02X_info",
      req->payload_id, req->data[0], req->data[1]);

  memcpy(payload, &req->data[0], data_len);
  if(kv_set(key, payload, data_len, KV_FPERSIST)) {
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = 0;
    return;
  }

  res->cc = CC_SUCCESS;
  *res_len = 0;

  return;
}

static void
oem_bios_extra_setup(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_oem_bios_extra_setup(req->payload_id, req->data, req_len, res->data, res_len);
}

static void
init_host_directory()
{
  char path[] = "/mnt/data/host";
  if (access(path, F_OK) == -1) {
    mkdir(path, 0777);
  }
}

static void
oem_get_https_certificate_data(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int ret;
  int offset = req->data[0] + (req->data[1] << 8);
  int count = req->data[2];

  int fd = open(HTTPS_BOOT_CERT_PATH, O_RDONLY);
  if (fd < 0) {
    res->cc = CC_UNSPECIFIED_ERROR;
    return;
  }

  do {
    // seek position based on given offset
    ret = lseek(fd, offset, SEEK_SET);
    if (ret < 0) {
      res->cc = CC_UNSPECIFIED_ERROR;
      break;
    }

    // read the file content
    ret = read(fd, data+1, count);
    if (ret <0) {
      res->cc = CC_UNSPECIFIED_ERROR;
      break;
    }
    data[0] = ret; // Count returned
    *res_len =  data[0] +1;
    res->cc = CC_SUCCESS;
  } while (0);

  close(fd);
  return;
}

static void
oem_get_https_certificate_attr(unsigned char *request, unsigned char req_len,
                   unsigned char *response, unsigned char *res_len)
{
  enum attr_type{
    ROOTCERT_SIZE,
    ROOTCERT_CRC,
  };
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char *data = &res->data[0];
  int type = req->data[0];

  int fd = open(HTTPS_BOOT_CERT_PATH, O_RDONLY);

  switch (type) {
    case ROOTCERT_SIZE:
    {
      int size = 0;
      struct stat st;
      if (fd >= 0 && fstat(fd, &st) == 0) {
        size = st.st_size;
      }
      data[0] = size & 0xFF; // size LSB
      data[1] = (size >> 8) & 0xFF; //size MSB
      *res_len = 2;
      res->cc = CC_SUCCESS;
      break;
    }
    case ROOTCERT_CRC:
    {
      unsigned char buf[1024];
      uint32_t checksum = crc32(0, Z_NULL, 0);
      int rc;
      if (fd >= 0) {
        do {
          rc = read(fd, buf, 1024);
          if (rc > 0) {
            checksum = crc32(checksum, buf, rc);
          }
        } while (rc > 0);
      }

      memcpy(data, &checksum, 4);
      *res_len = 4;
      res->cc = CC_SUCCESS;
      break;
    }
    default:
      res->cc = CC_UNSPECIFIED_ERROR;
  }
  if (fd) {
    close(fd);
  }
  return;
}

static void
ipmi_handle_oem (unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  unsigned char cmd = req->cmd;
  pthread_mutex_lock(&m_oem);
  switch (cmd)
  {
    case CMD_OEM_ADD_RAS_SEL:
      oem_add_ras_sel (request, req_len, response, res_len);
      break;
    case CMD_OEM_ADD_IMC_LOG:
      oem_add_imc_log (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PROC_INFO:
      oem_set_proc_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_PROC_INFO:
      oem_get_proc_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_DIMM_INFO:
      oem_set_dimm_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_DIMM_INFO:
      oem_get_dimm_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_BOOT_ORDER:
      if(length_check(SIZE_BOOT_ORDER, req_len, response, res_len)) {
        break;
      }
      oem_set_boot_order(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_BOOT_ORDER:
      oem_get_boot_order(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_POWER_LIMIT:
      if(length_check(SIZE_CPU_POWER_LIMIT_DATA, req_len, response, res_len)) {
        break;
      }
      oem_set_power_limit(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_POWER_LIMIT:
      if(length_check(0, req_len, response, res_len)) {
        break;
      }
      oem_get_power_limit(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_TPM_PRESENCE:
      oem_set_tpm_presence(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_TPM_PRESENCE:
      oem_get_tpm_presence(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PPR:
    case CMD_OEM_LEGACY_SET_PPR:
      oem_set_ppr (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_PPR:
    case CMD_OEM_LEGACY_GET_PPR:
      oem_get_ppr (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_POST_START:
      oem_set_post_start (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_POST_END:
      oem_set_post_end (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PPIN_INFO:
      oem_set_ppin_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_ADR_TRIGGER:
      oem_set_adr_trigger(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_PLAT_INFO:
      oem_get_plat_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_SYSTEM_GUID:
      oem_set_device_sys_guid (request, req_len, response, res_len);
      break;
    case CMD_OEM_SLED_AC_CYCLE:
      oem_sled_ac_cycle (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_PCIE_CONFIG:
      oem_get_poss_pcie_config (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_IMC_VERSION:
      oem_set_imc_version (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_FW_UPDATE_STATE:
      oem_set_fw_update_state (request, req_len, response, res_len);
      break;
    case CMD_OEM_BYPASS_CMD:
      oem_bypass_cmd (request, req_len, response, res_len);
      break;
    case CMD_OEM_BYPASS_DEV_CARD:
      oem_bypass_dev_card (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_BOARD_ID:
      oem_get_board_id (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_80PORT_RECORD:
      oem_get_80port_record (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_FW_INFO:
      oem_get_fw_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_MACHINE_CONFIG_INFO:
      oem_set_machine_config_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_BIOS_FLASH_INFO:
      oem_set_flash_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_BIOS_FLASH_INFO:
      oem_get_flash_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_PCIE_PORT_CONFIG:
      if(length_check(0, req_len, response, res_len))
        break;
      oem_get_pcie_port_config(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PCIE_PORT_CONFIG:
      if(length_check(SIZE_PCIE_PORT_CONFIG, req_len, response, res_len))
        break;
      oem_set_pcie_port_config(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_POWER_READING:
      if(length_check(SIZE_GET_PWR_READING, req_len, response, res_len))
        break;
      oem_get_power_reading(request, req_len, response, res_len);
      break;
    case CMD_OEM_BBV_POWER_CYCLE:
      oem_bbv_power_cycle(request, req_len, response, res_len);
      break;
    case CMD_OEM_ADD_CPER_LOG:
      oem_add_cper_log(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PSB_INFO:
      oem_set_psb_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_M2_INFO:
      oem_set_m2_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_80_PORT_DWORD_BUFFER:
      oem_get_80port_dword_record (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_DEV_CARD_SENSOR:
      oem_get_dev_card_sensor(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_SENSOR_REAL_READING:
      oem_get_sensor_real_reading(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_BIOS_CAP_FW_VER:
      oem_set_bios_cap_fw_ver(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_FSCD:
      oem_set_fscd(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_POWER_POLICY:
      oem_set_slot_power_policy(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_USB_CDC_STATUS:
      oem_get_usb_cdc_status(request, req_len, response, res_len);
      break;
    case CMD_OEM_CTRL_USB_CDC:
      oem_control_usb_cdc(request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_PCIE_INFO:
      oem_set_pcie_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_BIOS_EXTRA_SETUP:
      oem_bios_extra_setup(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_HTTPS_BOOT_CERT_DATA:
      if(length_check(3, req_len, response, res_len)) {
        break;
      }
      oem_get_https_certificate_data(request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_HTTPS_BOOT_CERT_ATTR:
      if(length_check(1, req_len, response, res_len)) {
        break;
      }
      oem_get_https_certificate_attr(request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_oem);
}

static void
ipmi_handle_oem_storage (unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_oem_storage);
  switch (cmd)
  {
    case CMD_OEM_STOR_ADD_STRING_SEL:
      oem_stor_add_string_sel (request, req_len, response, res_len);
      break;
    case CMD_OEM_STOR_SET_DAM_PIN_CONTROL:
      oem_stor_set_dam_pin_control (request, req_len, response, res_len);
      break;
    case CMD_OEM_STOR_GET_REMOTE_JUMPER_STATUS:
      oem_stor_get_remote_jumper_status (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_IOC_FW_RECOVERY:
      oem_set_ioc_fw_recovery (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_IOC_FW_RECOVERY:
      oem_get_ioc_fw_recovery (request, req_len, response, res_len);
      break;
    case CMD_OEM_SETUP_EXP_UART_BRIDGING:
      oem_setup_exp_uart_bridging (request, req_len, response, res_len);
      break;
    case CMD_OEM_TEARDOWN_EXP_UART_BRIDGING:
      oem_teardown_exp_uart_bridging (request, req_len, response, res_len);
      break;
    case CMD_OEM_SET_IOC_WWID:
      oem_set_ioc_wwid (request, req_len, response, res_len);
      break;
    case CMD_OEM_GET_IOC_WWID:
      oem_get_ioc_wwid (request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_oem_storage);
}

static void
ipmi_handle_oem_q (unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  unsigned char cmd = req->cmd;
  pthread_mutex_lock(&m_oem_q);
  switch (cmd)
  {
    case CMD_OEM_Q_SET_PROC_INFO:
      oem_q_set_proc_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_GET_PROC_INFO:
      oem_q_get_proc_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_SET_DIMM_INFO:
      oem_q_set_dimm_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_GET_DIMM_INFO:
      oem_q_get_dimm_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_SET_DRIVE_INFO:
      oem_q_set_drive_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_GET_DRIVE_INFO:
      oem_q_get_drive_info (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_SET_SMU_PSP_VER:
      oem_q_set_smu_psp_ver (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_GET_SMU_PSP_VER:
      oem_q_get_smu_psp_ver (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_SLED_CYCLE_PREPARE_REQUEST:
      oem_q_sled_cycle_prepare_request (request, req_len, response, res_len);
      break;
    case CMD_OEM_Q_SLED_CYCLE_PREPARE_STATUS:
      oem_q_sled_cycle_prepare_status (request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      break;
  }
  pthread_mutex_unlock(&m_oem_q);
}

static void
oem_1s_handle_ipmb_kcs(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  // Need to extract bridged IPMI command and handle
  unsigned char req_buf[MAX_IPMI_MSG_SIZE] = {0};
  unsigned char res_buf[MAX_IPMI_MSG_SIZE] = {0};

  // Add the payload id from the bridged command
  req_buf[0] = req->payload_id;

  // Remove OEM IPMI Header (including 1 byte for interface type, 3 bytes for IANA ID)
  // The offset moves by one due to the payload ID
  memcpy(&req_buf[1], &request[BIC_INTF_HDR_SIZE], req_len - BIC_INTF_HDR_SIZE);

  // Send the bridged KCS command along with the payload ID
  // The offset moves by one due to the payload ID
  ipmi_handle(req_buf, req_len - BIC_INTF_HDR_SIZE + 1, res_buf, res_len);

  // Copy the response back (1 byte interface type, 3 bytes for IANA ID)
  memcpy(&res->data[4], res_buf, *res_len);

  // Add the OEM command's response
  res->cc = CC_SUCCESS;
  memcpy(res->data, &req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = req->data[3]; // Bridge-IC interface
  *res_len += 4; // Interface type + IANA ID
}

static void
oem_1s_handle_ipmb_req(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  // handle based on Bridge-IC interface
  switch(req->data[3]) {
    case BIC_INTF_ME:
#ifdef DEBUG
      syslog(LOG_INFO, "oem_1s_handle_ipmb_req: Command received from ME for "
                  "payload#%d\n", req->payload_id);
#endif
      oem_1s_handle_ipmb_kcs(request, req_len, response, res_len);

      break;
    case BIC_INTF_SOL:
      // TODO: Need to call Optional SoL message handler
#ifdef DEBUG
      syslog(LOG_INFO, "oem_1s_handle_ipmb_req: Command received from SOL for "
                  "payload#%d\n", req->payload_id);
#endif
      memcpy(res->data, req->data, 4); //IANA ID + Interface type
      res->cc = CC_SUCCESS;
      *res_len = 4;
      break;
    case BIC_INTF_KCS:
    case BIC_INTF_KCS_SMM:
    case BIC_INTF_SSIF:
    case BIC_INTF_IMC:
      oem_1s_handle_ipmb_kcs(request, req_len, response, res_len);
      break;
    default:
      // TODO: Need to add additonal interface handler, if supported
      syslog(LOG_WARNING, "oem_1s_handle_ipmb_req: Command received on intf#%d "
                 "for payload#%d", req->data[3], req->payload_id);
      memcpy(res->data, req->data, 4); //IANA ID + Interface type
      res->cc = CC_INVALID_PARAM;
      *res_len = 4;
      break;
  }
}

static void
ipmi_handle_oem_1s(unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int i;

  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_oem_1s);
  switch (cmd)
  {
    case CMD_OEM_1S_MSG_IN:
      // As all bridge in messages are IPMI request
      // all IPMI request will be process by ipmi_handle
      // which will "properly" serialize the processing according to netfn
      // Thus it is not necessary to serialize processing of MSG-IN.
      pthread_mutex_unlock(&m_oem_1s);
      oem_1s_handle_ipmb_req(request, req_len, response, res_len);
      pthread_mutex_lock(&m_oem_1s);
      break;
    case CMD_OEM_1S_INTR:
#ifdef DEBUG
      syslog(LOG_INFO, "ipmi_handle_oem_1s (cmd 0x%02x): 1S server interrupt#%d received "
                "for payload#%d\n", cmd, req->data[3], req->payload_id);
#endif
      pal_handle_oem_1s_intr(req->payload_id, &(req->data[3]));

      res->cc = CC_SUCCESS;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_POST_BUF:
      // Skip the first 3 bytes of IANA ID and one byte of length field
      for (i = SIZE_IANA_ID+1; i <= req->data[SIZE_IANA_ID]+SIZE_IANA_ID; i++) {
        pal_post_handle(req->payload_id, req->data[i]);
      }

      res->cc = CC_SUCCESS;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_PLAT_DISC:
      syslog(LOG_INFO, "ipmi_handle_oem_1s: Platform Discovery received for "
                "payload#%d\n", req->payload_id);
      res->cc = CC_SUCCESS;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_BIC_RESET:
      syslog(LOG_INFO, "ipmi_handle_oem_1s: BIC Reset received "
                "for payload#%d\n", req->payload_id);

      if (req->data[3] == 0x0) {
         syslog(LOG_WARNING, "Cold Reset by Firmware Update\n");
         res->cc = CC_SUCCESS;
      } else if (req->data[3] == 0x01) {
         syslog(LOG_WARNING, "WDT Reset\n");
         res->cc = CC_SUCCESS;
      } else {
         syslog(LOG_WARNING, "Error\n");
         res->cc = CC_INVALID_PARAM;
      }

      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_BIC_UPDATE_MODE:
#ifdef DEBUG
      syslog(LOG_INFO, "ipmi_handle_oem_1s: BIC Update Mode received "
                "for payload#%d\n", req->payload_id);
#endif
      if (req->data[3] == 0x1) {
         syslog(LOG_INFO, "BIC Mode: Normal\n");
         res->cc = CC_SUCCESS;
      } else if (req->data[3] == 0x0F) {
         syslog(LOG_INFO, "BIC Mode: Update\n");
         res->cc = CC_SUCCESS;
      } else {
         syslog(LOG_WARNING, "Error\n");
         res->cc = CC_INVALID_PARAM;
      }

      pal_inform_bic_mode(req->payload_id, req->data[3]);

      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_ASD_MSG_IN:
      if (req_len > 7) {  // payload_id, netfn, cmd, IANA[3], data type
        pal_handle_oem_1s_asd_msg_in(req->payload_id, &req->data[3], req_len-6);
        res->cc = CC_SUCCESS;
      } else {
        res->cc = CC_INVALID_LENGTH;
      }
      memcpy(res->data, req->data, SIZE_IANA_ID);
      *res_len = 3;
      break;
    case CMD_OEM_1S_RAS_DUMP_IN:
      if (req_len > 7) {  // payload_id, netfn, cmd, IANA[3], data type
        pal_handle_oem_1s_ras_dump_in(req->payload_id, &req->data[3], req_len-7);
        res->cc = CC_SUCCESS;
      } else {
        res->cc = CC_INVALID_LENGTH;
      }
      memcpy(res->data, req->data, SIZE_IANA_ID);
      *res_len = 3;
      break;
    case CMD_OEM_1S_4BYTE_POST_BUF:
      // Skip the first 3 bytes of IANA ID and one byte of length field
      for(int k = SIZE_IANA_ID + 1; k < req->data[SIZE_IANA_ID]+SIZE_IANA_ID; k+=(sizeof(uint32_t)/sizeof(uint8_t)))
      {
        uint32_t port_buff = req->data[k] | (req->data[k+1] << 8) | (req->data[k+2] << 16) | (req->data[k+3] << 24);
        pal_display_4byte_post_code(req->payload_id, port_buff);
      }

      res->cc = CC_SUCCESS;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
    case CMD_OEM_1S_GET_SYS_FW_VER:
      if (req_len == 8) { // payload_id, netfn, cmd, IANA[3], data[0] (fru), data[1] (component)
        memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
        res->cc = pal_get_fw_ver(req->payload_id, &req->data[3], &res->data[3], res_len);
        *res_len += SIZE_IANA_ID;
      } else {
        res->cc = CC_INVALID_LENGTH;
        *res_len = SIZE_IANA_ID;
      }
      break;
    case CMD_OEM_1S_DEV_POWER:
      // payload_id, netfn, cmd, data[0] (device id), data[1] (action), data[2] (data)
      res->cc = pal_handle_oem_1s_dev_power(req->payload_id, &req->data[0], req_len-3, &res->data[0], res_len);
      break;
    case CMD_OEM_1S_UPDATE_SDR:
      res->cc = pal_handle_oem_1s_update_sdr(req->payload_id);
      break;
    case CMD_OEM_1S_SET_DELAY_ACTIVATE_SYSFW:
      if (req_len > 7) {  // payload_id, netfn, cmd, IANA[3], data type
        pal_set_delay_activate_sysfw_ver(req->payload_id, &req->data[SIZE_IANA_ID]);
        res->cc = CC_SUCCESS;
      } else {
        res->cc = CC_INVALID_LENGTH;
      }
      break;
    case CMD_OEM_1S_SEND_APML_ALERT_TO_BMC:
      syslog(LOG_WARNING, "Get APML alert from fru %d, RAS status %x\n", req->payload_id, req->data[4]);
      res->cc = pal_apml_alert_handler(req->payload_id, &req->data[0], req_len, &res->data[0], res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
  }
  pthread_mutex_unlock(&m_oem_1s);
}

static void
oem_usb_dbg_get_frame_info(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t num_frames;
  int ret;

  ret = plat_udbg_get_frame_info();
  if (ret < 0) {
    memcpy(res->data, req->data, 3); // IANA ID
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = SIZE_IANA_ID;
    return;
  }
  num_frames = (uint8_t) ret;

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = num_frames;
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + 1;
}

static void
oem_usb_dbg_get_updated_frames(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t num_updates;
  int ret;

  ret = plat_udbg_get_updated_frames(&num_updates, &res->data[4]);
  if (ret) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = SIZE_IANA_ID;
    return;
  }

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = num_updates;
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + 1 + num_updates;
}

static void
oem_usb_dbg_get_post_desc(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t index;
  uint8_t next;
  uint8_t end;
  uint8_t phase;
  uint8_t count;
  int ret;

  index = req->data[3];
  phase = req->data[4];

  ret = plat_udbg_get_post_desc(index, &next, phase, &end, &count, &res->data[8]);
  if (ret) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = SIZE_IANA_ID;
    return;
  }

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = index;
  res->data[4] = next;
  res->data[5] = phase;
  res->data[6] = end;
  res->data[7] = count;
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + 5 + count;
}

static void
oem_usb_dbg_get_gpio_desc(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t index;
  uint8_t next;
  uint8_t level;
  uint8_t in_out;
  uint8_t count;
  int ret;

  index = req->data[3];

  ret = plat_udbg_get_gpio_desc(index, &next, &level, &in_out, &count, &res->data[8]);
  if (ret) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = SIZE_IANA_ID;
    return;
  }

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = index;
  res->data[4] = next;
  res->data[5] = level;
  res->data[6] = in_out;
  res->data[7] = count;
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + 5 + count;
}

static void
oem_usb_dbg_get_frame_data(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t frame;
  uint8_t page;
  uint8_t next;
  uint8_t count;
  int ret;

  frame = req->data[3];
  page = req->data[4];

  ret = plat_udbg_get_frame_data(frame, page, &next, &count, &res->data[7]);
  if (ret) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    res->cc = CC_UNSPECIFIED_ERROR;
    *res_len = SIZE_IANA_ID;
    return;
  }

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->data[3] = frame;
  res->data[4] = page;
  res->data[5] = next;
  res->data[6] = count;
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + 4 + count;
}

static void
oem_usb_dbg_control_panel(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  uint8_t panel;
  uint8_t operation;
  uint8_t item;
  uint8_t count;
  int ret;

  panel = req->data[3];
  operation = req->data[4];
  item = req->data[5];

  ret = plat_udbg_control_panel(panel, operation, item, &count, &res->data[3]);
  if (ret) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    res->cc = ret;
    *res_len = SIZE_IANA_ID;
    return;
  }

  memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
  res->cc = CC_SUCCESS;
  *res_len = SIZE_IANA_ID + count;
}

static void
ipmi_handle_oem_usb_dbg(unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char cmd = req->cmd;
  char key[32] = "blk_ocp_cmd";
  char value[8] = {0};

  if ( kv_get(key, value, 0, 0) == 0 && atoi(value) != 0) {
    syslog(LOG_INFO, "lock cmd");
    res->cc = CC_NOT_SUPP_IN_CURR_STATE;
    *res_len = 0;
    return;
  }

  pthread_mutex_lock(&m_oem_usb_dbg);
  switch (cmd)
  {
    case CMD_OEM_USB_DBG_GET_FRAME_INFO:
      oem_usb_dbg_get_frame_info(request, req_len, response, res_len);
      break;
    case CMD_OEM_USB_DBG_GET_UPDATED_FRAMES:
      oem_usb_dbg_get_updated_frames(request, req_len, response, res_len);
      break;
    case CMD_OEM_USB_DBG_GET_POST_DESC:
      oem_usb_dbg_get_post_desc(request, req_len, response, res_len);
      break;
    case CMD_OEM_USB_DBG_GET_GPIO_DESC:
      oem_usb_dbg_get_gpio_desc(request, req_len, response, res_len);
      break;
    case CMD_OEM_USB_DBG_GET_FRAME_DATA:
      oem_usb_dbg_get_frame_data(request, req_len, response, res_len);
      break;
    case CMD_OEM_USB_DBG_CTRL_PANEL:
      oem_usb_dbg_control_panel(request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
  }
  pthread_mutex_unlock(&m_oem_usb_dbg);
}

static void
oem_zion_get_system_mode(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t mode = 0xff;
  int ret;

  ret = pal_get_host_system_mode(&mode);
  if (ret == PAL_ENOTSUP) {
    memcpy(res->data, req->data, SIZE_IANA_ID); // IANA ID
    *res_len = SIZE_IANA_ID;
    res->cc = CC_INVALID_CMD;
  } else {
    res->data[0] = mode;
    *res_len = 1;
    res->cc = ret;
  }
}

static void
oem_zion_set_system_mode(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  int ret;

  ret = pal_set_host_system_mode(req->data[0]);
  if (ret == PAL_ENOTSUP)
    res->cc = CC_INVALID_CMD;
  else
    res->cc = ret;
}

static void
oem_zion_set_usb_path(unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  uint8_t slot = req->data[0];
  uint8_t endpoint = req->data[1];
  int ret;

  ret = pal_set_usb_path(slot, endpoint);
  if (ret == PAL_ENOTSUP)
    res->cc = CC_INVALID_CMD;
  else
    res->cc = ret;
}

static void
oem_zion_get_sensor_value(unsigned char *request, unsigned char req_len, unsigned char *response,
      unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  res->cc = pal_ipmb_get_sensor_val(req->payload_id, req->data, req_len, res->data, res_len);

  return;
}

static void
ipmi_handle_oem_zion(unsigned char *request, unsigned char req_len,
     unsigned char *response, unsigned char *res_len)
{
  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;

  unsigned char cmd = req->cmd;

  pthread_mutex_lock(&m_oem_zion);
  switch (cmd)
  {
    case CMD_OEM_ZION_GET_SYSTEM_MODE:
      oem_zion_get_system_mode(request, req_len, response, res_len);
      break;
    case CMD_OEM_ZION_GET_SENSOR_VALUE:
      oem_zion_get_sensor_value(request, req_len, response, res_len);
      break;
    case CMD_OEM_ZION_SET_SYSTEM_MODE:
      oem_zion_set_system_mode(request, req_len, response, res_len);
      break;
    case CMD_OEM_ZION_SET_USB_PATH:
      oem_zion_set_usb_path(request, req_len, response, res_len);
      break;
    default:
      res->cc = CC_INVALID_CMD;
      memcpy(res->data, req->data, SIZE_IANA_ID); //IANA ID
      *res_len = 3;
      break;
  }
  pthread_mutex_unlock(&m_oem_zion);
}

/*
 * Function to handle all IPMI messages
 */
static void
ipmi_handle (unsigned char *request, unsigned char req_len,
       unsigned char *response, unsigned char *res_len)
{

  ipmi_mn_req_t *req = (ipmi_mn_req_t *) request;
  ipmi_res_t *res = (ipmi_res_t *) response;
  unsigned char netfn;
  netfn = req->netfn_lun >> 2;

  // Provide default values in the response message
  res->cmd = req->cmd;
  res->cc = 0xFF;   // Unspecified completion code
  printf("ipmi_handle netfn %x cmd %x len %d\n", netfn, req->cmd, req_len);

  if (gLogEnable) {
    char logbuf[256];
    uint16_t nleft = sizeof(logbuf);
    uint16_t nwrite = 0;
    uint16_t i = 0;
    syslog(LOG_INFO, "%lu ipmi_handle  netfn %x cmd %x len %d\n",
          (unsigned long)time(NULL), netfn, req->cmd, req_len);
    nwrite = snprintf(logbuf, nleft, " ipmi_handle recv data: ");
    i += nwrite;
    nleft -= nwrite;
    for (int j = 0; j < req_len; ++j) {
      nwrite = snprintf(logbuf + i, nleft, "%x ", (unsigned int)(request[j]));
      i += nwrite;
      nleft -= nwrite;
    }
    syslog(LOG_INFO, "%s", logbuf);
  }

  *(unsigned short*)res_len = 0;

  switch (netfn)
  {
    case NETFN_CHASSIS_REQ:
      res->netfn_lun = NETFN_CHASSIS_RES << 2;
      ipmi_handle_chassis (request, req_len, response, res_len);
      break;
    case NETFN_SENSOR_REQ:
      res->netfn_lun = NETFN_SENSOR_RES << 2;
      ipmi_handle_sensor (request, req_len, response, res_len);
      break;
    case NETFN_APP_REQ:
      res->netfn_lun = NETFN_APP_RES << 2;
      ipmi_handle_app (request, req_len, response, res_len);
      break;
    case NETFN_STORAGE_REQ:
      res->netfn_lun = NETFN_STORAGE_RES << 2;
      ipmi_handle_storage (request, req_len, response, res_len);
      break;
    case NETFN_TRANSPORT_REQ:
      res->netfn_lun = NETFN_TRANSPORT_RES << 2;
      ipmi_handle_transport (request, req_len, response, res_len);
      break;
    case NETFN_GROUP_EXTENSION_REQ:
      res->netfn_lun = NETFN_GROUP_EXTENSION_RES << 2;
      ipmi_handle_group_extension(request, req_len, response, res_len);
      break;
    case NETFN_OEM_REQ:
      res->netfn_lun = NETFN_OEM_RES << 2;
      ipmi_handle_oem (request, req_len, response, res_len);
      break;
    case NETFN_OEM_STORAGE_REQ:
      res->netfn_lun = NETFN_OEM_STORAGE_RES << 2;
      ipmi_handle_oem_storage (request, req_len, response, res_len);
      break;
    case NETFN_OEM_Q_REQ:
      res->netfn_lun = NETFN_OEM_Q_RES << 2;
      ipmi_handle_oem_q (request, req_len, response, res_len);
      break;
    case NETFN_OEM_1S_REQ:
      res->netfn_lun = NETFN_OEM_1S_RES << 2;
      ipmi_handle_oem_1s(request, req_len, response, res_len);
      break;
    case NETFN_OEM_USB_DBG_REQ:
      res->netfn_lun = NETFN_OEM_USB_DBG_RES << 2;
      ipmi_handle_oem_usb_dbg(request, req_len, response, res_len);
      break;
    case NETFN_OEM_ZION_REQ:
      res->netfn_lun = NETFN_OEM_ZION_RES << 2;
      ipmi_handle_oem_zion(request, req_len, response, res_len);
      break;
    default:
      res->netfn_lun = (netfn + 1) << 2;
      break;
  }

  // This header includes NetFunction, Command, and Completion Code
  *(unsigned short*)res_len += IPMI_RESP_HDR_SIZE;

  return;
}

int
conn_handler(client_t *cli) {
  unsigned char req_buf[MAX_IPMI_MSG_SIZE];
  unsigned char res_buf[MAX_IPMI_MSG_SIZE];
  size_t req_len = MAX_IPMI_MSG_SIZE, res_len = 0;

  memset(req_buf, 0, sizeof(req_buf));
  memset(res_buf, 0, sizeof(res_buf));

  if (ipc_recv_req(cli, req_buf, &req_len, TIMEOUT_IPMI)) {
    syslog(LOG_WARNING, "ipmid: recv() failed\n");
    return -1;
  }

  ipmi_handle(req_buf, (unsigned char)req_len, res_buf, (unsigned char*)&res_len);

  if (res_len == 0) {
    return -1;
  }

  if(ipc_send_resp(cli, res_buf, res_len)) {
    syslog(LOG_WARNING, "ipmid: send() failed\n");
    return -1;
  }
  return 0;
}

void *
wdt_timer (void *arg) {
  int ret;
  struct watchdog_data *wdt = (struct watchdog_data *)arg;
  uint8_t status;
  int action = 0;

  while (1) {
    usleep(100*1000);
    pthread_mutex_lock(&wdt->mutex);
    if (wdt->valid && wdt->run) {

      // Check if power off
      ret = pal_get_server_power(wdt->slot, &status);
      if ((ret >= 0) && (status != SERVER_POWER_ON)) {
        wdt->run = 0;
        pthread_mutex_unlock(&wdt->mutex);
        continue;
      }

      // count down; counter 0 and run associated timer events occur immediately
      if (wdt->present_count_down)
        wdt->present_count_down--;

      // Pre-timeout no support
      /*
      if (wdt->present_count_down == wdt->pre_interval*10) {
      }
      */

      // Timeout
      if (wdt->present_count_down == 0) {
        wdt->expiration |= (1 << wdt->use);

        // Execute actin out of mutex
        action = wdt->action;

        pal_set_fru_post(wdt->slot, 0);

        if (wdt->no_log) {
          wdt->no_log = 0;
        }
        else {
          syslog(LOG_CRIT, "FRU: %u, %s Watchdog %s",
            wdt->slot,
            wdt_use_name[wdt->use & 0x7],
            wdt_action_name[action & 0x7]);
        }

        wdt->run = 0;
      } /* End of Timeout Action*/
    } /* End of Valid and Run */
    pthread_mutex_unlock(&wdt->mutex);

    // Execute actin out of mutex
    if (action) {
      if (pal_is_crashdump_ongoing(wdt->slot)) {
        syslog(LOG_WARNING, "ipmid: fru%u crashdump is ongoing, ignore wdt action %s",
          wdt->slot,
          wdt_action_name[action & 0x7]);
        action = 0;
        continue;
      }

      pal_set_restart_cause(wdt->slot, RESTART_CAUSE_WATCHDOG_EXPIRATION);
      switch (action) {
      case 1: // Hard Reset
        pal_set_server_power(wdt->slot, SERVER_POWER_RESET);
        break;
      case 2: // Power Down
        pal_set_server_power(wdt->slot, SERVER_POWER_OFF);
        break;
      case 3: // Power Cycle
        pal_set_server_power(wdt->slot, SERVER_POWER_CYCLE);
        break;
      case 0: // no action
      default:
        break;
      }
      action = 0;
    }

  } /* Forever while */

  pthread_exit(NULL);
}

// optional  "-d" cmd line option to enable logging
int
main (int argc, char **argv)
{
  int fru;
  pthread_t tid;
  uint8_t max_slot_num = 0;

  //daemon(1, 1);
  //openlog("ipmid", LOG_CONS, LOG_DAEMON);

  if (argc > 2) {
    syslog(LOG_ERR, "ipmid: argc > 2!");
    exit(1);
  } else {
    if ((argc == 2) && (strncmp(argv[1], "-d", 2) == 0)) {
      syslog(LOG_INFO, "ipmid: enable logging");
      gLogEnable = 1;
    }
  }

  plat_fruid_init();
  plat_sensor_init();
  plat_lan_init(&g_lan_config);

  sdr_init();
  sel_init();
  init_host_directory();

  pthread_mutex_init(&m_chassis, NULL);
  pthread_mutex_init(&m_sensor, NULL);
  pthread_mutex_init(&m_app, NULL);
  pthread_mutex_init(&m_storage, NULL);
  pthread_mutex_init(&m_transport, NULL);
  pthread_mutex_init(&m_group_extension, NULL);
  pthread_mutex_init(&m_oem, NULL);
  pthread_mutex_init(&m_oem_1s, NULL);
  pthread_mutex_init(&m_oem_usb_dbg, NULL);
  pthread_mutex_init(&m_oem_q, NULL);
  pthread_mutex_init(&m_oem_zion, NULL);

  pal_get_num_slots(&max_slot_num);
  fru = 1;

  while(fru <= max_slot_num){
    //tpm
    pal_create_TPMTimer(fru);
    struct watchdog_data *wdt_data = calloc(1, sizeof(struct watchdog_data));
    if (!wdt_data) {
      syslog(LOG_WARNING, "ipmid: allocation wdt info failed!\n");
      continue;
    }
    wdt_data->slot = fru;
    wdt_data->valid = 0;
    wdt_data->pre_interval = 1;
    pthread_mutex_init(&wdt_data->mutex, NULL);

    g_wdt[fru - 1] = wdt_data;
    pthread_create(&wdt_data->tid, NULL, wdt_timer, wdt_data);
    pthread_detach(wdt_data->tid);
    pal_set_def_restart_cause( fru );
    fru++;
  }

  // set flag to notice BMC ipmid is ready
  kv_set("flag_ipmid", "1", 0, 0);

  if (ipc_start_svc(SOCK_PATH_IPMI, conn_handler, MAX_REQUESTS, NULL, &tid) == 0) {
    pthread_join(tid, NULL);
  }


  pthread_mutex_destroy(&m_chassis);
  pthread_mutex_destroy(&m_sensor);
  pthread_mutex_destroy(&m_app);
  pthread_mutex_destroy(&m_storage);
  pthread_mutex_destroy(&m_transport);
  pthread_mutex_destroy(&m_group_extension);
  pthread_mutex_destroy(&m_oem);
  pthread_mutex_destroy(&m_oem_1s);
  pthread_mutex_destroy(&m_oem_usb_dbg);
  pthread_mutex_destroy(&m_oem_q);

  return 0;
}
