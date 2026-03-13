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
#define SECOND_BACKLIGHT_I2C_ID_NAME "i2c_sgm37604a_2nd"

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " %s(%d) :[GATE][I2C] " fmt, __func__, __LINE__

/*****************************************************************************
 * Define Register
 *****************************************************************************/
#define BACKLIGHT_ENABLE			0x10
#define BRIGHTNESS_CONTOL			0x11
#define BRIGHTNESS_LSB			    0x1A
#define BRIGHTNESS_MSB			    0x19
#define LED_CURRENT			        0x1B

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
static const struct of_device_id _second_backlight_i2c_of_match[] = {
	{
		.compatible = "mediatek,i2c_lcd_second_backlight",
	 },
	{}
};

static struct i2c_client *second_backlight_i2c_client;

/*****************************************************************************
 * Driver Functions
 *****************************************************************************/

int second_backlight_i2c_read_bytes(unsigned char addr, unsigned char *returnData)
{
	char cmd_buf[2] = { 0x00, 0x00 };
	char readData = 0;
	int ret = 0;
	struct i2c_client *client = second_backlight_i2c_client;

	if (client == NULL) {
		pr_info("ERROR!! second_backlight_i2c_client is null\n");
		return 0;
	}

	cmd_buf[0] = addr;
	ret = i2c_master_send(client, &cmd_buf[0], 1);
	ret = i2c_master_recv(client, &cmd_buf[1], 1);
	if (ret < 0)
		pr_info("ERROR %d!! i2c read data 0x%0x fail !!\n", ret, addr);

	readData = cmd_buf[1];
	*returnData = readData;
	printk("readData is %d\n",readData);

	return ret;
}
EXPORT_SYMBOL_GPL(second_backlight_i2c_read_bytes);

int secong_backlight_i2c_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = second_backlight_i2c_client;
	char write_data[2] = { 0 };

	if (client == NULL) {
		printk("ERROR!! second_backlight_i2c_client is null\n");
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
EXPORT_SYMBOL_GPL(secong_backlight_i2c_write_bytes);

#if defined (WT_COMPILE_KERNEL_TARGET_PRODUCT_5G) || defined (WT_COMPILE_KERNEL_TARGET_PRODUCT_COMMERCIAL_WIFI)
void lcm_second_backlight_init(void)
{
	pr_info("%s+\n", __func__);

	/*register init*/
	secong_backlight_i2c_write_bytes(BACKLIGHT_ENABLE, 0x1F);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_CONTOL, 0x75);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_LSB, 0xFF);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_MSB, 0xE6);
	secong_backlight_i2c_write_bytes(LED_CURRENT, 0x00);
}
EXPORT_SYMBOL_GPL(lcm_second_backlight_init);
#else
void lcm_second_backlight_init(void)
{
	pr_info("%s+\n", __func__);

	/*register init*/
	secong_backlight_i2c_write_bytes(BACKLIGHT_ENABLE, 0x1F);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_CONTOL, 0x75);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_LSB, 0xF6);
	secong_backlight_i2c_write_bytes(BRIGHTNESS_MSB, 0xDB);
	secong_backlight_i2c_write_bytes(LED_CURRENT, 0x00);
}
EXPORT_SYMBOL_GPL(lcm_second_backlight_init);
#endif

void lcm_second_backlight_register_readback(void)
{
	unsigned char reg= 0;
	int ret = 0;
	
	pr_info("%s+\n", __func__);

	ret = second_backlight_i2c_read_bytes(BACKLIGHT_ENABLE, &reg);
	ret = second_backlight_i2c_read_bytes(BRIGHTNESS_CONTOL, &reg);
	ret = second_backlight_i2c_read_bytes(BRIGHTNESS_LSB, &reg);
	ret = second_backlight_i2c_read_bytes(BRIGHTNESS_MSB, &reg);
	ret = second_backlight_i2c_read_bytes(LED_CURRENT, &reg);
}
EXPORT_SYMBOL_GPL(lcm_second_backlight_register_readback);

/*****************************************************************************
 * Function
 *****************************************************************************/

static int second_backlight_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	printk("[LCM][I2C] _backlight_i2c_probe\n");
	printk("[LCM][I2C] NT: info==>name=%s addr=0x%x\n", client->name, client->addr);
	second_backlight_i2c_client = client;

	return 0;
}

static void second_backlight_i2c_remove(struct i2c_client *client)
{
	pr_info("%s+\n", __func__);

	second_backlight_i2c_client = NULL;
	i2c_unregister_device(client);
}

/*****************************************************************************
 * Data Structure
 *****************************************************************************/

static const struct i2c_device_id second_backlight_i2c_id[] = {
	{SECOND_BACKLIGHT_I2C_ID_NAME, 0},
	{}
};

static struct i2c_driver second_backlight_i2c_driver = {
	.id_table = second_backlight_i2c_id,
	.probe = second_backlight_i2c_probe,
	.remove = second_backlight_i2c_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = SECOND_BACKLIGHT_I2C_ID_NAME,
		   .of_match_table = _second_backlight_i2c_of_match,
		   },
};

module_i2c_driver(second_backlight_i2c_driver);

MODULE_AUTHOR("Mediatek Corporation");
MODULE_DESCRIPTION("SGM37604A SECOND_BACKLIGHT Driver");
MODULE_LICENSE("GPL");


