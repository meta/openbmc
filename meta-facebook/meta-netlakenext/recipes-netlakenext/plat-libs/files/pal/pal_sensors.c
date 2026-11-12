#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <string.h>
#include <ctype.h>
#include <openbmc/kv.h>
#include <openbmc/obmc-sensors.h>
#include <openbmc/libgpio.h>
#include <openbmc/obmc-i2c.h>
#include <openbmc/ipmb.h>
#include <openbmc/nvme-mi.h>
#include "pal.h"
#include "pal_sensors.h"

static int read_adc_val(uint8_t id, float *value);
static int read_battery_val(uint8_t id, float *value);
static int read_temp(uint8_t id, float *value);
static int read_dimm_temp(uint8_t id, float *value);
static int read_dimm_pwr(uint8_t id, float *value);
static int read_rpm(uint8_t id, float *value);
static int read_pmbus(uint8_t id, float *value);
static int read_hsc(uint8_t id, float *value);
static int read_cpld_adc(uint8_t id, float *value);
static int read_ina230(uint8_t id, float *value);
static int read_nvme_temp(uint8_t id, float *value);
static int read_nic_temp(uint8_t id, float *value);
static int read_cpu_hwmon(uint8_t id, float *value);
static int retrySensor = 0;
static int retryFAN[4] = {0, 0, 0, 0};

//{SensorName, ID, FUNCTION, RAEDING AVAILABLE, {UCR, UNC, UNR, LCR, LNC, LNR, Pos, Neg}, unit}
PAL_SENSOR_MAP server_sensor_map[] = {
  [SERVER_INLET_TEMP] =
  {"NETLAKE2_INLET_TEMP", MB_INLET, read_temp, STBY_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_OUTLET_TEMP] =
  {"NETLAKE2_OUTLET_TEMP", MB_OUTLET, read_temp, STBY_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_SOC_TEMP] =
  {"SOC_TEMP", SOC_TEMP, read_cpu_hwmon, POST_COMPLT_READING, {85, 0, 120, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_SOC_PWR] =
  {"SOC_PWR", SOC_PWR, read_cpu_hwmon, POST_COMPLT_READING, {51.5, 51, 52, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_DIMMA_TEMP] =
  {"DIMMA_TEMP", DIMMA_TEMP, read_dimm_temp, POST_COMPLT_READING, {85, 0, 95, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_DIMMA_PWR] =
  {"DIMMA_PWR", PMIC_DIMMA_PWR, read_dimm_pwr, POST_COMPLT_READING, {10, 0, 0, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_DIMMB_TEMP] =
  {"DIMMB_TEMP", DIMMB_TEMP, read_dimm_temp, POST_COMPLT_READING, {85, 0, 95, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_DIMMB_PWR] =
  {"DIMMB_PWR", PMIC_DIMMB_PWR, read_dimm_pwr, POST_COMPLT_READING, {10, 0, 0, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_A_P12V_STBY_NETLAKE2_VOL] =
  {"A_P12V_STBY_NETLAKE2_VOL", CPLD_ADC_P12V_STBY, read_cpld_adc, STBY_READING, {13.35, 13.22, 13.48, 10.71, 10.82, 10.6, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P3V3_STBY_NETLAKE2_VOL] =
  {"A_P3V3_STBY_NETLAKE2_VOL", CPLD_ADC_P3V3_STBY, read_cpld_adc, STBY_READING, {3.57, 3.53, 3.6, 3.04, 3.07, 3.01, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P1V8_STBY_NETLAKE2_VOL] =
  {"A_P1V8_STBY_NETLAKE2_VOL", CPLD_ADC_P1V8_STBY, read_cpld_adc, STBY_READING, {1.92, 1.9, 1.94, 1.68, 1.69, 1.66, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P1V8_AUDIO_NETLAKE2_VOL] =
  {"A_P1V8_AUDIO_NETLAKE2_VOL", CPLD_ADC_P1V8_AUDIO, read_cpld_adc, STBY_READING, {1.92, 1.9, 1.94, 1.68, 1.69, 1.66, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P1V8_NETLAKE2_VOL] =
  {"A_P1V8_NETLAKE2_VOL", CPLD_ADC_P1V8, read_cpld_adc, POWER_ON_READING, {1.92, 1.9, 1.94, 1.68, 1.69, 1.66, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P5V_STBY_NETLAKE2_VOL] =
  {"A_P5V_STBY_NETLAKE2_VOL", CPLD_ADC_P5V_STBY, read_cpld_adc, STBY_READING, {5.41, 5.36, 5.46, 4.61, 4.66, 4.56, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P5V_DIMM_NETLAKE2_VOL] =
  {"A_P5V_DIMM_NETLAKE2_VOL", CPLD_ADC_P5V_DIMM, read_cpld_adc, POWER_ON_READING, {5.41, 5.36, 5.46, 4.61, 4.66, 4.56, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_P3V3_NETLAKE2_VOL] =
  {"A_P3V3_NETLAKE2_VOL", CPLD_ADC_P3V3, read_cpld_adc, POWER_ON_READING, {3.57, 3.53, 3.6, 3.04, 3.07, 3.01, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_PVDDIO_MEM_S3_VOL] =
  {"A_PVDDIO_MEM_S3_VOL", CPLD_ADC_PVDDIO_MEM_S3, read_cpld_adc, POWER_ON_READING, {1.20, 1.19, 1.21, 1.03, 1.05, 1.02, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_A_PVDD_MISC_S5_VOL] =
  {"A_PVDD_MISC_S5_VOL", CPLD_ADC_PVDD_MISC_S5, read_cpld_adc, STBY_READING, {0.81, 0.80, 0.82, 0.69, 0.70, 0.68, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_TEMP] =
  {"PVDDCR_TEMP", VR_PVDDCR_TEMP, read_pmbus, POWER_ON_READING, {100, 0, 125, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_SOC_TEMP] =
  {"PVDDCR_SOC_TEMP", VR_PVDDCR_SOC_TEMP, read_pmbus, POWER_ON_READING, {100, 0, 125, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_PVDD_MISC_TEMP] =
  {"PVDD_MISC_TEMP", VR_PVDD_MISC_TEMP, read_pmbus, POWER_ON_READING, {100, 0, 125, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_VOL] =
  {"PVDDCR_VOL", VR_PVDDCR_VOL, read_pmbus, POWER_ON_READING, {1.82, 1.75, 1.90, 0, 0, 0, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_SOC_VOL] =
  {"PVDDCR_SOC_VOL", VR_PVDDCR_SOC_VOL, read_pmbus, POWER_ON_READING, {1.46, 1.36, 1.55, 0, 0, 0, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_PVDD_MISC_VOL] =
  {"PVDD_MISC_VOL", VR_PVDD_MISC_VOL, read_pmbus, POWER_ON_READING, {1.42, 1.29, 1.55, 0, 0, 0, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_CUR] =
  {"PVDDCR_CUR", VR_PVDDCR_CUR, read_pmbus, POWER_ON_READING, {180, 0, 192.15, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_SOC_CUR] =
  {"PVDDCR_SOC_CUR", VR_PVDDCR_SOC_CUR, read_pmbus, POWER_ON_READING, {44, 0, 49.7, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [SERVER_PVDD_MISC_CUR] =
  {"PVDD_MISC_CUR", VR_PVDD_MISC_CUR, read_pmbus, POWER_ON_READING, {35.75, 0, 50, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_PWR] =
  {"PVDDCR_PWR", VR_PVDDCR_PWR, read_pmbus, POWER_ON_READING, {131.84, 0, 133.12, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_PVDDCR_SOC_PWR] =
  {"PVDDCR_SOC_PWR", VR_PVDDCR_SOC_PWR, read_pmbus, POWER_ON_READING, {30.9, 0, 31.2, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_PVDD_MISC_PWR] =
  {"PVDD_MISC_PWR", VR_PVDD_MISC_PWR, read_pmbus, POWER_ON_READING, {24.72, 0, 24.96, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_P12V_COME_PWR] =
  {"P12V_COME_PWR", P12V_COME_PWR, read_ina230, STBY_READING, {117.4645454, 0, 119.7564826, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [SERVER_P12V_COME_IOUT] =
  {"P12V_COME_IOUT", P12V_COME_IOUT, read_ina230, STBY_READING, {10.97, 9.87, 0, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
};

PAL_SENSOR_MAP bmc_sensor_map[] = {
  [BMC_P12V_STBY_MTP_VOL] =
  {"P12V_STBY_MTP_VOL", ADC1, read_adc_val, STBY_READING, {13.349, 13.219, 14.333, 10.709, 10.819, 10.091, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P12V_COME_VOL] =
  {"P12V_COME_MTP_VOL", ADC3, read_adc_val, STBY_READING, {13.349, 13.219, 14.333, 10.709, 10.819, 10.091, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P3V_BAT_MTP_VOL] =
  {"P3V_BAT_MTP_VOL", ADC8, read_battery_val, STBY_READING, {3.502, 3.468, 3.993, 2.764, 2.793, 2.31, 0, 0}, VOLT, ONE_HOUR_INVERVAL},
  [BMC_P3V3_STBY_MTP_VOL] =
  {"P3V3_STBY_MTP_VOL", ADC2, read_adc_val, STBY_READING, {3.569, 3.534, 3.993, 3.041, 3.072, 2.31, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P5V_STBY_VOL] =
  {"P5V_STBY_VOL", ADC0, read_adc_val, STBY_READING, {5.407, 5.355, 5.8, 4.608, 4.655, 4, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P12V_NIC_MTP_VOL] =
  {"P12V_NIC_MTP_VOL", ADC7, read_adc_val, STBY_READING, {13.349, 13.219, 14.333, 10.709, 10.819, 10.091, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P3V3_NIC_MTP_VOL] =
  {"P3V3_NIC_MTP_VOL", ADC6, read_adc_val, STBY_READING, {3.569, 3.534, 3.993, 3.041, 3.072, 2.31, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_P12V_NIC_MTP_CURR] =
  {"P12V_NIC_MTP_CURR", ADC5, read_adc_val, STBY_READING, {3.9, 0, 0, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [BMC_P3V3_NIC_MTP_CURR] =
  {"P3V3_NIC_MTP_CURR", ADC4, read_adc_val, STBY_READING, {1, 0, 0, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [BMC_HSC_OUTPUT_VOL] =
  {"HSC_OUTPUT_VOL", HSC_VOUT, read_hsc, STBY_READING, {13.349, 13.219, 14.333, 10.709, 10.819, 10.091, 0, 0}, VOLT, NORMAL_POLL_INTERVAL},
  [BMC_HSC_OUTPUT_CUR] =
  {"HSC_OUTPUT_CUR", HSC_IOUT, read_hsc, STBY_READING, {20, 0, 0, 0, 0, 0, 0, 0}, CURR, NORMAL_POLL_INTERVAL},
  [BMC_HSC_INPUT_PWR] =
  {"HSC_INPUT_PWR", HSC_PIN, read_hsc, STBY_READING, {200, 0, 0, 0, 0, 0, 0, 0}, POWER, NORMAL_POLL_INTERVAL},
  [BMC_M2_B_TEMP] =
  {"M.2_B_TEMP", NVME_B_TEMP, read_nvme_temp, POWER_ON_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [BMC_M2_D_TEMP] =
  {"M.2_D_TEMP", NVME_D_TEMP, read_nvme_temp, POWER_ON_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
  [BMC_E1S_TEMP] =
  {"E1.S_TEMP", NVME_E1S_TEMP, read_nvme_temp, POWER_ON_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
};

PAL_SENSOR_MAP fio_sensor_map[] = {
  [FIO_INLET_TEMP] =
  {"FIO_INLET_TEMP", FIO_INLET, read_temp, STBY_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
};

PAL_SENSOR_MAP nic_sensor_map[] = {
  [BMC_OCP_NIC_TEMP] =
  {"OCP_NIC_TEMP", OCP_NIC_TEMP, read_nic_temp, POWER_ON_READING, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP, NORMAL_POLL_INTERVAL},
};

PAL_SENSOR_MAP pdb_sensor_map[] = {
  [FAN0_TACH] =
  {"FAN0_TACH", FAN0, read_rpm, STBY_READING, {0, 0, 0, 1200, 0, 0, 0, 0}, FAN, FAN_POLL_INTERVAL},
  [FAN1_TACH] =
  {"FAN1_TACH", FAN1, read_rpm, STBY_READING, {0, 0, 0, 1200, 0, 0, 0, 0}, FAN, FAN_POLL_INTERVAL},
  [FAN2_TACH] =
  {"FAN2_TACH", FAN2, read_rpm, STBY_READING, {0, 0, 0, 1200, 0, 0, 0, 0}, FAN, FAN_POLL_INTERVAL},
  [FAN3_TACH] =
  {"FAN3_TACH", FAN3, read_rpm, STBY_READING, {0, 0, 0, 1200, 0, 0, 0, 0}, FAN, FAN_POLL_INTERVAL},
};

const uint8_t server_sensor_list[] = {
  SERVER_INLET_TEMP,
  SERVER_OUTLET_TEMP,
  SERVER_SOC_TEMP,
  SERVER_SOC_PWR,
  SERVER_DIMMA_TEMP,
  SERVER_DIMMA_PWR,
  SERVER_DIMMB_TEMP,
  SERVER_DIMMB_PWR,
  SERVER_A_P12V_STBY_NETLAKE2_VOL,
  SERVER_A_P3V3_STBY_NETLAKE2_VOL,
  SERVER_A_P1V8_STBY_NETLAKE2_VOL,
  SERVER_A_P1V8_AUDIO_NETLAKE2_VOL,
  SERVER_A_P1V8_NETLAKE2_VOL,
  SERVER_A_P5V_STBY_NETLAKE2_VOL,
  SERVER_A_P5V_DIMM_NETLAKE2_VOL,
  SERVER_A_P3V3_NETLAKE2_VOL,
  SERVER_A_PVDDIO_MEM_S3_VOL,
  SERVER_A_PVDD_MISC_S5_VOL,
  SERVER_PVDDCR_TEMP,
  SERVER_PVDDCR_SOC_TEMP,
  SERVER_PVDD_MISC_TEMP,
  SERVER_PVDDCR_VOL,
  SERVER_PVDDCR_SOC_VOL,
  SERVER_PVDD_MISC_VOL,
  SERVER_PVDDCR_CUR,
  SERVER_PVDDCR_SOC_CUR,
  SERVER_PVDD_MISC_CUR,
  SERVER_PVDDCR_PWR,
  SERVER_PVDDCR_SOC_PWR,
  SERVER_PVDD_MISC_PWR,
  SERVER_P12V_COME_PWR,
  SERVER_P12V_COME_IOUT,
};

const uint8_t bmc_sensor_list[] = {
  BMC_P12V_STBY_MTP_VOL,
  BMC_P12V_COME_VOL,
  BMC_P3V_BAT_MTP_VOL,
  BMC_P3V3_STBY_MTP_VOL,
  BMC_P5V_STBY_VOL,
  BMC_P12V_NIC_MTP_VOL,
  BMC_P3V3_NIC_MTP_VOL,
  BMC_P12V_NIC_MTP_CURR,
  BMC_P3V3_NIC_MTP_CURR,
  BMC_HSC_OUTPUT_VOL,
  BMC_HSC_OUTPUT_CUR,
  BMC_HSC_INPUT_PWR,
  BMC_M2_B_TEMP,
  BMC_M2_D_TEMP,
  BMC_E1S_TEMP,
};

const uint8_t pdb_sensor_list[] = {
  FAN0_TACH,
  FAN1_TACH,
  FAN2_TACH,
  FAN3_TACH,
};

const uint8_t dimm_addr_list[] = {
  DIMMA_ADDR,
  DIMMB_ADDR,
};

const uint8_t pmic_addr_list[] = {
  PMICA_ADDR,
  PMICB_ADDR,
};

const uint8_t fio_sensor_list[] = {
  FIO_INLET_TEMP,
};

const uint8_t nic_sensor_list[] = {
  BMC_OCP_NIC_TEMP,
};

PAL_DEV_INFO hwmon_dev_list[] = {
  {"tmp75-i2c-4-4c",  "MB_INLET_TEMP"},
  {"tmp75-i2c-4-4a",  "MB_OUTLET_TEMP"},
  {"tmp75-i2c-10-48",  "FIO_INLET_TEMP"},
  {"sbtsi_0.0-i2c-5-4c",  "SOC_TEMP"},
  {"sbrmi_0.0-i2c-5-3c",  "SOC_PWR"},
};

const char *adc_label[] = {
  "ADC_P5V_STBY_VOL",
  "ADC_P12V_STBY_MTP_VOL",
  "ADC_P3V3_STBY_MTP_VOL",
  "ADC_P12V_COME_VOL",
  "ADC_P3V3_NIC_MTP_CURR",
  "ADC_P12V_NIC_MTP_CURR",
  "ADC_P3V3_NIC_MTP_VOL",
  "ADC_P12V_NIC_MTP_VOL",
  "ADC_P3V_BAT_MTP_VOL",
};

PAL_PMBUS_INFO pmbus_dev_table[] = {
  [VR_PVDDCR_TEMP] =
  {VR_BUS, VR_PVDDCR_ADDR, {{MP29608B, PAGE0, CMD_TEMP1}, {XDPE19283D, PAGE0, CMD_TEMP1}, {RAA229641, PAGE0, CMD_TEMP1}}},
  [VR_PVDDCR_SOC_TEMP] =
  {VR_BUS, VR_PVDDCR_SOC_ADDR, {{MP29608B, PAGE1, CMD_TEMP1}, {XDPE19283D, PAGE1, CMD_TEMP2}, {RAA229641, PAGE1, CMD_TEMP2}}},
  [VR_PVDD_MISC_TEMP] =
  {VR_BUS, VR_PVDD_MISC_ADDR, {{MP29608B, PAGE0, CMD_TEMP1}, {XDPE19283D, PAGE0, CMD_TEMP1}, {RAA229641, PAGE0, CMD_TEMP1}}},
  [VR_PVDDCR_VOL] =
  {VR_BUS, VR_PVDDCR_ADDR, {{MP29608B, PAGE0, CMD_VOUT}, {XDPE19283D, PAGE0, CMD_VOUT}, {RAA229641, PAGE0, CMD_VOUT}}},
  [VR_PVDDCR_SOC_VOL] =
  {VR_BUS, VR_PVDDCR_SOC_ADDR, {{MP29608B, PAGE1, CMD_VOUT}, {XDPE19283D, PAGE1, CMD_VOUT}, {RAA229641, PAGE1, CMD_VOUT}}},
  [VR_PVDD_MISC_VOL] =
  {VR_BUS, VR_PVDD_MISC_ADDR, {{MP29608B, PAGE0, CMD_VOUT}, {XDPE19283D, PAGE0, CMD_VOUT}, {RAA229641, PAGE0, CMD_VOUT}}},
  [VR_PVDDCR_CUR] =
  {VR_BUS, VR_PVDDCR_ADDR, {{MP29608B, PAGE0, CMD_IOUT}, {XDPE19283D, PAGE0, CMD_IOUT}, {RAA229641, PAGE0, CMD_IOUT}}},
  [VR_PVDDCR_SOC_CUR] =
  {VR_BUS, VR_PVDDCR_SOC_ADDR, {{MP29608B, PAGE1, CMD_IOUT}, {XDPE19283D, PAGE1, CMD_IOUT}, {RAA229641, PAGE1, CMD_IOUT}}},
  [VR_PVDD_MISC_CUR] =
  {VR_BUS, VR_PVDD_MISC_ADDR, {{MP29608B, PAGE0, CMD_IOUT}, {XDPE19283D, PAGE0, CMD_IOUT}, {RAA229641, PAGE0, CMD_IOUT}}},
  [VR_PVDDCR_PWR] =
  {VR_BUS, VR_PVDDCR_ADDR, {{MP29608B, PAGE0, CMD_POUT}, {XDPE19283D, PAGE0, CMD_POUT}, {RAA229641, PAGE0, CMD_POUT}}},
  [VR_PVDDCR_SOC_PWR] =
  {VR_BUS, VR_PVDDCR_SOC_ADDR, {{MP29608B, PAGE1, CMD_POUT}, {XDPE19283D, PAGE1, CMD_POUT}, {RAA229641, PAGE1, CMD_POUT}}},
  [VR_PVDD_MISC_PWR] =
  {VR_BUS, VR_PVDD_MISC_ADDR, {{MP29608B, PAGE0, CMD_POUT}, {XDPE19283D, PAGE0, CMD_POUT}, {RAA229641, PAGE0, CMD_POUT}}},
};

PAL_PMBUS_INFO hsc_dev_table[] = {
  [HSC_VOUT] = {MTP_HSC_BUS, MTP_HSC_ADDR, {{ADM1278, HSC_NO_SET_PAGE, CMD_VOUT}}},
  [HSC_IOUT] = {MTP_HSC_BUS, MTP_HSC_ADDR, {{ADM1278, HSC_NO_SET_PAGE, CMD_IOUT}}},
  [HSC_PIN] = {MTP_HSC_BUS, MTP_HSC_ADDR, {{ADM1278, HSC_NO_SET_PAGE, CMD_PIN}}},
};

PAL_INA230_INFO ina230_dev_table[] = {
  [P12V_COME_PWR] = {INA230_BUS, INA230_ADDR, INA230_POWER},
  [P12V_COME_IOUT] = {INA230_BUS, INA230_ADDR, INA230_IOUT},
};

/*List item: {CPLD-ADC reading low-byte offset, resistor ratio}
* High-byte offset of reading always be low-byte offset + 1
*/
PAL_CPLD_ADC_INFO cpld_adc_list[] = {
  {0x00, 15.70588},
  {0x04, 2},
  {0x06, 2},
  {0x02, 3.61111},
  {0x0A, 5.7},
  {0x08, 2},
  {0x12, 1},
  {0x10, 1.21277},
  {0x0E, 3.61111},
  {0x0C, 5.7},
};

const uint8_t nvme_temp_list[] = {
  [NVME_B_TEMP] = NVME_B_BUS,
  [NVME_D_TEMP] = NVME_D_BUS,
  [NVME_E1S_TEMP] = NVME_E1S_BUS,
};

PAL_PMBUS_POWER_TABLE power_cal_list[] = {
  [VR_PVDDCR_PWR] = {VR_PVDDCR_VOL, VR_PVDDCR_CUR},
  [VR_PVDDCR_SOC_PWR] = {VR_PVDDCR_SOC_VOL, VR_PVDDCR_SOC_CUR},
  [VR_PVDD_MISC_PWR] = {VR_PVDD_MISC_VOL, VR_PVDD_MISC_CUR},
};

size_t server_sensor_cnt = ARRAY_SIZE(server_sensor_list);
size_t bmc_sensor_cnt = ARRAY_SIZE(bmc_sensor_list);
size_t pdb_sensor_cnt = ARRAY_SIZE(pdb_sensor_list);
size_t fio_sensor_cnt = ARRAY_SIZE(fio_sensor_list);
size_t nic_sensor_cnt = ARRAY_SIZE(nic_sensor_list);

size_t pmbus_dev_cnt = ARRAY_SIZE(pmbus_dev_table);
size_t hsc_dev_cnt = ARRAY_SIZE(hsc_dev_table);

/**
*  @brief Function of getting sensor list and sensor count from FRU ID
*
*  @param fru: FRU ID
*  @param **sensor_list: return value of sensor list
*  @param *cnt: return value of sensor count
*
*  @return Status of getting FRU ID
*  0: Success
*  ERR_UNKNOWN_FRU: Unknown FRU error
**/
int
pal_get_fru_sensor_list(uint8_t fru, uint8_t **sensor_list, int *cnt) {

  switch(fru) {
  case FRU_SERVER:
    *sensor_list = (uint8_t *) server_sensor_list;
    *cnt = server_sensor_cnt;
    break;
  case FRU_BMC:
    *sensor_list = (uint8_t *) bmc_sensor_list;
    *cnt = bmc_sensor_cnt;
    break;
  case FRU_PDB:
    *sensor_list = (uint8_t *) pdb_sensor_list;
    *cnt = pdb_sensor_cnt;
    break;
  case FRU_FIO:
    *sensor_list = (uint8_t *) fio_sensor_list;
    *cnt = fio_sensor_cnt;
    break;
  case FRU_NIC:
    *sensor_list = (uint8_t *) nic_sensor_list;
    *cnt = nic_sensor_cnt;
    break;
  default:
    if (fru > MAX_NUM_FRUS) {
      return ERR_UNKNOWN_FRU;
    }
    // Nothing to read yet.
    *sensor_list = NULL;
    *cnt = 0;
    break;
  }

  return 0;
}

/**
*  @brief Function of reading tmp75 sensor
*
*  @param id: i2c device id
*  @param *value: return value of sensor reading
*
*  @return Status of reading sensor
*  0: Success
*  -1: Failed
*  ERR_SENSOR_NA: Sensor NA error
**/
static int
read_temp(uint8_t id, float *value) {
  if (id >= ARRAY_SIZE(hwmon_dev_list)) {
    return ERR_SENSOR_NA;
  }

  return sensors_read(hwmon_dev_list[id].chip, hwmon_dev_list[id].label, value);
}

static int
read_cpu_hwmon(uint8_t id, float *value) {
  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  if (id >= ARRAY_SIZE(hwmon_dev_list)) {
    return ERR_SENSOR_NA;
  }

  int ret = 0;
  ret = sensors_read(hwmon_dev_list[id].chip, hwmon_dev_list[id].label, value);
  if ((ret < 0) && (retrySensor < SENSOR_RETRY_TIME)) {
    sensors_reinit();
    sleep(POWER_ON_SENSOR_RETRY_SEC);
    ret = sensors_read(hwmon_dev_list[id].chip, hwmon_dev_list[id].label, value);
    if (ret != 0) {
      retrySensor++;
    }
  }

  return ret;
}

static int
read_dimm_temp(uint8_t id, float *value) {
  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  int ret = 0, fd = 0;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t tbuf = READ_DIMM_SPD_TEMP;
  uint8_t tlen = sizeof(tbuf);
  uint8_t rbuf = 0;
  uint8_t rlen = DIMM_TEMP_LEN;
  uint8_t low_byte = 0, high_byte = 0;

  fd = i2c_cdev_slave_open(DIMM_BUS, dimm_addr_list[id] >> 1,
                          I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_ERR, "Failed to open DIMM 0x%x\n", dimm_addr_list[id]);
    return -1;
  }

  // get low byte
  do {
    ret = i2c_rdwr_msg_transfer(fd, dimm_addr_list[id],
                                &tbuf, tlen, &rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get lowbyte reading %x-%x", __func__,
          DIMM_BUS, dimm_addr_list[id]);
    close(fd);
    return -1;
  }
  low_byte = rbuf;

  // get high byte
  tbuf += 1;
  retry = SENSOR_RETRY_TIME;
  do {
    ret = i2c_rdwr_msg_transfer(fd, dimm_addr_list[id],
                                &tbuf, tlen, &rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get highbyte reading %x-%x", __func__,
          DIMM_BUS, dimm_addr_list[id]);
    close(fd);
    return -1;
  }
  high_byte = rbuf;

  //Calculate the temp
  uint8_t int_temp = ((high_byte & 0x0F) << 4) | ((low_byte & 0xF0) >> 4);
  float decimal_temp = ((low_byte >> 3) & 0x1) * 0.5f + ((low_byte >> 2) & 0x1) * 0.25f;

  float temp = int_temp + decimal_temp;

  if (high_byte & 0x10) {
    temp = -temp;
  }

  *value = temp;
  close(fd);
  return ret;
}

static int
read_dimm_pwr(uint8_t id, float *value) {
  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  int ret = 0, fd = 0;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t tbuf = PMIC_VDD_READ;
  uint8_t tlen = sizeof(tbuf);
  uint8_t rbuf = 0;
  uint8_t rlen = 1;

  fd = i2c_cdev_slave_open(DIMM_BUS, pmic_addr_list[id] >> 1,
                          I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_ERR, "Failed to open PMIC 0x%x\n", pmic_addr_list[id]);
    return -1;
  }

  do {
    ret = i2c_rdwr_msg_transfer(fd, pmic_addr_list[id],
                                &tbuf, tlen, &rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to read DIMM power %x-%x", __func__,
          DIMM_BUS, pmic_addr_list[id]);
    close(fd);
    return -1;
  }

  *value = (float)rbuf * 0.125;

  close(fd);
  return 0;
}

static int
read_adc_val(uint8_t id, float *value) {
  int ret = 0;

  if (id >= ARRAY_SIZE(adc_label)) {
    return ERR_SENSOR_NA;
  }

  ret = sensors_read_adc(adc_label[id], value);

  if (*value < 0) {
    *value = 0;
  }

  return ret;
}

static int
read_battery_val(uint8_t id, float *value) {
  int ret = -1;

  gpio_desc_t *gpio_bat_sense_en = gpio_open_by_shadow("FM_BATTERY_SENSE_EN");
  if (!gpio_bat_sense_en) {
    return -1;
  }
  if (gpio_set_value(gpio_bat_sense_en, GPIO_VALUE_HIGH)) {
    syslog(LOG_WARNING, "%s() Fail to set gpio for reading sensor_id:%x", __func__, id);
    goto fail;
  }

  msleep(60);
  ret = read_adc_val(id, value);
  if (gpio_set_value(gpio_bat_sense_en, GPIO_VALUE_LOW)) {
    syslog(LOG_WARNING, "%s() Fail to recover gpio for reading sensor_id:%x", __func__, id);
    goto fail;
  }

fail:
  gpio_close(gpio_bat_sense_en);
  return ret;
}

static int
mp29608b_vid_convert(uint8_t *value_raw, uint8_t vid_step_raw, float *value) {
  uint16_t raw = (value_raw[1] << 8) | value_raw[0];
  float vid_step = 0.0;
  switch (vid_step_raw) {
    case 0b000:
      vid_step = 6.25 / 1000;
      break;
    case 0b001:
      vid_step = 5.0 / 1000;
      break;
    case 0b010:
      vid_step = 2.5 / 1000;
      break;
    case 0b011:
      vid_step = 2.0 / 1000;
      break;
    case 0b100:
      vid_step = 1.0 / 1000;
      break;
    case 0b101:
      vid_step = (1000 / 256) / 1000;
      break;
    case 0b110:
      vid_step = (1000 / 512) / 1000;
      break;
    case 0b111:
      vid_step = (1000 / 1024) / 1000;
      break;
    default:
      syslog(LOG_ERR, "%s: invalid parameter: vid_step_raw %02x", __func__, vid_step_raw);
      return -1;
  }
  *value = (float)raw * vid_step;
  return 0;
}

static int
read_pmbus(uint8_t id, float *value) {
  if (id >= ARRAY_SIZE(pmbus_dev_table)) {
    return ERR_SENSOR_NA;
  }

  int fd = 0, ret = 0;
  char key[MAX_KEY_LEN] = {0};
  uint8_t bus = pmbus_dev_table[id].bus;
  uint8_t addr = pmbus_dev_table[id].slv_addr;
  char val[MAX_VALUE_LEN] = {0};
  //kvbuf[0]: pmbus page, kvbuf[1]: pmbus offset, kvbuf[2]: read byte, kvbuf[3]: read format
  uint8_t kvbuf[5] = {0};
  char* saveptr;
  int bufcnt = 0;

  ret = kv_get(VR_DUMP_KV_KEY, val, NULL, 0);
  if ((ret == 0) && (strcmp(val, HIGH_STR) == 0)) {
    syslog(LOG_INFO, "%s() Skip pmbus read during VR dump", __func__);
    return SENSOR_NA;
  }

  snprintf(key, MAX_FRU_CMD_STR, "pmbus-sensor%02x%c", id, '\0');
  ret = kv_get(key, val, NULL, 0);
  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get pmbus info, errno=%d", __func__, errno);
    return -1;
  }

  char *kv_value;
  kv_value = strtok_r(val, "-", &saveptr);
  while( kv_value != NULL )
  {
    kvbuf[bufcnt] = atoi(kv_value);
    bufcnt++;
    kv_value = strtok_r(NULL, "-", &saveptr);
  }

  pmbus_i2c_info pmbus_i2c_data;
  memcpy(&pmbus_i2c_data, &kvbuf, sizeof(pmbus_i2c_data));

  if (pmbus_i2c_data.offset == CMD_POUT) {
    float voltage, current;

    ret = read_pmbus(power_cal_list[id].vol_sensor_id, &voltage);
    if (ret == ERR_VR_UPDATE_ONGOING) {
      syslog(LOG_INFO, "%s() Skip getting voltage value to calculate power, because VR update/get CRC is ongoing", __func__);
      return ERR_VR_UPDATE_ONGOING;
    } else if (ret < 0) {
      syslog(LOG_ERR, "%s() Failed to get voltage value to calculate power", __func__);
      return -1;
    }

    ret = read_pmbus(power_cal_list[id].cur_sensor_id, &current);
    if (ret == ERR_VR_UPDATE_ONGOING) {
      syslog(LOG_INFO, "%s() Skip getting current value to calculate power, because VR update/get CRC is ongoing", __func__);
      return ERR_VR_UPDATE_ONGOING;
    } else if (ret < 0) {
      syslog(LOG_ERR, "%s() Failed to get current value to calculate power", __func__);
      return -1;
    }

    *value = voltage * current;

    return 0;
  } else {
    uint8_t retry = SENSOR_RETRY_TIME;

    //Set pmbus page
    uint8_t setpage_data[2];
    setpage_data[0] = CMD_PAGE;
    setpage_data[1] = pmbus_i2c_data.page;

    fd = i2c_cdev_slave_open(bus, addr >> 1, I2C_SLAVE_FORCE_CLAIM);
    if (fd < 0) {
      syslog(LOG_ERR, "%s() Failed to open I2C bus %d\n", __func__, bus);
      return -1;
    }

    retry = SENSOR_RETRY_TIME;
    do {
      if (pal_is_fw_update_ongoing(FRU_SERVER)) {
        syslog(LOG_INFO, "%s() fru%d_fwupd detected, skip reading", __func__, FRU_SERVER);
        close(fd);
        return ERR_VR_UPDATE_ONGOING;
      }
      ret = i2c_rdwr_msg_transfer(fd, addr, setpage_data, sizeof(setpage_data),
                                  NULL, 0);
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    } while ((ret < 0) && ((retry--) > 0));

    if (ret < 0) {
      close(fd);
      syslog(LOG_ERR, "%s() Failed to set page %d-%d\n", __func__, bus, addr);
      return -1;
    }

    //Get pmbus reading
    uint8_t getreading_data = pmbus_i2c_data.offset;
    uint8_t rlen_reading = pmbus_i2c_data.read_byte;
    uint8_t rbuf_reading_raw[rlen_reading];
    uint8_t vout_mode_data = VR_VOUT_MODE_REG;
    uint8_t rlen_vout_mode = 1;
    uint8_t rbuf_vout_mode;

    retry = SENSOR_RETRY_TIME;
    do {
      ret = i2c_rdwr_msg_transfer(fd, addr, &getreading_data,
                                  sizeof(getreading_data),
                                  rbuf_reading_raw, rlen_reading);
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    } while ((ret < 0) && ((retry--) > 0));

    if (ret < 0) {
      close(fd);
      syslog(LOG_ERR, "%s() Failed to get reading %d-%d\n", __func__, bus, addr);
      return -1;
    }

    uint8_t read_format = pmbus_i2c_data.read_type;
    switch(read_format) {
    case LINEAR11:
      ret = netlakenext_common_linear11_convert(rbuf_reading_raw, value);
      if (ret < 0) {
          *value = 0;
          syslog(LOG_ERR, "%s() Failed to get reading by linear-11 format %d-%d\n",
              __func__, bus, addr);
        }
      break;
    case VOUT_MODE:
      ret = i2c_rdwr_msg_transfer(fd, addr, &vout_mode_data,
                                  sizeof(vout_mode_data), &rbuf_vout_mode, rlen_vout_mode);
      if (ret < 0) {
        close(fd);
        syslog(LOG_ERR, "%s() Failed to get VOUT_MODE for device %d-%d\n",
              __func__, bus, addr);
        return -1;
      }
      if (pmbus_i2c_data.type == MP29608B) {
        uint8_t rlen_vid_reading = READ_WORD;
        uint8_t rbuf_vid_reading_raw[rlen_vid_reading];
        uint8_t vid_step_reg = VR_MFR_VOUT_SCALE_LOOP_REG;
        
        retry = SENSOR_RETRY_TIME;
        do {
          ret = i2c_rdwr_msg_transfer(fd, addr, &vid_step_reg,
                                      sizeof(vid_step_reg),
                                      rbuf_vid_reading_raw, rlen_vid_reading);
          usleep(SENSOR_RETRY_INTERVAL_USEC);
        } while ((ret < 0) && ((retry--) > 0));

        if (ret < 0) {
          close(fd);
          syslog(LOG_ERR, "%s() Failed to get reading %d-%d %x\n", __func__, bus, addr, vid_step_reg);
          return -1;
        }
        ret = mp29608b_vid_convert(rbuf_reading_raw, (rbuf_vid_reading_raw[1] & 0x1c) >> 2, value);
        if (ret < 0) {
          *value = 0;
          syslog(LOG_ERR, "%s() Failed to get reading by mp29608b vid format %d-%d\n",
              __func__, bus, addr);
        }
      }
      else if (pmbus_i2c_data.type == XDPE19283D) {
        if (((rbuf_vout_mode >> 5) & 0x03) == 0) {
          ret = netlakenext_common_linear16_convert(rbuf_reading_raw, rbuf_vout_mode, value);
          if (ret < 0) {
            *value = 0;
            syslog(LOG_ERR, "%s() Failed to get reading by linear-16 format %d-%d\n",
                __func__, bus, addr);
          }
        } else if (((rbuf_vout_mode >> 5) & 0x03) == 0x01) {
          *value = (float)((rbuf_reading_raw[1] * 256) + rbuf_reading_raw[0]) * 0.005;
        } else {
          *value = 0;
          syslog(LOG_ERR, "%s() VOUT_MODE is not supported for device %d-%d\n",
                __func__, bus, addr);
        }
      }
      else {
        *value = 0;
        syslog(LOG_ERR, "%s() vr type is invalid %d-%d %d\n", __func__, bus, addr, pmbus_i2c_data.type);
      }
      break;
    case DIRECT:
      if (pmbus_i2c_data.type == RAA229641) {
        uint16_t value_tmp = ((uint16_t)rbuf_reading_raw[1] << 8) | rbuf_reading_raw[0];
        if (pmbus_i2c_data.offset != CMD_VOUT) {
          *value = (float)(int16_t)value_tmp;
        } else {
          *value = (float)value_tmp;
        }
        if (pmbus_i2c_data.offset == CMD_IOUT) {
          *value *= 0.1;
        } else if (pmbus_i2c_data.offset == CMD_VIN || pmbus_i2c_data.offset == CMD_IIN) {
          *value *= 0.01;
        } else if (pmbus_i2c_data.offset == CMD_VOUT) {
          *value *= 0.001;
        }
      }
      else {
        *value = 0;
        syslog(LOG_ERR, "%s() vr type is invalid %d-%d %d\n", __func__, bus, addr, pmbus_i2c_data.type);
      }
      break;
    default:
      *value = 0;
      syslog(LOG_ERR, "%s() Now only support linear-11 and linear-16 format\n",
            __func__);
      break;
    }

    if (*value < 0) {
      *value = 0;
    }

    close(fd);
    return 0;
  }
}

static int
read_hsc(uint8_t id, float *value) {
  if (id >= ARRAY_SIZE(hsc_dev_table)) {
    return ERR_SENSOR_NA;
  }

  int fd = 0, ret = 0;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t bus = hsc_dev_table[id].bus;
  uint8_t addr = hsc_dev_table[id].slv_addr;
  char key[MAX_KEY_LEN] = {0};
  char val[MAX_VALUE_LEN] = {0};
  uint8_t kvbuf[3] = {0};
  char* saveptr;
  int bufcnt = 0;

  snprintf(key, MAX_FRU_CMD_STR, "hsc-sensor%02x%c", id, '\0');
  ret = kv_get(key, val, NULL, 0);
  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get HSC info, errno=%d", __func__, errno);
    return -1;
  }

  char *kv_value;
  kv_value = strtok_r(val, "-", &saveptr);
  while( kv_value != NULL )
  {
    kvbuf[bufcnt] = atoi(kv_value);
    bufcnt++;
    kv_value = strtok_r(NULL, "-", &saveptr);
  }

  hsc_i2c_info hsc_i2c_data;
  memcpy(&hsc_i2c_data, &kvbuf, sizeof(hsc_i2c_data));

  fd = i2c_cdev_slave_open(bus, addr >> 1, I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_ERR, "%s() Failed to open I2C bus %d\n", __func__, bus);
    return -1;
  }

  //Get HSC reading
  uint8_t getreading_data = hsc_i2c_data.offset;
  uint8_t rlen = hsc_dev_list[hsc_i2c_data.hsc_type_cnt].hsc_cmd_list[hsc_i2c_data.hsc_cmd_cnt].read_byte;
  uint8_t rbuf[rlen];

  retry = SENSOR_RETRY_TIME;
  do {
    ret = i2c_rdwr_msg_transfer(fd, addr, (uint8_t *)&getreading_data, sizeof(getreading_data), rbuf, rlen);
    usleep(SENSOR_RETRY_INTERVAL_USEC);
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    close(fd);
    syslog(LOG_ERR, "%s() Failed to get reading %d-%d\n", __func__, bus, addr);
    return -1;
  }

  float m = hsc_dev_list[hsc_i2c_data.hsc_type_cnt].hsc_cmd_list[hsc_i2c_data.hsc_cmd_cnt].m;
  float b = hsc_dev_list[hsc_i2c_data.hsc_type_cnt].hsc_cmd_list[hsc_i2c_data.hsc_cmd_cnt].b;
  float r = hsc_dev_list[hsc_i2c_data.hsc_type_cnt].hsc_cmd_list[hsc_i2c_data.hsc_cmd_cnt].r;
  *value = ((float)(rbuf[1] << 8 | rbuf[0]) * r - b) / m;

  close(fd);
  return 0;
}

int
read_cpld_adc(uint8_t id, float *value) {
  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  if (id >= ARRAY_SIZE(cpld_adc_list)) {
    return ERR_SENSOR_NA;
  }

  int ret = 0, fd = 0;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t tlen = 1, rlen = 1;
  uint8_t tbuf = cpld_adc_list[id].lowbyte_cmd;
  uint8_t rbuf = 0;
  uint16_t sensor_read_raw = 0x0000;

  fd = i2c_cdev_slave_open(CPLD_BUS_4, CPLD_ADC_ADDR_BUS_4 >> 1,
                           I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_ERR, "Failed to open CPLD 0x%x\n", CPLD_ADC_ADDR_BUS_4);
    return -1;
  }

  //Get low byte
  retry = SENSOR_RETRY_TIME;
  do {
    ret = i2c_rdwr_msg_transfer(fd, CPLD_ADC_ADDR_BUS_4,
                                &tbuf, tlen, &rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get lowbyte reading %x-%x", __func__,
           CPLD_BUS_4, CPLD_ADC_ADDR_BUS_4);
    close(fd);
    return -1;
  } else {
    //Mask for getting bit[0-3]
    sensor_read_raw = rbuf & 0xF;
  }

  //Get high byte
  tbuf += 1;
  retry = SENSOR_RETRY_TIME;
  do {
    ret = i2c_rdwr_msg_transfer(fd, CPLD_ADC_ADDR_BUS_4,
                                &tbuf, tlen, &rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));
  close(fd);

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get highbyte reading %x-%x", __func__,
           CPLD_BUS_4, CPLD_ADC_ADDR_BUS_4);
    return -1;
  } else {
    //Mask for getting bit[4-11]
    sensor_read_raw += (rbuf << 4) & 0xFF0;
  }

  //Calculate from divided voltage
  *value = ((float)sensor_read_raw / 4096) * 1.825 * cpld_adc_list[id].resistor_ratio;

  return ret;
}

static int
read_ina230(uint8_t id, float *value) {
  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  int ret = 0, fd = 0;
  uint8_t bus = ina230_dev_table[id].bus;
  uint8_t addr = ina230_dev_table[id].addr;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t calibration = INA230_CALIBRATION;
  uint8_t tbuf = ina230_dev_table[id].offset;
  uint8_t tlen = sizeof(tbuf);
  uint8_t rbuf[2] = {0};
  uint8_t rlen = INA230_GET_DATA_LEN;
  int32_t read_ina230_raw;

  fd = i2c_cdev_slave_open(bus, addr >> 1, I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_ERR, "Failed to open ina230 0x%x\n", addr);
    return -1;
  }
  // 
  uint8_t config_data[3];
  config_data[0] = INA230_CONFIG;
  config_data[1] = LSB_INA230_CONFIG;
  config_data[2] = MSB_INA230_CONFIG;

  //default calibration date
  uint8_t default_calibration_data[3];
  default_calibration_data[0] = INA230_CALIBRATION;
  default_calibration_data[1] = LSB_INA230_DEFAULT_CALIBRATION;
  default_calibration_data[2] = MSB_INA230_DEFAULT_CALIBRATION;

  // Get INA230 Calibration
  do {
    ret = i2c_rdwr_msg_transfer(fd, addr, &calibration, sizeof(calibration),
                                rbuf, rlen);
    if (ret < 0) {
      syslog(LOG_ERR, "%s: get ina230 calibration failed", __func__);
      close(fd);
      return -1;
    }

    if (rbuf[0] == 0x00 && rbuf[1] == 0x00) {
      // Set config register to 0x274f
      ret = i2c_rdwr_msg_transfer(fd, addr, config_data,
                                  sizeof(config_data), NULL, 0);
      if (ret < 0) {
        syslog(LOG_ERR, "%s: write ina230 config failed", __func__);
        close(fd);
        return -1;
      }

      /* Write the config in the Calibration register */
      ret = i2c_rdwr_msg_transfer(fd, addr, default_calibration_data,
                                  sizeof(default_calibration_data), NULL, 0);
      if (ret < 0) {
        syslog(LOG_ERR, "%s: write ina230 default calibration failed", __func__);
        close(fd);
        return -1;
      }
      /* Wait for the conversion to finish */
      msleep(50);
      retry--;
    } else {
      break;
    }
  } while(retry);

  retry = SENSOR_RETRY_TIME;

  // Read ina230 power or voltage register
  do {
    ret = i2c_rdwr_msg_transfer(fd, addr, &tbuf, tlen, rbuf, rlen);
    if (ret != 0) {
      usleep(SENSOR_RETRY_INTERVAL_USEC);
    }
  } while ((ret < 0) && ((retry--) > 0));

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get ina230 reading %x-%x", __func__,
          bus, addr);
    close(fd);
    return -1;
  }

  switch(tbuf) {
    case INA230_POWER:
      // ina230 pwr
      read_ina230_raw = (rbuf[0] << 8) | rbuf[1];
      *value = ((float) read_ina230_raw) / 40;
      break;
    case INA230_IOUT:
      // ina230 IOUT
      read_ina230_raw = (rbuf[0] << 8) | rbuf[1];
      *value = ((float) read_ina230_raw) / 1000;
      break;
  }

  close(fd);
  return 0;
}

static int
read_nvme_temp(uint8_t id, float *value) {

  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  if (id >= ARRAY_SIZE(nvme_temp_list)) {
    return ERR_SENSOR_NA;
  }

  int ret = 0, fd = 0;
  uint8_t bus = nvme_temp_list[id];
  uint8_t addr = NVME_ADDR;
  uint8_t tbuf = NVME_GET_STATUS_CMD;
  uint8_t tlen = sizeof(tbuf);
  uint8_t rlen = NVME_GET_STATUS_LEN;
  uint8_t retry;
  uint8_t rbuf[NVME_GET_STATUS_LEN] = {0};

  fd = i2c_cdev_slave_open(bus, addr >> 1, I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_WARNING, "%s() Failed to open I2C bus %d\n", __func__, bus);
    return ERR_SENSOR_NA;
  }

  for (retry = SENSOR_RETRY_TIME; retry > 0; retry--) {
    ret = i2c_rdwr_msg_transfer(fd, addr, &tbuf, tlen, rbuf, rlen);
    if (ret >= 0) {
      break;
    }

    usleep(SENSOR_RETRY_INTERVAL_USEC);
  }

  close(fd);

  if (ret < 0) {
    return ERR_SENSOR_NA;
  }

  // valid temperature range: -60C(0xC4) ~ +127C(0x7F)
  // C4h-FFh is two's complement, means -60 to -1
  ret = nvme_temp_value_check((int)rbuf[NVME_TEMP_REG], value);
  if (ret == SNR_READING_SKIP) {
    ret = SENSOR_NA;
  }
  else if (ret != 0) {
    ret = ERR_SENSOR_NA;
  }

  return ret;
}

/* Check the valid range of NIC Temperature. */
static int
nic_temp_value_check(uint8_t value, float *value_check) {

  if (value_check == NULL) {
    syslog(LOG_ERR, "%s(): fail to check NIC temperature due to the NULL parameter.\n", __func__);
    return -1;
  }

  if (value <= MAX_NIC_TEMPERATURE) {
    *value_check = (float)value;
  } else {
    return -1;
  }

  return 0;
}

static int
read_nic_temp(uint8_t id, float *value) {

  if (value == NULL) {
    syslog(LOG_ERR, "%s: invalid parameter: value pointer is NULL", __func__);
    return -1;
  }

  int fd = 0, ret = -1;
  uint8_t bus = NIC_BUS;
  uint8_t addr = NIC_ADDR;
  uint8_t retry = SENSOR_RETRY_TIME;
  uint8_t tbuf = NIC_INFO_TEMP_CMD;
  uint8_t tlen = sizeof(tbuf);
  uint8_t rlen = NIC_TEMP_LEN;
  uint8_t rbuf = 0;
  static int nic_temp_retry = 0;

  fd = i2c_cdev_slave_open(bus, addr >> 1, I2C_SLAVE_FORCE_CLAIM);
  if (fd < 0) {
    syslog(LOG_WARNING, "%s() Failed to open I2C bus %d\n", __func__, bus);
    return ERR_SENSOR_NA;
  }

  for (retry = SENSOR_RETRY_TIME; retry > 0; retry--) {
    ret = i2c_rdwr_msg_transfer(fd, addr, &tbuf, tlen, &rbuf, rlen);
    if (ret == 0) {
      break;
    }

    usleep(SENSOR_RETRY_INTERVAL_USEC);
  }

  close(fd);

  if (ret < 0) {
    return ERR_SENSOR_NA;
  }

  ret = nic_temp_value_check(rbuf, value);

  // Temperature within valid range
  if (ret == 0) {
    nic_temp_retry = 0;
  } else {
    if (nic_temp_retry <= NIC_TEMP_RETRY_TIME) {
      ret = READING_SKIP;
      nic_temp_retry++;
    } else {
      ret = ERR_SENSOR_NA;
    }
  }

  if (ret < 0) {
    syslog(LOG_ERR, "%s() Failed to get reading %x-%x", __func__,
           NIC_BUS, NIC_ADDR);
    return -1;
  } else {
    *value = (float)rbuf;
  }

  return ret;
}

/**
*  @brief Function of reading sensor
*
*  @param fru: FRU ID
*  @param sensor_num: sensor number
*  @param *value: return value of sensor reading
*
*  @return Status of reading sensor
*  0: Success
*  -1: Failed
*  ERR_SENSOR_NA: Sensor NA error
**/
int
pal_sensor_read_raw(uint8_t fru, uint8_t sensor_num, void *value) {
  char key[MAX_KEY_LEN] = {0};
  char str[MAX_VALUE_LEN] = {0};
  char fru_name[32] = {0};
  int ret = 0;
  PAL_SENSOR_MAP sensor;

  if (pal_get_fru_name(fru, fru_name)) {
    syslog(LOG_WARNING, "%s() Fail to get fru%d name\n", __func__, fru);
    return ERR_SENSOR_NA;
  }

  snprintf(key, sizeof(key), "%s_sensor%d", fru_name, sensor_num);

  if (pal_is_fw_update_ongoing(fru)) {
    syslog(LOG_INFO, "snr_monitor: fru%d_fwupd detected, skip reading", fru);
    return ERR_VR_UPDATE_ONGOING;
  }

  switch(fru) {
  case FRU_SERVER:
    sensor = server_sensor_map[sensor_num];
    break;
  case FRU_BMC:
    sensor = bmc_sensor_map[sensor_num];
    break;
  case FRU_PDB:
    sensor = pdb_sensor_map[sensor_num];
    break;
  case FRU_FIO:
    sensor = fio_sensor_map[sensor_num];
    break;
  case FRU_NIC:
    sensor = nic_sensor_map[sensor_num];
    break;
  default:
    return ERR_SENSOR_NA;
  }

  // Check available sensor readings to avoid readings in wrong status
  if (sensor.reading_available == STBY_READING) {
    ret = sensor.read_sensor(sensor.id, (float * ) value);

  } else if (sensor.reading_available == POWER_ON_READING) {
    ret = kv_get(PWR_GOOD_KV_KEY, str, NULL, 0);
    if (ret < 0) {
      syslog(LOG_ERR, "%s: Failed to get gpio power good status in kv.", __func__);
      return ERR_SENSOR_NA;
    }

    if (strncmp(str, HIGH_STR, strlen(HIGH_STR)) == 0) {
      ret = sensor.read_sensor(sensor.id, (float * ) value);
    } else {
      ret = SENSOR_NA;
    }
  } else if (sensor.reading_available == POST_COMPLT_READING) {
    ret = kv_get(POST_CMPLT_KV_KEY, str, NULL, 0);
    if (ret < 0) {
      syslog(LOG_ERR, "%s: Failed to get post complete status in kv.", __func__);
      return ERR_SENSOR_NA;
    }

    if (strncmp(str, LOW_STR, strlen(LOW_STR)) == 0) {
      ret = sensor.read_sensor(sensor.id, (float * ) value);
    } else {
      ret = SENSOR_NA;
      retrySensor = 0;
    }
  } else {
    ret = ERR_SENSOR_NA;
  }

  memset(str, 0, sizeof(str));

  if (ret != 0) {
    kv_get(key, str, 0, 0);

    if (strcmp(str, "NA") != 0) {
      switch (ret) {
      case ERR_SENSOR_NA:
          syslog(LOG_WARNING,
                 "%s() Read sensor %s failed because of sensor NA error.\n",
                 __func__, sensor.snr_name);
          break;
      case ERR_UNKNOWN_FRU:
          syslog(LOG_WARNING,
                 "%s() Read sensor %s failed because of unknown FRU error.\n",
                 __func__, sensor.snr_name);
          break;
      case SENSOR_NA:
          break;
      case ERR_VR_UPDATE_ONGOING:
          break;
      default:
          syslog(LOG_WARNING,
                 "%s() Read sensor %s failed because of unknwon issue.\n",
                  __func__, sensor.snr_name);
          break;
      }
    }

    strncpy(str, "NA", sizeof(str));
  } else {
    snprintf(str, sizeof(str), "%.2f", *((float * ) value));
  }

  if (kv_set(key, str, 0, 0) < 0) {
    syslog(LOG_WARNING, "%s() cache_set key = %s, str = %s failed.\n", __func__, key, str);
    return ERR_SENSOR_NA;
  }

  return ret;
}

/**
*  @brief Function of getting sensor name
*
*  @param fru: FRU ID
*  @param sensor_num: sensor number
*  @param *name: return value of sensor name
*
*  @return Status of reading sensor name
*  0: Success
*  ERR_UNKNOWN_FRU: Unknown FRU error
**/
int
pal_get_sensor_name(uint8_t fru, uint8_t sensor_num, char *name) {
  switch(fru) {
  case FRU_SERVER:
    snprintf(name, MAX_SENSOR_NAME_SIZE, "%s", server_sensor_map[sensor_num].snr_name);
    break;
  case FRU_BMC:
    snprintf(name, MAX_SENSOR_NAME_SIZE, "%s", bmc_sensor_map[sensor_num].snr_name);
    break;
  case FRU_PDB:
    snprintf(name, MAX_SENSOR_NAME_SIZE, "%s", pdb_sensor_map[sensor_num].snr_name);
    break;
  case FRU_FIO:
    snprintf(name, MAX_SENSOR_NAME_SIZE, "%s", fio_sensor_map[sensor_num].snr_name);
    break;
  case FRU_NIC:
    snprintf(name, MAX_SENSOR_NAME_SIZE, "%s", nic_sensor_map[sensor_num].snr_name);
    break;
  default:
    return ERR_UNKNOWN_FRU;
  }

  return 0;
}

/**
*  @brief Function of getting sensor threshold
*
*  @param fru: FRU ID
*  @param sensor_num: sensor number
*  @param thresh: sensor threshold type
*  @param *value: return value of sensor threshold
*
*  @return Status of reading sensor threshold
*  0: Success
*  -1: Threshold type error
*  ERR_UNKNOWN_FRU: Unknown FRU error
**/
int
pal_get_sensor_threshold(uint8_t fru, uint8_t sensor_num, uint8_t thresh, void *value) {
  float *val = (float*) value;
  PAL_SENSOR_MAP * sensor_map = NULL;

  switch (fru) {
  case FRU_SERVER:
    sensor_map = server_sensor_map;
    break;
  case FRU_BMC:
    sensor_map = bmc_sensor_map;
    break;
  case FRU_PDB:
    sensor_map = pdb_sensor_map;
    break;
  case FRU_FIO:
    sensor_map = fio_sensor_map;
    break;
  case FRU_NIC:
    sensor_map = nic_sensor_map;
    break;
  default:
    return ERR_UNKNOWN_FRU;
  }

  switch(thresh) {
  case UCR_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.ucr_thresh;
    break;
  case UNC_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.unc_thresh;
    break;
  case UNR_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.unr_thresh;
    break;
  case LCR_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.lcr_thresh;
    break;
  case LNC_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.lnc_thresh;
    break;
  case LNR_THRESH:
    *val = sensor_map[sensor_num].snr_thresh.lnr_thresh;
    break;
  case POS_HYST:
    *val = sensor_map[sensor_num].snr_thresh.pos_hyst;
    break;
  case NEG_HYST:
    *val = sensor_map[sensor_num].snr_thresh.neg_hyst;
    break;
  default:
    syslog(LOG_WARNING, "%s() Threshold type error value=%d\n", __func__, thresh);
    return -1;
  }

  return 0;
}

/**
*  @brief Function of getting sensor units
*
*  @param fru: FRU ID
*  @param sensor_num: sensor number
*  @param *units: return value of sensor units
*
*  @return Status of reading sensor units
*  0: Success
*  ERR_UNKNOWN_FRU: Unknown FRU error
**/
int
pal_get_sensor_units(uint8_t fru, uint8_t sensor_num, char *units) {
  uint8_t sensor_units = 0;

  switch (fru) {
  case FRU_SERVER:
    sensor_units = server_sensor_map[sensor_num].units;
    break;
  case FRU_BMC:
    sensor_units = bmc_sensor_map[sensor_num].units;
    break;
  case FRU_PDB:
    sensor_units = pdb_sensor_map[sensor_num].units;
    break;
  case FRU_FIO:
    sensor_units = fio_sensor_map[sensor_num].units;
    break;
  case FRU_NIC:
    sensor_units = nic_sensor_map[sensor_num].units;
    break;
  default:
    return ERR_UNKNOWN_FRU;
  }

  switch(sensor_units) {
  case UNSET_UNIT:
    strcpy(units, "");
    break;
  case TEMP:
    sprintf(units, "C");
    break;
  case FAN:
    sprintf(units, "RPM");
    break;
  case PERCENT:
    sprintf(units, "%%");
    break;
  case VOLT:
    sprintf(units, "Volts");
    break;
  case CURR:
    sprintf(units, "Amps");
    break;
  case POWER:
    sprintf(units, "Watts");
    break;
  case FLOW:
    sprintf(units, "CFM");
    break;
  default:
    syslog(LOG_WARNING, "%s() unit not found, sensor number: %x, unit: %u\n", __func__, sensor_num, sensor_units);
    break;
  }

  return 0;
}

int
pal_get_sensor_poll_interval(uint8_t fru, uint8_t sensor_num, uint32_t *value)
{
  switch (fru)
  {
    case FRU_SERVER:
      *value = server_sensor_map[sensor_num].poll_invernal;
      break;
    case FRU_BMC:
      *value = bmc_sensor_map[sensor_num].poll_invernal;
      break;
    case FRU_PDB:
      *value = pdb_sensor_map[sensor_num].poll_invernal;
      break;
    case FRU_NIC:
      *value = nic_sensor_map[sensor_num].poll_invernal;
      break;
    default:
      return ERR_UNKNOWN_FRU;
  }

  return PAL_EOK;
}

static
int read_rpm(uint8_t id, float *value) {
  int rpm = 0;
  int ret = 0;

  ret = pal_get_fan_speed(id, &rpm);
  if (ret == 0) {
    *value = rpm;
    return ret;
  }

  if (retryFAN[id] < SENSOR_RETRY_TIME) {
    retryFAN[id]++;
    return SENSOR_NA;
  }

  retryFAN[id] = 0;
  return ERR_SENSOR_NA;
}
