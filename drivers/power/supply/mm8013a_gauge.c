// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include "mtk_battery.h"
#include "charger_class.h"
#include "mtk_charger.h"
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/hardware_info.h>

static bool dbg_log_en = true;
module_param(dbg_log_en, bool, 0644);
#define mmc_dbg(dev, fmt, ...) \
	do { \
		if (dbg_log_en) \
			dev_info(dev, "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

#define DRIVER_VERSION			"1.0.0"
#define REG_CTRL_0			0x00
#define REG_TEMPERATURE			0x06
#define REG_VOLTAGE			0x08
#define REG_CURRENT			0x14
#define REG_CURRENT_NOW		0x72
#define REG_RSOC			0x2c
#define REG_BLOCKDATAOFFSET		0x3e
#define REG_BLOCKDATA			0x40
#define REG_CYCLECOUNT			0x2a
#define REG_CHARGEVOLT			0x30
#define REG_HEALTHD_STATUS		0x2E
#define REG_PRODUCT_DATE		0x00F2
#define BATTERY_QMAX			10200
#define _BATTERY_CV			4500
//+Peridot-422, liyiying.wt, 20240222, add, add batt id
#define REG_PACK_ID			0x10
#define BATT_SUNWODA		0x0101
#define BATT_SCUD			0x0102
#define BATT_NVT				0x0103
#define BATT_SUNWODA_NAME	"SUNWODA_LI-ION_10200mAh"
#define BATT_SCUD_NAME		"SCUD_LI-ION_10200mAh"
#define BATT_NVT_NAME		"NVT_LI-ION_10200mAh"
#define BATT_UNKNOW_NAME	"UNKNOW_BATT"
//-Peridot-422, liyiying.wt, 20240222, add, add batt id
//+Peridot-35,xiaohongyu,wt,add,20240223,add GAUGE message in factory mode
#define GAUGE_NAME "MM8013"
//-Peridot-35,xiaohongyu,wt,add,20240223,add GAUGE message in factory mode

#define MM8013C_DEFAULT_SOC_VALUE		51
#define MM8013C_DEFAULT_TEMP_VALUE		260
#define MM8013C_DEFAULT_VOLAGE_VALUE		3900
#define MM8013C_DEFAULT_CURRENT_VALUE		500

#define BAT_CALI_DEVNAME "MT_pmic_adc_cali"
#define Get_META_BAT_VOL _IOW('k', 10, int)
#define Get_META_BAT_SOC _IOW('k', 11, int)
#define Get_META_BAT_CAR_TUNE_VALUE _IOW('k', 12, int)
#define Set_META_BAT_CAR_TUNE_VALUE _IOW('k', 13, int)
#define Set_BAT_DISABLE_NAFG _IOW('k', 14, int)
#define Set_CARTUNE_TO_KERNEL _IOW('k', 15, int)

static struct class *bat_cali_class;
static int bat_cali_major;
static dev_t bat_cali_devno;
static struct cdev *bat_cali_cdev;

static int get_to_full_now(void);
static int get_chg_type(void);

struct battery_info {
	int status;
	int health;
	int present;
	int technology;
	int cycle_count;
	int capacity;
	int current_now;
	int current_avg;
	int voltage_now;
	int charger_full;
	int charger_counter;
	int battery_temp;
	int capacoty_level;
	int time_to_full_now;
	int charger_full_design;
	int constant_charge_voltage;
};

struct mm8013_chip {
	struct device *dev;
	struct platform_device *pdev;
	struct i2c_client *client;
	struct power_supply_desc battery;
	struct power_supply *mm8013_psy;
	struct power_supply *battery_psy;
	struct power_supply *chg_psy;
	struct power_supply *mtk_master_charger_psy;
	struct battery_info bat_data;
	struct delayed_work work;
	struct delayed_work monitor_work;
	bool is_probe_done;
	//+Peridot-422, liyiying.wt, 20240222, add, add batt id
	struct mutex i2c_rw_lock;
	//-Peridot-422, liyiying.wt, 20240222, add, add batt id
	struct mutex meta_rw_lock;

	int bat_status;
	int batt_soc;
	int batt_volt;
	int batt_uisoc;
	int val_current;
	int pre_time;
	int vbus_type;
	int uisoc_update_time;
	int monitor_work_time;
};

struct mm8013_chip *chip;
bool has_8013;
int test_val_cur, test_val_cur_now;

//+Peridot-422, liyiying.wt, 20240222, add, add batt id
static int mm8013_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;
	struct mm8013_chip *chip_m8013 = i2c_get_clientdata(client);

	mutex_lock(&chip_m8013->i2c_rw_lock);
	mdelay(4);
	ret = i2c_smbus_write_word_data(client, reg, value);
	if (ret < 0) {
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
	}
	mutex_unlock(&chip_m8013->i2c_rw_lock);

	return ret;
}
//-Peridot-422, liyiying.wt, 20240222, add, add batt id

static int mm8013_read_reg(struct i2c_client *client, u8 reg)
{
	int ret = 0;
	struct mm8013_chip *chip_m8013 = i2c_get_clientdata(client);

	mutex_lock(&chip_m8013->i2c_rw_lock);
	mdelay(4);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		dev_info(&client->dev, "%s: err %d\n", __func__, ret);
	mutex_unlock(&chip_m8013->i2c_rw_lock);

	return ret;
}

int mm8013_soc(int *val)
{
	int soc = 0;
	int count = 3;

	while (count--) {
		if (has_8013) {
			soc = mm8013_read_reg(chip->client, REG_RSOC);
			if (soc >= 0) {
				*val = soc;
				break;
			}
			mdelay(10);
		} else {
			*val = MM8013C_DEFAULT_SOC_VALUE;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mm8013_soc);

int mm8013_voltage(int *val)
{
	int volt = 0;
	int count = 3;

	while (count--) {
		if (has_8013) {
			volt = mm8013_read_reg(chip->client, REG_VOLTAGE);
			if (volt >= 0) {
				*val = volt;
				break;
			}
			mdelay(10);
		} else {
			*val = MM8013C_DEFAULT_VOLAGE_VALUE;
		}
	}

	return 0;
}
EXPORT_SYMBOL(mm8013_voltage);

int mm8013_current(int *val)
{
	int curr = 0;
	int count = 3;

	while (count--) {
		if (has_8013) {
			curr = mm8013_read_reg(chip->client, REG_CURRENT);
			if (curr >= 0) {
				if (curr > 32767)
					curr -= 65536;
				*val = curr * 10;
				break;
			}
			mdelay(10);
		} else {
			*val = MM8013C_DEFAULT_CURRENT_VALUE;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mm8013_current);

int mm8013_current_now(int *val)
{
	int curr = 0;
	int count = 3;

	while (count --) {
		if (has_8013) {
			curr = mm8013_read_reg(chip->client, REG_CURRENT_NOW);
			if (curr >= 0) {
				if (curr > 32767)
					curr -= 65536;
				*val = curr * 10;
				break;
			}
			mdelay(10);
		} else {
			*val = MM8013C_DEFAULT_CURRENT_VALUE;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mm8013_current_now);

int mm8013_temperature(int *val)
{
	int temp = 0;
	int count = 3;

	while (count--) {
		if (has_8013) {
			temp = mm8013_read_reg(chip->client, REG_TEMPERATURE);
			if (temp >= 0) {
				*val = temp - 2731;
				break;
			}
			mdelay(10);
		} else
			*val = MM8013C_DEFAULT_TEMP_VALUE;
	}

	return 0;
}
EXPORT_SYMBOL(mm8013_temperature);

//+Peridot-7655, liyiying.wt, add, 20240515, add battery_healthd node
static int mm8013_read_healthd_status(void)
{
	int ret = 50;
	int count = 3;

	while (count--) {
		if (has_8013) {
			ret = mm8013_read_reg(chip->client, REG_HEALTHD_STATUS);
			if (ret >= 0) {
				ret = ret + 2;
				if (ret >= 100)
					ret = 100;
				return ret;
			}
			mdelay(10);
		}
	}

	if (ret < 0) {
		ret = 50;
	}

	return ret;
}
//-Peridot-7655, liyiying.wt, add, 20240515, add battery_healthd node

static int mm8013_checkdevice(void)
{
	int ret = 0;
	int count = 3;

	while (count--) {
		//+PERIDOT-658, liyiying.wt, 20240403, mod, display negative charge
		ret = mm8013_read_reg(chip->client, REG_RSOC);
		if (ret > 0) {
			pr_err("%s success\n", __func__);
			break;
		} else {
			pr_err("%s fail: %d\n", __func__, count);
		}
		mdelay(10);
		//-PERIDOT-658, liyiying.wt, 20240403, mod, display negative charge
	}

	return ret;
}

//+Peridot-422, liyiying.wt, 20240222, add, add batt id
static int mm8013_read_batt_id(void)
{
	int ret;

	ret = mm8013_write_reg(chip->client, 0x0, 0x0008);
	if (ret < 0) {
		pr_err("%s write reg err\n", __func__);
		return ret;
	}

	ret = mm8013_read_reg(chip->client, 0x0);
	if (ret < 0) {
		pr_err("%s read reg err\n", __func__);
		return ret;
	} else {
		pr_err("%s batt id is %x\n", __func__, ret);
	}

	return ret;
}
//-Peridot-422, liyiying.wt, 20240222, add, add batt id

//+Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0
static int mm8013_read_cyclecount(int *val)
{
	int i = 4, count = 0;

	while (i--){
		count = mm8013_read_reg(chip->client, REG_CYCLECOUNT);
		if (count < 0) {
			pr_err("%s batt cyclecount read error ret is %x, i is %d\n", __func__, count, i);
			mdelay(5);
		} else {
			break;
		}
	}

	if (count < 0) {
		return count;
	} else {
		*val = count;
		pr_err("[%s] batt cyclecount read success ret is %x\n", __func__, count);
	}

	return 0;
}

static int mm8013_read_charge_vol(int *val)
{
	int i = 4, vol = 0;

	while (i--) {
		vol = mm8013_read_reg(chip->client, REG_CHARGEVOLT);
		if (vol < 0) {
			pr_err("%s batt vol read error ret is %x, i is %d\n", __func__, vol, i);
			mdelay(5);
		} else {
			break;
		}
	}

	if (vol < 0) {
		return vol;
	} else {
		*val = vol;
	}

	return 0;
}

static int mm8013_write_charge_vol(int val)
{
	int i = 4, ret, ret_;
	int _val = 0;

	while (i--) {
		ret = mm8013_write_reg(chip->client, REG_CHARGEVOLT, val);
		if (ret < 0) {
			pr_err("%s batt write vol error ret is %d, i is %d\n", __func__, val, i);
			mdelay(5);
		}

		mdelay(100);

		ret_ = mm8013_write_reg(chip->client, REG_CHARGEVOLT, val);
		if (ret_ < 0) {
			pr_err("%s batt write vol error ret is %d, i is %d\n", __func__, val, i);
			mdelay(5);
		}

		if (ret >= 0 && ret >= 0) {
			pr_err("%s batt write vol success ret is %d\n", __func__, val);
			mm8013_read_charge_vol(&_val);
			pr_err("%s batt read vol is %d\n", __func__, _val);
			break;
		}
	}

	if(ret < 0) {
		return ret;
	} else if (ret_ < 0) {
		return ret_;
	}

	return 0;
}
//-Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0

//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
static int mm8013_i2c_bulk_write(struct i2c_client *client, u8 reg, u8 *data, int len)
{
	int ret = 0;
	u8 buf[33];
	struct i2c_msg msg;

	if (!client->adapter) {
		dev_err(chip->dev, "%s err!!!", __func__);
		return -ENODEV;
	}

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	msg.buf = buf;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len + 1;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		return ret;
	}
	if (ret != 1) {
		return -EINVAL;
	}

	return 0;
}

static int mm8013_i2c_bulk_read(struct i2c_client *client, u8 reg, u8 *data, int len)
{
	int ret;

	if (!client->adapter) {
		return -ENODEV;
	}

	ret = i2c_smbus_read_i2c_block_data(chip->client, reg, len,data);
	if (ret < 0) {
		return ret;
	}
	if (ret != len) {
		return -EINVAL;
	}
	return 0;
}

static int mm8013_read_product_date(void)
{
	int ret;
	u8 data[32];
	int date;
	u32 unseal_code;
	/*
	* Send Unseal code[0]
	* Write command 0x00
	* and Unseal code data0
	*/
	unseal_code = 0x56781234;
	unseal_code &= 0xFFFFFFFFL;

	data[0] = (u8)(unseal_code & 0xFF);
	data[1] = (u8)((unseal_code >> 8) & 0xFF);
	ret = mm8013_i2c_bulk_write(chip->client, 0x00, data,  2);
	if (ret < 0) {
		dev_err(chip->dev, "%s err: %d!!! - 1", __func__, ret);
		return -1;
	}

	/*
	* Send Unseal code[1]
	*   Write command 0x00
	*   and Unseal code data1
	*/
	data[0] = (u8)((unseal_code >> 16) & 0xFF);
	data[1] = (u8)((unseal_code >> 24) & 0xFF);
	ret = mm8013_i2c_bulk_write(chip->client, 0x00, data, 2);
	if (ret < 0) {
		dev_err(chip->dev, "%s err: %d!!! - 2", __func__, ret);
		return -1;
	}

	/*
	* User NVM Write Setting
	*   Write command 0x61
	*   and Data 0x00
	*/
	data[0] = (u8)0x00;
	ret = mm8013_i2c_bulk_write(chip->client, 0x61, data, 1);
	if (ret < 0) {
		dev_err(chip->dev, "%s err: %d!!! - 3", __func__, ret);
		return -1;
	}

	/*
	* Manufacture A/B Request
	*   Write command 0x3E
	*   and Data 0x00F2/0x00F3
	*/
	data[0] = (u8)(REG_PRODUCT_DATE & 0xFF);
	data[1] = (u8)((REG_PRODUCT_DATE >> 8) & 0xFF);
	ret = mm8013_i2c_bulk_write(chip->client, 0x3E, data, 2);
	if (ret < 0) {
		dev_err(chip->dev, "%s err: %d!!! - 4", __func__, ret);
		return -1;
	}

	/*
	* Read Manufacture A/B Request
	*   Read command 0x40
	*   and get 32bytes data
	*/
	ret = mm8013_i2c_bulk_read(chip->client, 0x40, data, 32);
	if (ret < 0) {
		dev_err(chip->dev, "%s err: %d!!! - 6", __func__, ret);
		return -1;
	}

	date = data[0] & 0xFF;		/* Day:	  [0] */
	date |= (data[1] & 0xFF) << 8;	/* Month: [1] */
	date |= (data[2] & 0xFF) << 16;	/* Year:  [2] */

	/*
	* Seal Set Request
	*   Write command 0x00
	*   and Data 0x0020
	*   -> wait 100msec
	*/
	data[0] = (u8)0x20;
	data[1] = (u8)0x00;
	ret = mm8013_i2c_bulk_write(chip->client, 0x00, data, 2);
	msleep(100);
	if (ret < 0) {
		return -1;
	}

	return date;
}
#endif
//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel

#define BATT_CAPICITY_ZERO	0
#define BATT_CAPICITY_LOW		1
#define BATT_CAPICITY_HIGH	96
#define BATT_FULL_100 	100
#define UISOC_BATT_PROTECT_MODE	60
#define BATT_CAPACITY_MAX		10000
#define UISOC_PE50_CHARGE_TIME	35
#define UISOC_PD20_CHARGE_TIME	90 //130
#define UISOC_QC20_CHARGE_TIME	90 //130
#define UISOC_DEFAULT_CHARGE_TIME	120 //180
#define UISOC_DEFAULT_DISCHARGE_TIME	120
#define UISOC_TRACE_TO_FULL_TIME	60
#define UISOC_KEEP_100_CURRENT_MAX	1500000
#define UISOC_CHARGER_DIFF_MAX	2
#define UISOC_DIFF_MAX	5

#define PE50_MONITOR_WORK_TIME	3
#define PD20_MONITOR_WORK_TIME	5
#define DISCHARGE_MONITOR_WORK_TIME	5

#define CHARGE_SHUTDOWN_VOLTAGE		3350
#define DISCHARGE_SHUTDOWN_VOLTAGE		3380

static int mm8013_get_sys_time(void)
{
	struct rtc_time tm_android = {0};
	struct timespec64 tv_android = {0};
	int timep = 0;

	ktime_get_real_ts64(&tv_android);
	rtc_time64_to_tm(tv_android.tv_sec, &tm_android);
	tv_android.tv_sec -= (uint64_t)sys_tz.tz_minuteswest * 60;
	rtc_time64_to_tm(tv_android.tv_sec, &tm_android);
	timep = tm_android.tm_sec + tm_android.tm_min * 60 + tm_android.tm_hour * 3600;

	return timep;
}

static int mm8013_get_uisoc(struct mm8013_chip *chip)
{
	int ret = 0;
	int input_current;
	int time_now, interval_time;
	int curr_update_time = 0;
	int charger_update_time;
	struct mtk_charger *info = NULL;
	struct power_supply *chg_psy = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (IS_ERR_OR_NULL(chg_psy)) {
		dev_err(chip->dev, "%s: fail to get chg_psy\n", __func__);
		return ret;
	} else {
		info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
		if (IS_ERR_OR_NULL(chg_psy)) {
			dev_err(chip->dev, "%s: fail to get charger drvdate\n", __func__);
			return ret;
		}
	}

	ret = charger_dev_get_input_current(info->chg1_dev, &input_current);
	if (ret == (-EOPNOTSUPP)) {
		dev_err(chip->dev, "%s: fail to get aicl\n", __func__);
		return ret;
	}

	//get rtc time and calculate interval time
	time_now = mm8013_get_sys_time();
	if (chip->vbus_type != info->chr_type) {
		dev_info(chip->dev, "charger insert/remove:  insert_time(pre: %d, now: %d)\n",
			chip->pre_time, time_now);
		chip->pre_time = time_now;
	}
	chip->vbus_type = info->chr_type;
	interval_time = abs(time_now - chip->pre_time);

	//battery capacity smooth handling
	if (info->chr_type != POWER_SUPPLY_TYPE_UNKNOWN) {
		if (info->pe5_online == true) {
			charger_update_time = UISOC_PE50_CHARGE_TIME;
			chip->monitor_work_time = PE50_MONITOR_WORK_TIME;
		} else if (info->pd_online == true) {
			charger_update_time = UISOC_PD20_CHARGE_TIME;
			chip->monitor_work_time = PD20_MONITOR_WORK_TIME;
		} else if (info->qc20_online == true) {
			charger_update_time = UISOC_QC20_CHARGE_TIME;
			chip->monitor_work_time = PD20_MONITOR_WORK_TIME;
		} else {
			charger_update_time = UISOC_DEFAULT_CHARGE_TIME;
			chip->monitor_work_time = PD20_MONITOR_WORK_TIME;
		}

	#if 0
		curr_update_time = 3600 /((chip->val_current * 10) /BATT_CAPACITY_MAX);
		curr_update_time = min(curr_update_time, UISOC_DEFAULT_CHARGE_TIME);
		if (abs(chip->batt_soc - chip->batt_uisoc) >= UISOC_CHARGER_DIFF_MAX) {
			curr_update_time = charger_update_time;
		}
	//#else
		//uisoc increase slower, when uisoc close to 100
		if (chip->batt_uisoc >= BATT_CAPICITY_HIGH) {
			curr_update_time = charger_update_time +
				(chip->batt_uisoc - BATT_CAPICITY_HIGH) *
				(charger_update_time /(BATT_FULL_100 - BATT_CAPICITY_HIGH))
		}
	#endif
		chip->uisoc_update_time = max(curr_update_time, charger_update_time);

		if (chip->batt_soc > chip->batt_uisoc) {
		//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
			#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
			if ((info->batt_protection_a == 1) &&
				(chip->batt_uisoc >= info->batt_protection_ccc) &&
				(info->running_batt_protection == true)) {
				if (interval_time >= chip->uisoc_update_time) {
					chip->pre_time = time_now;
				}
				goto charging_update_exit;
			}
			#endif
			if ((info->charging_enabled_user_control == 0) &&
				(chip->batt_uisoc >= 60)) {
				if (interval_time >= chip->uisoc_update_time) {
					chip->pre_time = time_now;
				}
                                pr_err("charging disable for protection");
				goto charging_update_exit;
			}
			//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
			//uisoc keep 100, when charge done status
			if ((info->is_chg_done ||info->pe5_is_chg_done) &&
				(input_current >= UISOC_KEEP_100_CURRENT_MAX) &&
				(info->setting.cv !=info->data.jeita_temp_t3_to_t4_cv)) {
				chip->uisoc_update_time = UISOC_TRACE_TO_FULL_TIME;
				if (chip->batt_uisoc < BATT_FULL_100) {
					if (interval_time >= chip->uisoc_update_time) {
						chip->batt_uisoc = chip->batt_uisoc + 1;
						chip->pre_time = time_now;
						goto charging_update_exit;
					}
				}
			}

			if (interval_time >= chip->uisoc_update_time) {
				chip->batt_uisoc = chip->batt_uisoc + 1;
				chip->pre_time = time_now;
			}
		} else if (chip->batt_soc < chip->batt_uisoc) {
			//uisoc keep 100, when charge done status
			if ((info->is_chg_done ||info->pe5_is_chg_done ||
				(info->charging_enabled_user_control != true &&
				chip->batt_uisoc == UISOC_BATT_PROTECT_MODE)) &&
				(input_current >= UISOC_KEEP_100_CURRENT_MAX) &&
				(abs(chip->batt_uisoc - chip->batt_soc) < UISOC_DIFF_MAX) &&
				(info->setting.cv !=info->data.jeita_temp_t3_to_t4_cv)) {
				goto charging_update_exit;
			}

			//uisoc keep 1%, when battery voltage too high
			if (chip->batt_uisoc <= BATT_CAPICITY_LOW) {
				if (chip->batt_soc == BATT_CAPICITY_ZERO &&
					chip->batt_volt >= CHARGE_SHUTDOWN_VOLTAGE) {
					chip->batt_uisoc = BATT_CAPICITY_LOW;
					goto charging_update_exit;
				}
			}

			//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
			#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
			if ((info->batt_recharge_setting == 95 ||
				info->batt_recharge_setting == 76) &&
				chip->batt_uisoc == info->batt_recharge_setting &&
				chip->bat_status == POWER_SUPPLY_STATUS_CHARGING &&
				info->batt_recharge_running == false &&
				chip->val_current > 0) {
				goto charging_update_exit;
			}
			#endif
			//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel

			//uisoc reduce logic
			chip->uisoc_update_time = UISOC_DEFAULT_DISCHARGE_TIME;
			if (interval_time >= chip->uisoc_update_time) {
				chip->batt_uisoc = chip->batt_uisoc - 1;
				chip->pre_time = time_now;
			}
		} else {
			//do nothing
		}

charging_update_exit:
		min(100, chip->batt_uisoc);
	} else {
		chip->monitor_work_time = DISCHARGE_MONITOR_WORK_TIME;
		chip->uisoc_update_time = UISOC_DEFAULT_DISCHARGE_TIME;
		if (chip->batt_soc < chip->batt_uisoc) {
			//uisoc keep 1%, when battery voltage too high
			if (chip->batt_uisoc <= BATT_CAPICITY_LOW) {
				if (chip->batt_soc == BATT_CAPICITY_ZERO &&
					chip->batt_volt >= DISCHARGE_SHUTDOWN_VOLTAGE) {
					chip->batt_uisoc = BATT_CAPICITY_LOW;
					goto discharge_update_exit;
				}
			}

			if (interval_time >= chip->uisoc_update_time) {
				chip->batt_uisoc = chip->batt_uisoc -1;
				chip->pre_time = time_now;
			}
		} else {
			//do nothing
		}

discharge_update_exit:
		max(0, chip->batt_uisoc);
	}

	mmc_dbg(chip->dev, "chr_type: %d, aicl: %d, batt_volt: %d, val_curr: %d, update_time(chg: %d, curr: %d, soc: %d)\n",
			info->chr_type, input_current, chip->batt_volt, chip->val_current,
			charger_update_time, curr_update_time, chip->uisoc_update_time);

	dev_info(chip->dev, "soc_time(pre: %d, now: %d, interval: %d), soc(soc: %d, uisoc: %d)\n",
			chip->pre_time, time_now, interval_time,
			chip->batt_soc, chip->batt_uisoc);

	return ret;
}

static void mm8013_monitor_workfunc(struct work_struct *work)
{
	static int pre_val_soc = 0;
	struct mm8013_chip *chip = container_of(work,
				struct mm8013_chip, monitor_work.work);

	mm8013_soc(&chip->batt_soc);
	mm8013_current(&chip->val_current);
	mm8013_voltage(&chip->batt_volt);
	mm8013_get_uisoc(chip);

	if (pre_val_soc != chip->batt_uisoc) {
		pre_val_soc = chip->batt_uisoc;
		power_supply_changed(chip->mm8013_psy);
	}

	schedule_delayed_work(&chip->monitor_work, chip->monitor_work_time * HZ);
}

static enum power_supply_property mm8013_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
	POWER_SUPPLY_PROP_MANUFACTURE_YEAR,
	#endif
};

int mm8013_get_capacity_level(int uisoc)
{
	if (uisoc >= 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (uisoc >= 80 && uisoc < 100)
		return POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (uisoc >= 20 && uisoc < 80)
		return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (uisoc > 0 && uisoc < 20)
		return POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (uisoc == 0)
		return POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else
		return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
}

//+Peridot-7210, liyiying.wt, 20240510, add, when connect the tablet to charger-the battery level is increasing-but there is no charging icon
static void get_battery_charging_status(int debug)
{
	union power_supply_propval online = {0}, status = {0}, vbat0 = {0};
	union power_supply_propval prop_type = {0};
	bool chg_dev_chgen = true;
	int ret;
	struct mtk_charger *info = NULL;
	//int ret_vbus = 0;
	//union power_supply_propval vbus_val = {0};

	if (chip->is_probe_done == false) {
		pr_info("[%s]mm8013 probe is not rdy:%d\n",
			__func__, chip->is_probe_done);
		return;
	}

	if (IS_ERR_OR_NULL(chip->mtk_master_charger_psy)){
		chip->mtk_master_charger_psy = power_supply_get_by_name("mtk-master-charger");
	}

	if (chip->mtk_master_charger_psy != NULL) {
		info = (struct mtk_charger *)power_supply_get_drvdata(chip->mtk_master_charger_psy);
		//ret_vbus = power_supply_get_property(chip->mtk_master_charger_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &vbus_val);
	}

	if (IS_ERR_OR_NULL(chip->chg_psy)) {
		chip->chg_psy = devm_power_supply_get_by_phandle(chip->dev, "charger");
		pr_info("[%s]: retry to get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chip->chg_psy,
				POWER_SUPPLY_PROP_ONLINE, &online);

		ret = power_supply_get_property(chip->chg_psy,
			POWER_SUPPLY_PROP_STATUS, &status);

		ret = power_supply_get_property(chip->chg_psy,
			POWER_SUPPLY_PROP_ENERGY_EMPTY, &vbat0);

		if (!online.intval) {
			chip->bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING) {
				if (info->is_fast_charging == true && info->can_charging == true && chip->mtk_master_charger_psy != NULL) {
					chip->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				} else {
					chip->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
				}
			} else {
				chip->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
		}

		//notify_fg_chr_full(gm);

		ret = power_supply_get_property(chip->chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop_type);
	}

	if (debug == true) {
		pr_err("[wt-debug][%s]online:%d, status:%d, cur_chr_type:%d, chg_dev_chgen:%d, bat_status:%d, is_fast_charging:%d, can_chg:%d", __func__,
				online.intval, status.intval, prop_type.intval, chg_dev_chgen, chip->bat_status,
				info->is_fast_charging, info->can_charging);
	}
}
//-Peridot-7210, liyiying.wt, 20240510, add, when connect the tablet to charger-the battery level is increasing-but there is no charging icon

//+Peridot-4621, liyiying.wt, 20240514, add, The estimated charging time displayed in the battery management is incorrect
static int get_chg_type(void)
{
	struct power_supply *chg_master_psy = NULL;
	union power_supply_propval propval = {0};
	int ret;

	chg_master_psy = power_supply_get_by_name("primary_chg");

	ret = power_supply_get_property(chg_master_psy,
				POWER_SUPPLY_PROP_USB_TYPE, &propval);

	return propval.intval;
}

static int get_to_full_now(void)
{
	int ret;
	int ui_soc;
	int time_to_full_ret;
	union power_supply_propval tmp_val;

	ui_soc = chip->batt_uisoc;
	ret = mm8013_get_capacity_level(ui_soc);
	if ((ret == POWER_SUPPLY_CAPACITY_LEVEL_FULL) ||
		(ret == POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN))
		time_to_full_ret = 0;
	else {
		int q_max_now = BATTERY_QMAX;
		int remain_ui = 100 - ui_soc;
		int remain_mah = remain_ui * q_max_now / 10;
		int current_now = 0;
		int time_to_full = 0;
		int chg_type;
		int pre_current_now = 0;
		struct mtk_charger *info = NULL;

		chg_type = get_chg_type();

		if (IS_ERR_OR_NULL(chip->mtk_master_charger_psy)){
			chip->mtk_master_charger_psy = power_supply_get_by_name("mtk-master-charger");
		}

		if (chip->mtk_master_charger_psy != NULL) {
			info = (struct mtk_charger *)power_supply_get_drvdata(chip->mtk_master_charger_psy);
			if ((chg_type == POWER_SUPPLY_USB_TYPE_SDP
						&& info->sink_vbus_mV >= 5000
						&& info->sink_vbus_mA >= 1500) ||
					(chg_type == POWER_SUPPLY_USB_TYPE_SDP
						&& info->sink_vbus_mV >= 9000)) {
				chg_type = POWER_SUPPLY_USB_TYPE_DCP;
			}
		}

		mm8013_current(&tmp_val.intval);
		pre_current_now = tmp_val.intval;
		current_now = tmp_val.intval;

		if (remain_ui == 0) {
			current_now = 0;
		} else  if (remain_ui <= 1) {
			current_now = max(current_now, 1000);
		} else if (remain_ui <= 2) {
			current_now = max(current_now, 2000);
		} else  if (remain_ui <= 3) {
			current_now = max(current_now, 3000);
		} else if (remain_ui <= 4 ||
			chg_type == POWER_SUPPLY_USB_TYPE_SDP) {
			if (chg_type == POWER_SUPPLY_USB_TYPE_SDP
				&& chip->mtk_master_charger_psy != NULL) {
				if (info->sink_vbus_mV == 0 && info->sink_vbus_mA == 0) {
					return -1;
				}
			}
			current_now = max(current_now, 4000);
		} else if(remain_ui <= 15) {
			if (current_now < 8000) {
				return -1;
			}
			current_now = max(current_now, 8000);
		} else {
			if (current_now < 12000) {
				return -1;
			}
			current_now = max(current_now, 12000);
		}

		if (current_now != 0)
			time_to_full = remain_mah * 3600 / current_now;
		pr_info("time_to_full:%d, remain:ui:%d mah:%d, pre_current_now:%d, current_now:%d, qmax:%d, chg_type:%d\n",
			time_to_full, remain_ui, remain_mah,
			pre_current_now, current_now, q_max_now, chg_type);
		time_to_full_ret = abs(time_to_full);
	}

	return time_to_full_ret;
}
//-Peridot-4621, liyiying.wt, 20240514, add, The estimated charging time displayed in the battery management is incorrect

static int mm8013_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret  = 0;
	union power_supply_propval tmp_val;

	if (has_8013) {
		switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY:
			//ret = mm8013_soc(&chip->batt_soc);
			val->intval = chip->batt_uisoc;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			ret = mm8013_voltage(&chip->batt_volt);
			val->intval = chip->batt_volt * 1000;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			mm8013_current_now(&test_val_cur_now);
			val->intval = test_val_cur_now * 100;
			break;
		case POWER_SUPPLY_PROP_CURRENT_AVG:
			mm8013_current(&test_val_cur);
			val->intval = test_val_cur * 100;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			ret = mm8013_temperature(&val->intval);
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			ret = mm8013_voltage(&val->intval);
			if (val->intval <= 2000)
				val->intval = 0;
			else
				val->intval = 1;
			break;
		/* mtk add */
		case POWER_SUPPLY_PROP_STATUS:
			get_battery_charging_status(false);
			val->intval = chip->bat_status;
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		case POWER_SUPPLY_PROP_CYCLE_COUNT:
			//val->intval = 1;
			ret = mm8013_read_cyclecount(&val->intval);
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL:
			val->intval = BATTERY_QMAX * 1000;
			break;
		case POWER_SUPPLY_PROP_CHARGE_COUNTER:
			//ret = mm8013_soc(&val->intval);
			val->intval = (chip->batt_uisoc) * ((BATTERY_QMAX * 1000) / 100);
			break;
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			val->intval = mm8013_get_capacity_level(chip->batt_uisoc);
			break;
		case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
			val->intval = get_to_full_now();
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			//val->intval = 0;
			//ret = mm8013_soc(&(tmp_val.intval));
			tmp_val.intval = chip->batt_soc;
			if (mm8013_get_capacity_level(tmp_val.intval) ==
				POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN) {
				val->intval = 0;
			} else {
				val->intval = 10200000;
			}
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
			//val->intval = _BATTERY_CV;
			ret = mm8013_read_charge_vol(&val->intval);
			break;
		case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
			val->intval = mm8013_read_healthd_status();
			break;
		#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
		case POWER_SUPPLY_PROP_MANUFACTURE_YEAR:
			val->intval = mm8013_read_product_date();
			break;
		#endif
		default:
			return -EINVAL;
		}
	} else {
		pr_info("failed: %s!\n", __func__);
		val->intval = -99;
	}

	return ret;
}

//+Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0
static int mm8013_set_property(struct power_supply *psy,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	int ret;

	switch (psp) {
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
			ret = mm8013_write_charge_vol(val->intval);
			break;
		default:
			return -EINVAL;
	}

	return ret;
}

static int mm8013_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return 1;
	default:
		return 0;
	}
}
//-Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0

static void mm8013_external_power_changed(struct power_supply *psy)
{
	get_battery_charging_status(true);
	power_supply_changed(chip->mm8013_psy);
}

//+Peridot-9162, liyiying,wt, add, 20240606, MTK Verify Battery LevelTablet fail
#if IS_ENABLED(CONFIG_COMPAT)
static long compat_adc_cali_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int adc_out_datas[2] = { 1, 1 };

	pr_err("%s 32bit IOCTL, cmd=0x%08x\n",
		__func__, cmd);
	if (!filp->f_op || !filp->f_op->unlocked_ioctl) {
		pr_err("%s file has no f_op or no f_op->unlocked_ioctl.\n",
			__func__);
		return -ENOTTY;
	}

	if (sizeof(arg) != sizeof(adc_out_datas))
		return -EFAULT;

	switch (cmd) {
	case Get_META_BAT_VOL:
	case Get_META_BAT_SOC:
	case Get_META_BAT_CAR_TUNE_VALUE:
	case Set_META_BAT_CAR_TUNE_VALUE:
	case Set_BAT_DISABLE_NAFG:
	case Set_CARTUNE_TO_KERNEL: {
		pr_err(
			"%s send to unlocked_ioctl cmd=0x%08x\n",
			__func__,
			cmd);
		return filp->f_op->unlocked_ioctl(
			filp, cmd,
			(unsigned long)compat_ptr(arg));
	}
		break;
	default:
		pr_err("%s unknown IOCTL: 0x%08x, %d\n",
			__func__, cmd, adc_out_datas[0]);
		break;
	}

	return 0;
}
#endif

static long adc_cali_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int *user_data_addr;
	int ret = 0;
	int adc_in_data[2] = { 1, 1 };
	int adc_out_data[2] = { 1, 1 };
	//int temp_car_tune;
	//int isdisNAFG = 0;

	mutex_lock(&chip->meta_rw_lock);
	user_data_addr = (int *)arg;
	ret = copy_from_user(adc_in_data, user_data_addr, sizeof(adc_in_data));
	if (adc_in_data[1] < 0) {
		pr_err("%s unknown data: %d\n", __func__, adc_in_data[1]);
		mutex_unlock(&chip->meta_rw_lock);
		return -EFAULT;
	}

	switch (cmd) {
		/* add for meta tool------------------------------- */

	case Get_META_BAT_VOL:
		ret = mm8013_voltage(&chip->batt_volt);
		adc_out_data[0] = chip->batt_volt * 1000;
		if (copy_to_user(user_data_addr, adc_out_data,
			sizeof(adc_out_data))) {
			mutex_unlock(&chip->meta_rw_lock);
			return -EFAULT;
		}

		pr_err("**** unlocked_ioctl :Get_META_BAT_VOL Done!\n");
		break;
	case Get_META_BAT_SOC:
		adc_out_data[0] = chip->batt_uisoc;

		if (copy_to_user(user_data_addr, adc_out_data,
			sizeof(adc_out_data))) {
			mutex_unlock(&chip->meta_rw_lock);
			return -EFAULT;
		}

		pr_err("**** unlocked_ioctl :Get_META_BAT_SOC Done!\n");
		break;
	/*
	case Get_META_BAT_CAR_TUNE_VALUE:
		break;
	case Set_META_BAT_CAR_TUNE_VALUE:
		break;
	case Set_BAT_DISABLE_NAFG:
		break;
	case Set_CARTUNE_TO_KERNEL:
		break;
	*/
	default:
		pr_err("**** unlocked_ioctl unknown IOCTL: 0x%08x\n", cmd);
		mutex_unlock(&chip->meta_rw_lock);
		return -EINVAL;
	}

	mutex_unlock(&chip->meta_rw_lock);

	return 0;
}

static int adc_cali_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int adc_cali_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations adc_cali_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adc_cali_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.compat_ioctl = compat_adc_cali_ioctl,
#endif
	.open = adc_cali_open,
	.release = adc_cali_release,
};

static int adc_cali_cdev_init(struct platform_device *pdev)
{
	int ret = 0;
	struct class_device *class_dev = NULL;

	mutex_init(&chip->meta_rw_lock);

	ret = alloc_chrdev_region(&bat_cali_devno, 0, 1, BAT_CALI_DEVNAME);
	if (ret)
		pr_err("Error: Can't Get Major number for adc_cali\n");

	bat_cali_cdev = cdev_alloc();
	bat_cali_cdev->owner = THIS_MODULE;
	bat_cali_cdev->ops = &adc_cali_fops;
	ret = cdev_add(bat_cali_cdev, bat_cali_devno, 1);
	if (ret)
		pr_err("adc_cali Error: cdev_add\n");

	bat_cali_major = MAJOR(bat_cali_devno);
	bat_cali_class = class_create(THIS_MODULE, BAT_CALI_DEVNAME);
	class_dev = (struct class_device *)device_create(bat_cali_class,
		NULL,
		bat_cali_devno,
		NULL, BAT_CALI_DEVNAME);

	return 0;
}
//-Peridot-9162, liyiying,wt, add, 20240606, MTK Verify Battery LevelTablet fail

static int	mm8013_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret;
	int batt_id;
	struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg = {0};
	struct power_supply_desc *batt_psy_desc;
	struct power_supply_config batt_psy_cfg = {0};
	struct device *cdev = &client->dev;

	has_8013 = false;
	pr_info("%s start!\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		pr_info("failed: %s smbus data not supported!\n", __func__);
		return -EIO;
	}

	chip = devm_kzalloc(cdev, sizeof(struct mm8013_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;

	i2c_set_clientdata(client, chip);

	chip->pdev = to_platform_device(&client->dev);

	//+Peridot-422, liyiying.wt, 20240222, add, add batt id
	mutex_init(&chip->i2c_rw_lock);
	//-Peridot-422, liyiying.wt, 20240222, add, add batt id

	ret = mm8013_checkdevice();
	if (ret < 0) {
		pr_info("failed to access\n");
		return -EPROBE_DEFER;
	} else {
		has_8013 = true;
	}

	psy_desc = devm_kzalloc(&client->dev, sizeof(*psy_desc), GFP_KERNEL);
	if (!psy_desc)
		return -ENOMEM;

	psy_cfg.drv_data = chip;
	psy_desc->name = "battery_mm8013";
	psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	psy_desc->properties = mm8013_battery_props;
	psy_desc->num_properties = ARRAY_SIZE(mm8013_battery_props);
	psy_desc->get_property = mm8013_get_property;
	psy_desc->external_power_changed = mm8013_external_power_changed;
	//+Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0
	psy_desc->set_property = mm8013_set_property;
	psy_desc->property_is_writeable =
			mm8013_property_is_writeable;
	//-Peridot-198, liyiying.wt, 20240326, battery and charging - battery maintenance 1.0

	chip->mm8013_psy = power_supply_register(&client->dev, psy_desc, &psy_cfg);
	if (IS_ERR(chip->mm8013_psy)) {
		ret = PTR_ERR(chip->mm8013_psy);
		pr_info("failed to register battery: %d\n", ret);

		return ret;
	}

	//+PERIDOT-1421, liyiying.wt, 20240424, mod, Standby power consumption exceeds the standard
	batt_psy_desc = devm_kzalloc(&client->dev, sizeof(*batt_psy_desc), GFP_KERNEL);
	if (!batt_psy_desc)
		return -ENOMEM;

	batt_psy_cfg.drv_data = chip;
	batt_psy_desc->name = "battery";
	batt_psy_desc->type = POWER_SUPPLY_TYPE_BATTERY;
	batt_psy_desc->properties = mm8013_battery_props;
	batt_psy_desc->num_properties = ARRAY_SIZE(mm8013_battery_props);
	batt_psy_desc->get_property = mm8013_get_property;
	batt_psy_desc->external_power_changed = mm8013_external_power_changed;
	batt_psy_desc->set_property = mm8013_set_property;
	batt_psy_desc->property_is_writeable = mm8013_property_is_writeable;

	chip->battery_psy = power_supply_register(&client->dev, batt_psy_desc, &batt_psy_cfg);
	if (IS_ERR(chip->battery_psy)) {
		ret = PTR_ERR(chip->battery_psy);
		pr_info("failed to register battery: %d\n", ret);

		return ret;
	}

	chip->bat_status = POWER_SUPPLY_STATUS_DISCHARGING;

	/*
	chip->chg1_dev = get_charger_by_name("primary_dvchg");
	if (chip->chg1_dev)
		pr_err("%s, Found primary charger\n", __func__);
	else {
		pr_err("%s, *** Error : can't find primary charger ***\n" , __func__);
	}
	*/
	//-PERIDOT-1421, liyiying.wt, 20240424, mod, Standby power consumption exceeds the standard

	mm8013_soc(&chip->batt_soc);
	mm8013_current(&chip->val_current);
	mm8013_current_now(&test_val_cur_now);
	mm8013_voltage(&chip->batt_volt);
	chip->batt_uisoc = chip->batt_soc;
	if (chip->batt_uisoc < BATT_CAPICITY_LOW) {
		chip->batt_uisoc = BATT_CAPICITY_LOW;
	}

	//+Peridot-422, liyiying.wt, 20240222, add, add batt id
	batt_id = mm8013_read_batt_id();
	dev_info(chip->dev, "batt_soc: %d, now_curr: %d, val_curr: %d, val_vol: %d, batt_id: %d\n",
			chip->batt_soc, test_val_cur_now,
			chip->val_current, chip->batt_volt, batt_id);

	switch(batt_id) {
		case BATT_SUNWODA:
			hardwareinfo_set_prop(HARDWARE_BATTERY_ID, BATT_SUNWODA_NAME);
			break;
		case BATT_SCUD:
			hardwareinfo_set_prop(HARDWARE_BATTERY_ID, BATT_SCUD_NAME);
			break;
		case BATT_NVT:
			hardwareinfo_set_prop(HARDWARE_BATTERY_ID, BATT_NVT_NAME);
			break;
		default:
			hardwareinfo_set_prop(HARDWARE_BATTERY_ID, BATT_UNKNOW_NAME);
			break;
	}
	//-Peridot-422, liyiying.wt, 20240222, add, add batt id

	//+Peridot-35,xiaohongyu,wt,add,20240223,add GAUGE message in factory mode
	hardwareinfo_set_prop(HARDWARE_BMS_GAUGE_ID, GAUGE_NAME);
	//-Peridot-35,xiaohongyu,wt,add,20240223,add GAUGE message in factory mode

	chip->pre_time = mm8013_get_sys_time();
	chip->vbus_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->monitor_work_time = DISCHARGE_MONITOR_WORK_TIME;
	INIT_DELAYED_WORK(&chip->monitor_work, mm8013_monitor_workfunc);
	schedule_delayed_work(&chip->monitor_work, 0);

	//+Peridot-9162, liyiying,wt, add, 20240606, MTK Verify Battery LevelTablet fail
	adc_cali_cdev_init(chip->pdev);
	//-Peridot-9162, liyiying,wt, add, 20240606, MTK Verify Battery LevelTablet fail

	pr_info("%s success!\n", __func__);
	chip->is_probe_done = true;

	return 0;
}

static	struct i2c_device_id mm8013_id_table[] = {
	{ "mm8013", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, mm8013_id_table);

static const struct of_device_id mm8013_match_table[] = {
	{ .compatible = "mediatek,mm8013",},
	{},
};

static struct i2c_driver mm8013_i2c_driver = {
	.driver    = {
	.name  = "mm8013",
	.owner = THIS_MODULE,
	.of_match_table = mm8013_match_table,
	},
	.probe	   = mm8013_probe,
	.id_table  = mm8013_id_table,
};

static int __init mm8013_i2c_init(void)
{
	return i2c_add_driver(&mm8013_i2c_driver);
}
static void __exit mm8013_i2c_exit(void)
{
	i2c_del_driver(&mm8013_i2c_driver);
}

module_init(mm8013_i2c_init);
module_exit(mm8013_i2c_exit);
MODULE_DESCRIPTION("I2c bus driver for mm8013x gauge");
MODULE_LICENSE("GPL v2");
