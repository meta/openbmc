/* Copyright 2020-present Facebook. All Rights Reserved.
 *
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

#ifndef __NETLKAE_GPIO_H__
#define __NETLAKE_GPIO_H__

#include <stdint.h>
#include <openbmc/libgpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gpio_info {
  char *shadow_name;
  char *pin_name;
  gpio_direction_t direction;
  gpio_value_t value;
} gpio_cfg;

// GPIO Expander GPIO pins
enum {
  MAX_GPIO_EXPANDER_GPIO_PINS
};

// BMC GPIO pins
// Essential Pins for talking to x86 CPU includes : 
// PCH related pins : PWRGD_PCH_R_PWROK
// CPU itself  :
//               PWRGD_PCH_R_PWROK
//               FM_THERMTRIP_R_N
//               FM_BIOS_POST_CMPLT_R_N
//               CPU_ALERT_BMC_N
//               FM_CPU_PROCHOT_LATCH_LVT3_R_N
//               BIOS_DEBUG_MSG_DIS_R_N
//
// COMe itself :
//               IRQ_PVCCIN_CPU_VRHOT_LVC3_R_N
//               H_MEMHOT_OUT_FET_R_N

enum {
  FW_FLASH_SPI_RST0_N = MAX_GPIO_EXPANDER_GPIO_PINS,
  FW_FLASH_SPI_RST1_N,
  FM_THERMTRIP_R_N,
  PWRGD_PCH_R_PWROK,
  JTAG_ISO_R_EN,
  UART_CH_SELECT,
  UART_BMC_MUX_CTRL,
  OCP_DEBUG_PRSNT_N,
  FM_BATTERY_SENSE_EN,
  BIOS_DEBUG_MSG_DIS_R_N,
  IRQ_SMI_ACTIVE_R_N,
  FM_BIOS_POST_CMPLT_R_N,
  IRQ_PVCCIN_CPU_VRHOT_LVC3_R_N,
  CPU_ALERT_BMC_N,
  P5V_USB_EN,
  FAULT_LED,
  RST_BTN_N,
  RST_BTN_COME_R_N,
  PWR_BTN_N,
  PWR_BTN_COME_R_N,
  BMC_READY_N,
  MTP_BOARD_REV_ID0,
  MTP_BOARD_REV_ID1,
  MTP_BOARD_REV_ID2,
  JTAG1_MUX_SEL_R,
  SPI_MUX_SEL_R,
  IRQ_BMC_PCH_SMI_R_N,
  LED_POSTCODE_0,
  LED_POSTCODE_1,
  LED_POSTCODE_2,
  LED_POSTCODE_3,
  LED_POSTCODE_4,
  LED_POSTCODE_5,
  LED_POSTCODE_6,
  LED_POSTCODE_7,
  SPI_TPM_PP,
  PWR_ID_LED,
  FM_CPU_PROCHOT_LATCH_LVT3_R_N,
  FPGA_DEV_CLR_N,
  FPGA_DEV_OE,
  FPGA_CONFIG_SEL,
  BMC_FPGA_JTAG_EN,
  BMC_TPM_SPI_PIRQ_N,
  H_MEMHOT_OUT_FET_R_N,
  BMC_EMMC_RST_N,
  MAX_GPIO_PINS
};

extern gpio_cfg gpio_expander_gpio_table[];
extern gpio_cfg bmc_gpio_table[];
const char * netlakenext_get_gpio_name(uint8_t gpio);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __NETLAKENEXT_GPIO_H__ */
