// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>	/* for ARRAY_SIZE */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include "i2c_dev_sysfs.h"

/*
 * NOTE: ONLY export register fields that are required from user space.
 */
static const i2c_dev_attr_st mcbcpld_attrs[] = {
	{
		"board_id",
		"Meta Networking system board type:\n"
		" 0x7 : Tahansb800bc\n"
		" 0x8 : Icecube\n"
		" 0x9 : Icetea\n"
		" 0xD : Ladakh800bcls\n"
		" 0xF : M4061CLSCL",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x0,
		0,
		4,
	},
	{
		"version_id",
		"Board revision",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x0,
		4,
		4,
	},
	{
		"cpld_ver",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x1,
		0,
		7,
	},
	{
		"golden_flag",
		"0: CPLD load external upgrade image\n"
		"1: CPLD load internal golden image",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x1,
		7,
		1,
	},
	{
		"cpld_minor_ver",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x2,
		0,
		8,
	},
	{
		"cpld_sub_ver",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x3,
		0,
		8,
	},
	{
		"iob_flash_wp_l",
		"Control IOB FPGA SPI flash write protect\n"
		"1: Not protect\n"
		"0: Protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x6,
		1,
		1,
	},
	{
		"timer_base_lsb",
		"PWR Cycle Timer Base Setting\n"
		" 0x01: 10ms\n"
		" 0x02: 100ms\n"
		" 0x04: 1s\n"
		" 0x08: 10s",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		/* I2C register 0x20 bit0 - bit3 for PWR Cycle Timer Base Setting */
		0x20,
		0,
		4,
	},
	{
		"timer_counter_setting",
		"This timer is used for power up automatically\n"
		"When counter down to zero, the power will repower up.",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		/* I2C register 0x21 for PWR Cycle Timer Counter Setting */
		0x21,
		0,
		8,
	},
	{
		"timer_counter_state",
		"The Timer counter current state",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		/* I2C register 0x22 for PWR Cycle Timer Counter State */
		0x22,
		0,
		8,
	},
	{
		"power_cycle_go",
		"0: No power cycle\n"
		"1: Start the power cycle",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		/* I2C register 0x23 bit0 for PWR Cycle Timer Misc Control-POWER_CYCLE_GO */
		0x23,
		0,
		1,
	},
	{
		"timer_counter_setting_update",
		"0: No update\n"
		"1: Update timer_base_setting 0x20, timer_counter_setting 0x21",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		/* I2C register 0x23 bit1 for PWR Cycle Timer Misc Control-TIMER_COUNTER_SETTING_UPDATE */
		0x23,
		1,
		1,
	}
};

static const struct i2c_device_id mcbcpld_id[] = {
	{ "mcbcpld", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, mcbcpld_id);

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 5, 0)
static int mcbcpld_probe(struct i2c_client *client)
#else
static int mcbcpld_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
#endif
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

MODULE_DESCRIPTION("FBOSS OpenBMC mcbcpld Driver");
MODULE_LICENSE("GPL");
