/* Copyright 2020-present Facebook. All Rights Reserved.
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
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include "netlakenext_gpio.h"


/* GPIO Expander gpio table */
gpio_cfg gpio_expander_gpio_table[] = {
  /* shadow_name, pin_name, direction, value */

  // LAST
  {NULL, NULL, GPIO_DIRECTION_INVALID, GPIO_VALUE_INVALID}
};

/* BMC gpio table */
gpio_cfg bmc_gpio_table[] = {
  /* shadow_name, pin_name, direction, value */

  // GPIOB0
  [FW_FLASH_SPI_RST0_N] =
  {"FW_FLASH_SPI_RST0_N", "GPIOB0", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOB2
  [FW_FLASH_SPI_RST1_N] =
  {"FW_FLASH_SPI_RST1_N", "GPIOB2", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOF3, CPU Thermal Trip
  [FM_THERMTRIP_R_N] =
  {"FM_THERMTRIP_R_N", "GPIOF3", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOF4, power good
  [PWRGD_PCH_R_PWROK] =
  {"PWRGD_PCH_R_PWROK", "GPIOF4", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOF7
  [JTAG_ISO_R_EN] =
  {"JTAG_ISO_R_EN", "GPIOF7", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOG0
  [UART_CH_SELECT] =
  {"UART_CH_SELECT", "GPIOG0", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOG1
  [UART_BMC_MUX_CTRL] =
  {"UART_BMC_MUX_CTRL", "GPIOG1", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOG2
  [OCP_DEBUG_PRSNT_N] =
  {"OCP_DEBUG_PRSNT_N", "GPIOG2", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOG3
  [FM_BATTERY_SENSE_EN] =
  {"FM_BATTERY_SENSE_EN", "GPIOG3", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOG7
  [BIOS_DEBUG_MSG_DIS_R_N] =
  {"BIOS_DEBUG_MSG_DIS_R_N", "GPIOG7", GPIO_DIRECTION_IN, GPIO_VALUE_HIGH},
  // GPIOH0, SMI
  [IRQ_SMI_ACTIVE_R_N] =
  {"IRQ_SMI_ACTIVE_R_N", "GPIOH0", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOH2, post complete
  [FM_BIOS_POST_CMPLT_R_N] =
  {"FM_BIOS_POST_CMPLT_R_N", "GPIOH2", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOH3, VR Hot
  [IRQ_PVCCIN_CPU_VRHOT_LVC3_R_N] =
  {"IRQ_PVCCIN_CPU_VRHOT_LVC3_R_N", "GPIOH3", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOM3, CPU Fail
  [CPU_ALERT_BMC_N] =
  {"CPU_ALERT_BMC_N", "GPIOM3", GPIO_DIRECTION_IN, GPIO_VALUE_HIGH},
  // GPION4
  [P5V_USB_EN] =
  {"P5V_USB_EN", "GPION4", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOO4
  [FAULT_LED] =
  {"FAULT_LED", "GPIOO4", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOP2, reset button
  [RST_BTN_N] =
  {"RST_BTN_N", "GPIOP2", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOP3, reset button by BMC control
  [RST_BTN_COME_R_N] =
  {"RST_BTN_COME_R_N", "GPIOP3", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOP4, power button
  [PWR_BTN_N] =
  {"PWR_BTN_N", "GPIOP4", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOP5 , power button by BMC control
  [PWR_BTN_COME_R_N] =
  {"PWR_BTN_COME_R_N", "GPIOP5", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOP0, rename from FM_BMC_READY_R_N to BMC_READY_N for common kcsd use
  [BMC_READY_N] =
  {"BMC_READY_N", "GPIOP0", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOQ1
  [MTP_BOARD_REV_ID0] =
  {"MTP_BOARD_REV_ID0", "GPIOQ1", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOQ2
  [MTP_BOARD_REV_ID1] =
  {"MTP_BOARD_REV_ID1", "GPIOQ2", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOQ3
  [MTP_BOARD_REV_ID2] =
  {"MTP_BOARD_REV_ID2", "GPIOQ3", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOQ4
  [JTAG1_MUX_SEL_R] =
  {"JTAG1_MUX_SEL_R", "GPIOQ4", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOQ6
  [SPI_MUX_SEL_R] =
  {"SPI_MUX_SEL_R", "GPIOQ6", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOQ7
  [IRQ_BMC_PCH_SMI_R_N] =
  {"IRQ_BMC_PCH_SMI_R_N", "GPIOQ7", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOR0
  [LED_POSTCODE_0] =
  {"LED_POSTCODE_0", "GPIOR0", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR1
  [LED_POSTCODE_1] =
  {"LED_POSTCODE_1", "GPIOR1", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR2
  [LED_POSTCODE_2] =
  {"LED_POSTCODE_2", "GPIOR2", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR3
  [LED_POSTCODE_3] =
  {"LED_POSTCODE_3", "GPIOR3", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR4
  [LED_POSTCODE_4] =
  {"LED_POSTCODE_4", "GPIOR4", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR5
  [LED_POSTCODE_5] =
  {"LED_POSTCODE_5", "GPIOR5", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR6
  [LED_POSTCODE_6] =
  {"LED_POSTCODE_6", "GPIOR6", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOR7
  [LED_POSTCODE_7] =
  {"LED_POSTCODE_7", "GPIOR7", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOS4
  [SPI_TPM_PP] =
  {"SPI_TPM_PP", "GPIOS4", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOV0
  [PWR_ID_LED] =
  {"PWR_ID_LED", "GPIOV0", GPIO_DIRECTION_OUT, GPIO_VALUE_LOW},
  // GPIOV3, CPU Throttle
  [FM_CPU_PROCHOT_LATCH_LVT3_R_N] =
  {"FM_CPU_PROCHOT_LATCH_LVT3_R_N", "GPIOV3", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOV5
  [FPGA_DEV_CLR_N] =
  {"FPGA_DEV_CLR_N", "GPIOV5", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOV6
  [FPGA_DEV_OE] =
  {"FPGA_DEV_OE", "GPIOV6", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOV7
  [FPGA_CONFIG_SEL] =
  {"FPGA_CONFIG_SEL", "GPIOV7", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOX6
  [BMC_FPGA_JTAG_EN] =
  {"BMC_FPGA_JTAG_EN", "GPIOX6", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOX7
  [BMC_TPM_SPI_PIRQ_N] =
  {"BMC_TPM_SPI_PIRQ_N", "GPIOX7", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // GPIOY2, DIMM Hot
  [H_MEMHOT_OUT_FET_R_N] =
  {"H_MEMHOT_OUT_FET_R_N", "GPIOY2", GPIO_DIRECTION_IN, GPIO_VALUE_INVALID},
  // GPIOY3
  [BMC_EMMC_RST_N] =
  {"BMC_EMMC_RST_N", "GPIOY3", GPIO_DIRECTION_OUT, GPIO_VALUE_HIGH},
  // LAST
  {NULL, NULL, GPIO_DIRECTION_INVALID, GPIO_VALUE_INVALID}
};

const char *
netlakenext_get_gpio_name(uint8_t gpio) {
  if (gpio < MAX_GPIO_PINS) {
    return bmc_gpio_table[gpio].shadow_name;
  } else {
    syslog(LOG_WARNING, "%s() Invalid gpio number: %u\n", __func__, gpio);
    return "";
  }
}
