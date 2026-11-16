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
static const i2c_dev_attr_st scmcpld_attrs[] = {
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
		"cpld_major_ver",
		"CPLD MAJOR Revision",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x1,
		0,
		7,
	},
	{
		"cpld_minor_ver",
		"CPLD MINOR Revision",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x2,
		0,
		8,
	},
	{
		"cb_sys_reset",
		"COMe System Warm Reset\n"
		"0: Reset\n"
		"1: Not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xf,
		5,
		1,
	},
	{
		"bios_post_cmplt_l",
		"0: Boot to OS Stage\n"
		"1: POST Code Stage",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x12,
		6,
		1,
	},
	{
		"pwr_come_en",
		"0: COMe power is off\n"
		"1: COMe power is on",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x14,
		0,
		1,
	},
	{
		"pwr_force_off",
		"0: COMe power is off\n"
		"1: COMe power is on",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x14,
		1,
		1,
	},
	{
		"pwr_cyc_n",
		"Write 0 to trigger CPLD power cycling COMe\n"
		"The bit will auto set to 1 after power cycle finishes",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x14,
		2,
		1,
	},
	{
		"spi_select",
		" 0x00: IOB FPGA FLASH (CS0)\n"
		" 0x01: BIOS (CS0)\n"
		" 0x0E: BIOS (CS1)\n"
		" 0x0F: IOB FPGA FLASH (CS0), BIOS (CS1)",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x34,
		0,
		8,
	},
	{
		"xp5p0_come_pg",
		" 0x00: Not good\n"
		" 0x01: Good\n",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x2a,
		5,
		1,
	},
	{
		"xp12p0_come_pg",
		" 0x00: Not good\n"
		" 0x01: Good\n",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x2a,
		6,
		1,
	},
	{
		"e1_s_ssd_prsnt0",
		" 0x00: not present\n"
		" 0x01: present",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0xb4,
		1,
		1,
	},

};

static const struct i2c_device_id scmcpld_id[] = {
	{ "scmcpld", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, scmcpld_id);

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 5, 0)
static int scmcpld_probe(struct i2c_client *client)
#else
static int scmcpld_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
#endif
{
	i2c_dev_data_st *pdata;

	pdata = devm_kmalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, pdata);

	return devm_i2c_dev_sysfs_init(client, pdata, scmcpld_attrs,
				       ARRAY_SIZE(scmcpld_attrs));
}

static struct i2c_driver scmcpld_driver = {
	.class    = I2C_CLASS_HWMON,
	.driver = {
		.name = "scmcpld",
	},
	.probe    = scmcpld_probe,
	.id_table = scmcpld_id,
};

module_i2c_driver(scmcpld_driver);

MODULE_DESCRIPTION("FBOSS OpenBMC scmcpld Driver");
MODULE_LICENSE("GPL");
