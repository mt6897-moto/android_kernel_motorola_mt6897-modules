// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "gate_i2c.h"

/*****************************************************************************
 * Define
 *****************************************************************************/
#define GATE_I2C_ID_NAME "gate_ic_i2c_sm5109"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :[GATE][I2C] " fmt, __func__, __LINE__

/*****************************************************************************
 * Define Register
 *****************************************************************************/
#define VPOS_BIAS			0x00
#define VNEG_BIAS			0x01

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
static const struct of_device_id _lcm_i2c_of_match[] = {
	{
		.compatible = "mediatek,i2c_lcd_bias",
	 },
	{}
};

static struct i2c_client *_lcm_i2c_client;

/*****************************************************************************
 * Driver Functions
 *****************************************************************************/

int _lcm_i2c_read_bytes(unsigned char addr, unsigned char *returnData)
{
	char cmd_buf[2] = { 0x00, 0x00 };
	char readData = 0;
	int ret = 0;
	struct i2c_client *client = _lcm_i2c_client;

	if (client == NULL) {
		pr_info("ERROR!! _lcm_i2c_client is null\n");
		return 0;
	}

	cmd_buf[0] = addr;
	ret = i2c_master_send(client, &cmd_buf[0], 1);
	ret = i2c_master_recv(client, &cmd_buf[1], 1);
	if (ret < 0)
		pr_info("ERROR %d!! i2c read data 0x%0x fail !!\n", ret, addr);

	readData = cmd_buf[1];
	*returnData = readData;

	return ret;
}
EXPORT_SYMBOL_GPL(_lcm_i2c_read_bytes);

int _lcm_i2c_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = _lcm_i2c_client;
	char write_data[2] = { 0 };

	if (client == NULL) {
		printk("ERROR!! _lcm_i2c_client is null\n");
		return 0;
	}

	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		printk("ERROR %d!! i2c write data fail 0x%0x, 0x%0x !!\n",
				ret, addr, value);

	return ret;
}
EXPORT_SYMBOL_GPL(_lcm_i2c_write_bytes);


void _lcm_i2c_panel_bias_enable(void)
{
	pr_info("%s+\n", __func__);

	/* set bias to 6v */
	_lcm_i2c_write_bytes(VPOS_BIAS, 0x12);
	_lcm_i2c_write_bytes(VNEG_BIAS, 0x12);
}
EXPORT_SYMBOL_GPL(_lcm_i2c_panel_bias_enable);

/*****************************************************************************
 * Function
 *****************************************************************************/

static int _lcm_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	printk("[LCM][I2C] _lcm_i2c_probe\n");
	printk("[LCM][I2C] NT: info==>name=%s addr=0x%x\n", client->name, client->addr);
	_lcm_i2c_client = client;

	return 0;
}

static void _lcm_i2c_remove(struct i2c_client *client)
{
	pr_info("%s+\n", __func__);

	_lcm_i2c_client = NULL;
	i2c_unregister_device(client);
}

/*****************************************************************************
 * Data Structure
 *****************************************************************************/

static const struct i2c_device_id _lcm_i2c_id[] = {
	{GATE_I2C_ID_NAME, 0},
	{}
};

static struct i2c_driver _lcm_i2c_driver = {
	.id_table = _lcm_i2c_id,
	.probe = _lcm_i2c_probe,
	.remove = _lcm_i2c_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = GATE_I2C_ID_NAME,
		   .of_match_table = _lcm_i2c_of_match,
		   },
};

module_i2c_driver(_lcm_i2c_driver);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("SM5109 BIAS Driver");
MODULE_LICENSE("GPL");


