/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef _GATE_I2C_DRV_H_
#define _GATE_I2C_DRV_H_

enum led_i2c_type {
	LED_I2C_0 = 0,
	LED_I2C_6,
};

//extern int rt4831_read_byte(unsigned char cmd, unsigned char *returnData);
extern int _gate_ic_i2c_write_bytes(unsigned char cmd, unsigned char writeData,
				enum led_i2c_type i2c_type);
extern int _gate_ic_i2c_read_bytes(unsigned char cmd, unsigned char *returnData);
extern void _gate_ic_i2c_panel_bias_enable(unsigned int power_status);
extern void _gate_ic_Power_on(void);
extern void _gate_ic_Power_off(void);
extern int _gate_ic_backlight_set(unsigned int level);
//+Peridot-32,zhangshaoxiong1.wt,add,20240305,add kernel bias control
extern void _lcm_i2c_panel_bias_enable(void);
//-Peridot-32,zhangshaoxiong1.wt,add,20240305,add kernel bias control
//+Peridot-32,zhangshaoxiong1.wt,add,20240318,add kernel backlight control
extern int _backlight_i2c_write_bytes(unsigned char addr, unsigned char value);
extern int secong_backlight_i2c_write_bytes(unsigned char addr, unsigned char value);
extern void lcm_backlight_init(void);
extern void lcm_backlight_register_readback(void);
extern void lcm_second_backlight_init(void);
extern void lcm_second_backlight_register_readback(void);
//-Peridot-32,zhangshaoxiong1.wt,add,20240318,add kernel backlight control
int _gate_ic_i2c_type_backlight_set(unsigned int hw_level, enum led_i2c_type i2c_type);

#endif

