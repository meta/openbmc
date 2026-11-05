// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>	/* for ARRAY_SIZE */
#include <linux/module.h>
#include <linux/i2c.h>
#include "i2c_dev_sysfs.h"

/*
 * FIXME: fill the below structure to export sysfs entries to the user
 * space.
 * NOTE: ONLY export register fields that are required from user space.
 */
static const i2c_dev_attr_st pwrcpld_attrs[] = {
	/*
	 * Example:
	{
		"cpld_ver",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x01,
		0,
		8,
	},
	 */
};

static const struct i2c_device_id pwrcpld_id[] = {
	{ "pwrcpld", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, pwrcpld_id);

static int pwrcpld_probe(struct i2c_client *client)
{
	i2c_dev_data_st *pdata;

	pdata = devm_kmalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, pdata);

	return devm_i2c_dev_sysfs_init(client, pdata, pwrcpld_attrs,
				       ARRAY_SIZE(pwrcpld_attrs));
}

static struct i2c_driver pwrcpld_driver = {
	.class    = I2C_CLASS_HWMON,
	.driver = {
		.name = "pwrcpld",
	},
	.probe    = pwrcpld_probe,
	.id_table = pwrcpld_id,
};

module_i2c_driver(pwrcpld_driver);

MODULE_AUTHOR("Tao Ren <taoren@meta.com>");
MODULE_DESCRIPTION("FBOSS OpenBMC Power-CPLD Driver");
MODULE_LICENSE("GPL");
