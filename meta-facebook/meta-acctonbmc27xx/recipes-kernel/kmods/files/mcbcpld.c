// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>	/* for ARRAY_SIZE */
#include <linux/module.h>
#include <linux/i2c.h>
#include "i2c_dev_sysfs.h"

#define intr_help_str                           \
  "0: Interrupt active\n"                       \
  "1: No interrupt"

#define enable_help_str                         \
  "0: Disable\n"                                \
  "1: Enable"

#define status_help_str                         \
  "0: Fail\n"                                   \
  "1: Normal"

static const i2c_dev_attr_st mcbcpld_attrs[] = {
  {
    "board_id",
    NULL,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x00, 0, 4,
  },
  {
    "version_id",
    NULL,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x00, 4, 3,
  },
  {
    "is_golden_image",
    "CPLD Configuration Image Flag\n"
    " 0: CPLD load external upgrade image\n"
    " 1: CPLD load internal golden image",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x01, 7, 1,
  },
  {
    "cpld_ver",
    NULL,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x01, 0, 7,
  },
  {
    "cpld_minor_ver",
    NULL,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x02, 0, 8,
  },
  {
    "cpld_sub_ver",
    "used for HW debug only",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x03, 0, 8,
  },
  {
    "software_scratch",
    "scratchpad for software use.\n"
    "shall reset default when got POR reset.\n"
    "default: 0xde",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x04, 0, 8,
  },
  {
    "cpld_self_cycle",
    "MCB Standby Power Self-lock\n"
    "0: MCB CPLD standby supply self-lock\n"
    "1: MCB CPLD normal standby supply",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x0f, 0, 1,
  },
  {
    "psu_left_on",
    enable_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x10, 0, 1,
  },
  {
    "psu_right_on",
    enable_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x10, 1, 1,
  },
  {
    "psu_left_smart_on",
    enable_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x10, 4, 1,
  },
  {
    "psu_right_smart_on",
    enable_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x10, 5, 1,
  },
  {
    "pdb_psu_left_alert_l",
    "0: PSU Alert\n"
    "1: PSU Normal",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 0, 1,
  },
  {
    "pdb_psu_right_alert_l",
    "0: PSU Alert\n"
    "1: PSU Normal",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 1, 1,
  },
  {
    "cpld_psu_left_pg",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 2, 1,
  },
  {
    "cpld_psu_right_pg",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 3, 1,
  },
  {
    "psu_left_ac_ok",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 4, 1,
  },
  {
    "psu_right_ac_ok",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 5, 1,
  },
  {
    "pdb_left_sensor_status",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 6, 1,
  },
  {
    "pdb_right_sensor_status",
    status_help_str,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x11, 7, 1,
  },
  {
    "pwr_come_en",
    "0: COMe power is off\n"
    "1: COMe power is on\n",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x14, 0, 1,
  },
  {
    "pwr_force_off",
    "0: COMe power is off\n"
    "1: COMe power is on\n",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x14, 1, 1,
  },
  {
    "pwr_come_cycle_n",
    "write 0 to trigger CPLD power cycling COMe\n"
    "then this bit will auto set to 1 after Power cycle finish",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x14, 2, 1,
  },
  {
    "pwr_come_cycle_dev_n",
    "write 0 to trigger CPLD power cycling the COMe + I210 + SSD\n"
    "then this bit will auto set to 1 after Power cycle finish",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x14, 3, 1,
  },
  {
    "timer_base_10ms",
    "default: 0",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x20, 0, 1,
  },
  {
    "timer_base_100ms",
    "default: 0",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x20, 1, 1,
  },
  {
    "timer_base_1s",
    "default: 1",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x20, 2, 1,
  },
  {
    "timer_base_10s",
    "default: 0",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x20, 3, 1,
  },
  {
    "timer_counter_setting",
    "This timer is used for power up automatically\n"
    "When couter down to zero, the power will repower up.",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x21, 0, 8,
  },
  {
    "timer_counter_state",
    NULL,
    I2C_DEV_ATTR_SHOW_DEFAULT,
    NULL,
    0x22, 0, 8,
  },
  {
    "power_cycle_go",
    "0: No power cycle\n"
    "1: Start the power cycle",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x23, 0, 1,
  },
  {
    "timer_counter_setting_update",
    "0: No update\n"
    "1: Update timer_base_setting, timer_couter_setting",
    I2C_DEV_ATTR_SHOW_DEFAULT,
    I2C_DEV_ATTR_STORE_DEFAULT,
    0x23, 1, 1,
  },
};

static const struct i2c_device_id mcbcpld_id[] = {
	{ "mcbcpld", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mcbcpld_id);

static int mcbcpld_probe(struct i2c_client *client)
{
	i2c_dev_data_st *pdata;

	pdata = devm_kmalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, pdata);

	return devm_i2c_dev_sysfs_init(client, pdata, mcbcpld_attrs,
				       ARRAY_SIZE(mcbcpld_attrs));
}

static struct i2c_driver mcbcpld_driver = {
	.class    = I2C_CLASS_HWMON,
	.driver = {
		.name = "mcbcpld",
	},
	.probe    = mcbcpld_probe,
	.id_table = mcbcpld_id,
};

module_i2c_driver(mcbcpld_driver);

MODULE_AUTHOR("Neal Chen <neal_chen@accton.com>");
MODULE_DESCRIPTION("MCBCPLD Driver");
MODULE_LICENSE("GPL");
