// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

/*
 *
 * Filename:
 * ---------
 *    mtk_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/thermal.h>

#include <asm/setup.h>

#include <linux/kernel.h>
#include <linux/container_of.h>
#include <linux/workqueue.h>

#include "mtk_charger.h"
#include "mtk_battery.h"
#include <tcpm.h>

struct tag_bootmode {
	u32 size;
	u32 tag;
	u32 bootmode;
	u32 boottype;
};

//+PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
static int mtk_charger_enable_power_path(struct mtk_charger *info, int idx, bool en);
//-PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection

//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
#define DISCHARGING_STATE_ATO			0x0001
#define DISCHARGING_STATE_MAINTAIN		0x0002
#define DISCHARGING_STATE_PROTECT		0x0004
#define DISCHARGING_STATE_STOREMODE	0x0008
#define DISCHARGING_STATE_FULLCAPACITY	0x0020
#define DISCHARGING_STATE_SLATEMODE	0x0100
#define DISCHARGING_BY_DISABLE			0x00FF
#define DISCHARGING_BY_HIZ				0xFF00
#define ATO_SOC_CONTROL_CHARGING		1
#define ATO_SOC_CONTROL_DISCHARGING	2
#ifdef WT_COMPILE_FACTORY_VERSION
static int wtchg_ato_charge_manage(struct mtk_charger *info);
#endif
//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
static bool wtchg_is_batt_protection_setting_eu(struct mtk_charger *info);
static bool wtchg_check_charge_full(struct mtk_charger *info);
#endif
//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel

//+PERIDOT-35, liyiying.wt, add, 20240420, add real_type node
static const char * const real_type_test[] = {
	"Unknown", "SDP", "DCP", "CDP", "PD", "PD_PPS", "BrickID", "HVDCP", "PE2"
};

enum wt_battery_type {
	WT_BATERY_TYPE_UNKNOWN = 0,
	WT_BATERY_TYPE_SDP,
	WT_BATERY_TYPE_DCP,
	WT_BATERY_TYPE_CDP,
	WT_BATERY_TYPE_PD,
	WT_BATERY_TYPE_PD_PPS,
	WT_BATERY_TYPE_BRICKID,
	WT_BATERY_TYPE_HVDCP,
	WT_BATERY_TYPE_PE2,
};
//-PERIDOT-35, liyiying.wt, add, 20240420, add real_type node

#ifdef MODULE
static char __chg_cmdline[COMMAND_LINE_SIZE];
static char *chg_cmdline = __chg_cmdline;


#if 1
#include <linux/uaccess.h>
int read_sys_protection_setting(void)
{
	struct file *filp = NULL;
	char buf[32] = {0};
	loff_t pos =0;
	int ret = -1;
	int value = 0;
	
	filp = filp_open("sys/class/power_supply/battery/protection_setting_eu",O_RDONLY,0);
	if(IS_ERR(filp))
	{
		chr_err("%s: failed to open sysfs\n", __func__);
		return PTR_ERR(filp);
	}
	ret = kernel_read(filp,buf,sizeof(buf) - 1,&pos);
	if(ret < 0) 
	{
		chr_err("%s: failed to read file: %d\n", __func__,ret);
		goto out_close;
	}	
	buf[ret] = '\0';
	if(kstrtoint(buf,10,&value) != 0)
	{
		chr_err("%s: Invalid format in sysfs file\n", __func__);
		ret = -EINVAL;
		goto out_close;
	}
	chr_err("%s: Read protectipn_setting_eu = %d\n", __func__,value);
	ret = value;
out_close:
	filp_close(filp,NULL);
	return ret;
}
#endif

const char *chg_get_cmd(void)
{
	struct device_node *of_chosen = NULL;
	char *bootargs = NULL;

	if (__chg_cmdline[0] != 0)
		return chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		bootargs = (char *)of_get_property(
					of_chosen, "bootargs", NULL);
		if (!bootargs)
			chr_err("%s: failed to get bootargs\n", __func__);
		else {
			strcpy(__chg_cmdline, bootargs);
			chr_err("%s: bootargs: %s\n", __func__, bootargs);
		}
	} else
		chr_err("%s: failed to get /chosen\n", __func__);

	return chg_cmdline;
}

#else
const char *chg_get_cmd(void)
{
	return saved_command_line;
}
#endif

int chr_get_debug_level(void)
{
	struct power_supply *psy;
	static struct mtk_charger *info;
	int ret;

	if (info == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL)
			ret = CHRLOG_DEBUG_LEVEL;
		else {
			info =
			(struct mtk_charger *)power_supply_get_drvdata(psy);
			if (info == NULL)
				ret = CHRLOG_DEBUG_LEVEL;
			else
				ret = info->log_level;
		}
	} else
		ret = info->log_level;

	ret = CHRLOG_DEBUG_LEVEL;
	return ret;
}
EXPORT_SYMBOL(chr_get_debug_level);

void _wake_up_charger(struct mtk_charger *info)
{
	unsigned long flags;

	if (info == NULL)
		return;
	spin_lock_irqsave(&info->slock, flags);
	if (!info->charger_wakelock->active)
		__pm_stay_awake(info->charger_wakelock);
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	wake_up_interruptible(&info->wait_que);
}

bool is_disable_charger(struct mtk_charger *info)
{
	if (info == NULL)
		return true;

	if (info->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

int _mtk_enable_charging(struct mtk_charger *info,
	bool en)
{
	chr_debug("%s en:%d\n", __func__, en);
	if (info->algo.enable_charging != NULL)
		return info->algo.enable_charging(info, en);
	return false;
}

int mtk_charger_notifier(struct mtk_charger *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

static void mtk_charger_parse_dt(struct mtk_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val = 0;
	struct device_node *boot_node = NULL;
	struct tag_bootmode *tag = NULL;
	int ret = 0;

	boot_node = of_parse_phandle(dev->of_node, "bootmode", 0);
	if (!boot_node)
		chr_err("%s: failed to get boot mode phandle\n", __func__);
	else {
		tag = (struct tag_bootmode *)of_get_property(boot_node,
							"atag,boot", NULL);
		if (!tag)
			chr_err("%s: failed to get atag,boot\n", __func__);
		else {
			chr_err("%s: size:0x%x tag:0x%x bootmode:0x%x boottype:0x%x\n",
				__func__, tag->size, tag->tag,
				tag->bootmode, tag->boottype);
			info->bootmode = tag->bootmode;
			info->boottype = tag->boottype;
		}
	}

	if (of_property_read_string(np, "algorithm-name",
		&info->algorithm_name) < 0) {
		if (of_property_read_string(np, "algorithm_name",
			&info->algorithm_name) < 0) {
			chr_err("%s: no algorithm_name, use Basic\n", __func__);
			info->algorithm_name = "Basic";
		}
	}

	if (strcmp(info->algorithm_name, "Basic") == 0) {
		chr_err("found Basic\n");
		mtk_basic_charger_init(info);
	} else if (strcmp(info->algorithm_name, "Pulse") == 0) {
		chr_err("found Pulse\n");
		mtk_pulse_charger_init(info);
	}

	info->disable_charger = of_property_read_bool(np, "disable_charger")
		|| of_property_read_bool(np, "disable-charger");
	info->charger_unlimited = of_property_read_bool(np, "charger_unlimited")
		|| of_property_read_bool(np, "charger-unlimited");
	info->atm_enabled = of_property_read_bool(np, "atm_is_enabled")
		|| of_property_read_bool(np, "atm-is-enabled");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer")
			|| of_property_read_bool(np, "enable-sw-safety-timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;
	info->disable_aicl = of_property_read_bool(np, "disable_aicl")
		|| of_property_read_bool(np, "disable-aicl");
	info->alg_new_arbitration = of_property_read_bool(np, "alg_new_arbitration")
		|| of_property_read_bool(np, "alg-new-arbitration");
	info->alg_unchangeable = of_property_read_bool(np, "alg_unchangeable")
		|| of_property_read_bool(np, "alg-unchangeable");

	/* common */

	if (of_property_read_u32(np, "charger_configuration", &val) >= 0)
		info->config = val;
	else if (of_property_read_u32(np, "charger-configuration", &val) >= 0)
		info->config = val;
	else {
		chr_err("use default charger_configuration:%d\n",
			SINGLE_CHARGER);
		info->config = SINGLE_CHARGER;
	}

	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else if (of_property_read_u32(np, "battery-cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chr_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}


	info->enable_boot_volt =
		of_property_read_bool(np, "enable_boot_volt")
		|| of_property_read_bool(np, "enable-boot-volt");

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else if (of_property_read_u32(np, "max-charger-voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "vbus_sw_ovp_voltage", &val) >= 0)
		info->data.vbus_sw_ovp_voltage = val;
	else if (of_property_read_u32(np, "vbus-sw-ovp-voltage", &val) >= 0)
		info->data.vbus_sw_ovp_voltage = val;
	else {
		chr_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.vbus_sw_ovp_voltage = VBUS_OVP_VOLTAGE;
	}

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else if (of_property_read_u32(np, "min-charger-voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chr_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

	if (of_property_read_u32(np, "enable_vbat_mon", &val) >= 0) {
		info->enable_vbat_mon = val;
		info->enable_vbat_mon_bak = val;
	} else if (of_property_read_u32(np, "enable-vbat-mon", &val) >= 0) {
		info->enable_vbat_mon = val;
		info->enable_vbat_mon_bak = val;
	} else {
		chr_err("use default enable 6pin\n");
		info->enable_vbat_mon = 0;
		info->enable_vbat_mon_bak = 0;
	}
	chr_err("enable_vbat_mon:%d\n", info->enable_vbat_mon);

	/* sw jeita */
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita")
		|| of_property_read_bool(np, "enable-sw-jeita");

	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-above-t4-cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t3-to-t4-cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t2-to-t3-cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t1-to-t2-cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else if (of_property_read_u32(np, "jeita-temp-t0-to-t1-cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	if (of_property_read_u32(np, "jeita-temp-below-t0-cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	//+PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
	/* max charging current: fcc */
	if (of_property_read_u32(np, "jeita-temp-above-t4-fcc", &val) >= 0)
		info->data.jeita_temp_above_t4_fcc = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_FCC:%d\n",
			JEITA_TEMP_ABOVE_T4_FCC);
		info->data.jeita_temp_above_t4_fcc = JEITA_TEMP_ABOVE_T4_FCC;
	}

	if (of_property_read_u32(np, "jeita-temp-t3-to-t4-fcc", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_fcc= val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_FCC:%d\n",
			JEITA_TEMP_T3_TO_T4_FCC);
		info->data.jeita_temp_t3_to_t4_fcc = JEITA_TEMP_T3_TO_T4_FCC;
	}

	if (of_property_read_u32(np, "jeita-temp-t2-to-t3-fcc", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_fcc = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_FCC:%d\n",
			JEITA_TEMP_T2_TO_T3_FCC);
		info->data.jeita_temp_t2_to_t3_fcc = JEITA_TEMP_T2_TO_T3_FCC;
	}

	if (of_property_read_u32(np, "jeita-temp-t1-to-t2-fcc", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_fcc = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_FCC:%d\n",
			JEITA_TEMP_T1_TO_T2_FCC);
		info->data.jeita_temp_t1_to_t2_fcc = JEITA_TEMP_T1_TO_T2_FCC;
	}

	if (of_property_read_u32(np, "jeita-temp-t0-to-t1-fcc", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_fcc = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_FCC:%d\n",
			JEITA_TEMP_T0_TO_T1_FCC);
		info->data.jeita_temp_t0_to_t1_fcc = JEITA_TEMP_T0_TO_T1_FCC;
	}

	if (of_property_read_u32(np, "jeita-temp-below-t0-fcc", &val) >= 0)
		info->data.jeita_temp_below_t0_fcc = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_FCC:%d\n",
			JEITA_TEMP_BELOW_T0_FCC);
		info->data.jeita_temp_below_t0_fcc = JEITA_TEMP_BELOW_T0_FCC;
	}

	/* cut-off current: term */
	if (of_property_read_u32(np, "jeita-temp-above-t4-term", &val) >= 0)
		info->data.jeita_temp_above_t4_term = val;
	else {
		chr_err("use default JEITA_TEMP_ABOVE_T4_TERM:%d\n",
			JEITA_TEMP_ABOVE_T4_TERM);
		info->data.jeita_temp_above_t4_term = JEITA_TEMP_ABOVE_T4_TERM;
	}

	if (of_property_read_u32(np, "jeita-temp-t3-to-t4-term", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_term= val;
	else {
		chr_err("use default JEITA_TEMP_T3_TO_T4_TERM:%d\n",
			JEITA_TEMP_T3_TO_T4_TERM);
		info->data.jeita_temp_t3_to_t4_term = JEITA_TEMP_T3_TO_T4_TERM;
	}

	if (of_property_read_u32(np, "jeita-temp-t2-to-t3-term", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_term = val;
	else {
		chr_err("use default JEITA_TEMP_T2_TO_T3_TERM:%d\n",
			JEITA_TEMP_T2_TO_T3_TERM);
		info->data.jeita_temp_t2_to_t3_term = JEITA_TEMP_T2_TO_T3_TERM;
	}

	if (of_property_read_u32(np, "jeita-temp-t1-to-t2-term", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_term = val;
	else {
		chr_err("use default JEITA_TEMP_T1_TO_T2_TERM:%d\n",
			JEITA_TEMP_T1_TO_T2_TERM);
		info->data.jeita_temp_t1_to_t2_term = JEITA_TEMP_T1_TO_T2_TERM;
	}

	if (of_property_read_u32(np, "jeita-temp-t0-to-t1-term", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_term = val;
	else {
		chr_err("use default JEITA_TEMP_T0_TO_T1_TERM:%d\n",
			JEITA_TEMP_T0_TO_T1_TERM);
		info->data.jeita_temp_t0_to_t1_term = JEITA_TEMP_T0_TO_T1_TERM;
	}

	if (of_property_read_u32(np, "jeita-temp-below-t0-term", &val) >= 0)
		info->data.jeita_temp_below_t0_term = val;
	else {
		chr_err("use default JEITA_TEMP_BELOW_T0_TERM:%d\n",
			JEITA_TEMP_BELOW_T0_TERM);
		info->data.jeita_temp_below_t0_term = JEITA_TEMP_BELOW_T0_TERM;
	}

	if (of_property_read_u32(np, "charge-basic-term", &val) >= 0)
		info->charge_baisc_term = val;
	else {
		chr_err("use default CHARGE_BASIC_TERM:%d\n",
			CHARGE_BASIC_TERM);
		info->charge_baisc_term = CHARGE_BASIC_TERM;
	}
	//-PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic

	//+Peridot-198, yangpingao.wt, 20240329, mod, adjust battery maintenance logic
	/* max battery aging cycle: cycle */
	if (of_property_read_u32(np, "maintenance-v10-level0-cycle", &val) >= 0)
		info->data.maintenance_v10_level0_cycle = val;
	else {
		chr_err("load maintenance-v10-level0-cycle fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level1-cycle", &val) >= 0)
		info->data.maintenance_v10_level1_cycle = val;
	else {
		chr_err("load maintenance-v10-level1-cycle fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level2-cycle", &val) >= 0)
		info->data.maintenance_v10_level2_cycle = val;
	else {
		chr_err("load maintenance-v10-level2-cycle fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level3-cycle", &val) >= 0)
		info->data.maintenance_v10_level3_cycle = val;
	else {
		chr_err("load maintenance-v10-level3-cycle fail !!!\n");
	}

	/* max battery aging voltage: cv */
	if (of_property_read_u32(np, "maintenance-v10-level0-cv", &val) >= 0)
		info->data.maintenance_v10_level0_cv = val;
	else {
		chr_err("load maintenance-v10-level0-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level1-cv", &val) >= 0)
		info->data.maintenance_v10_level1_cv = val;
	else {
		chr_err("load maintenance-v10-level1-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level2-cv", &val) >= 0)
		info->data.maintenance_v10_level2_cv = val;
	else {
		chr_err("load maintenance-v10-level2-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level3-cv", &val) >= 0)
		info->data.maintenance_v10_level3_cv = val;
	else {
		chr_err("load maintenance-v10-level3-cv fail !!!\n");
	}

	/* max battery aging fuel gauge voltage: fg-cv */
	if (of_property_read_u32(np, "maintenance-v10-level0-fg-cv", &val) >= 0)
		info->data.maintenance_v10_level0_fg_cv = val;
	else {
		chr_err("load maintenance-v10-level0-fg-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level1-fg-cv", &val) >= 0)
		info->data.maintenance_v10_level1_fg_cv = val;
	else {
		chr_err("load mmaintenance-v10-level1-fg-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level2-fg-cv", &val) >= 0)
		info->data.maintenance_v10_level2_fg_cv = val;
	else {
		chr_err("load maintenance-v10-level2-fg-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v10-level3-fg-cv", &val) >= 0)
		info->data.maintenance_v10_level3_fg_cv = val;
	else {
		chr_err("load maintenance-v10-level3-fg-cv fail !!!\n");
	}

	/* max maintenance-v20 protect voltage: fg-cv */
	if (of_property_read_u32(np, "maintenance-v20-level0-cv", &val) >= 0)
		info->data.maintenance_v20_level0_cv = val;
	else {
		chr_err("load maintenance-v20-level0-cv fail !!!\n");
	}

	if (of_property_read_u32(np, "maintenance-v20-level0-fg-cv", &val) >= 0)
		info->data.maintenance_v20_level0_fg_cv = val;
	else {
		chr_err("load maintenance-v20-level0-fg-cv fail !!!\n");
	}
	//-Peridot-198, yangpingao.wt, 20240329, mod, adjust battery maintenance logic

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else if (of_property_read_u32(np, "temp-t4-thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chr_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t4-thres-minus-x-degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else if (of_property_read_u32(np, "temp-t3-thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chr_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t3-thres-minus-x-degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chr_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else if (of_property_read_u32(np, "temp-t2-thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chr_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t2-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else if (of_property_read_u32(np, "temp-t1-thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chr_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t1-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else if (of_property_read_u32(np, "temp-t0-thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chr_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else if (of_property_read_u32(np, "temp-t0-thres-plus-x-degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chr_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else if (of_property_read_u32(np, "temp-neg-10-thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else {
		chr_err("use default TEMP_NEG_10_THRES:%d\n",
			TEMP_NEG_10_THRES);
		info->data.temp_neg_10_thres = TEMP_NEG_10_THRES;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp")
		|| of_property_read_bool(np, "enable-min-charge-temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else if (of_property_read_u32(np, "min-charge-temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chr_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
		>= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else if (of_property_read_u32(np, "min-charge-temp-plus-x-degree", &val)
		>= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chr_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else if (of_property_read_u32(np, "max-charge-temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chr_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
		>= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else if (of_property_read_u32(np, "max-charge-temp-minus-x-degree", &val)
		>= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chr_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0)
		info->data.usb_charger_current = val;
	else if (of_property_read_u32(np, "usb-charger-current", &val) >= 0)
		info->data.usb_charger_current = val;
	else {
		chr_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0)
		info->data.ac_charger_current = val;
	if (of_property_read_u32(np, "ac-charger-current", &val) >= 0)
		info->data.ac_charger_current = val;
	else {
		chr_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else if (of_property_read_u32(np, "ac-charger-input-current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chr_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else if (of_property_read_u32(np, "charging-host-charger-current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
	if (of_property_read_u32(np, "apple-5w-charger-current", &val) >= 0)
		info->data.apple_5w_charger_current = val;
	else {
		info->data.apple_5w_charger_current = APPLE_5W_CHARGER_CURRENT;
		chr_err("load apple-5w-charger-current fail !\n");
	}

	if (of_property_read_u32(np, "apple-10w-charger-current", &val) >= 0)
		info->data.apple_10w_charger_current = val;
	else {
		info->data.apple_10w_charger_current = APPLE_10W_CHARGER_CURRENT;
		chr_err("load apple-10w-charger-current fail !\n");
	}

	if (of_property_read_u32(np, "apple-12w-charger-current", &val) >= 0)
		info->data.apple_12w_charger_current = val;
	else {
		info->data.apple_12w_charger_current = APPLE_12W_CHARGER_CURRENT;
		chr_err("load apple-12w-charger-current fail !\n");
	}

	if (of_property_read_u32(np, "samsung-charger-current", &val) >= 0)
		info->data.samsung_charger_current = val;
	else {
		info->data.samsung_charger_current = SAMSUNG_CHARGER_CURRENT;
		chr_err("load samsung-charger-current fail !\n");
	}

	if (of_property_read_u32(np, "qc20-charger-current", &val) >= 0)
		info->data.qc20_charger_current = val;
	else {
		info->data.qc20_charger_current = QC20_CHARGER_CURRENT;
		chr_err("load qc20-charger-current fail !\n");
	}

	if (of_property_read_u32(np, "qc20-charger-input-current", &val) >= 0)
		info->data.qc20_charger_input_current = val;
	else {
		info->data.qc20_charger_input_current = QC20_CHARGER_INPUT_CURRENT;
		chr_err("load qc20-charger-input-current fail !\n");
	}
	//-PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

	/* dynamic mivr */
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr")
			|| of_property_read_bool(np, "enable-dynamic-mivr");

	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else if (of_property_read_u32(np, "min-charger-voltage-1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chr_err("use default V_CHARGER_MIN_1: %d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else if (of_property_read_u32(np, "min-charger-voltage-2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chr_err("use default V_CHARGER_MIN_2: %d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else if (of_property_read_u32(np, "max-dmivr-charger-current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chr_err("use default MAX_DMIVR_CHARGER_CURRENT: %d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}
	/* fast charging algo support indicator */
	info->enable_fast_charging_indicator =
			of_property_read_bool(np, "enable_fast_charging_indicator")
			|| of_property_read_bool(np, "enable-fast-charging-indicator");

	//+ExtB EKCANCUN-11, yangpingao.wt, 2024/02/01, add fast charging indicator
	if (of_property_read_u32(np, "fast_charging_indicator", &val) >= 0)
		info->fast_charging_indicator = val;
	else {
		chr_err("use default fast_charging_indicator: 0x0F\n");
	}
	//-ExtB EKCANCUN-11, yangpingao.wt, 2024/02/01, add fast charging indicator

	//+PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func
	ret = of_get_named_gpio(np, "vbus_mos_gpio", 0);
	if (ret < 0) {
		chr_err("%s fail get vbus_mos_gpio\n", __func__);
	} else {
		info->vbus_mos_gpio = ret;
		chr_err("%s success get vbus_mos_gpio: %d\n", __func__, info->vbus_mos_gpio);
	}

	ret = gpio_request(info->vbus_mos_gpio, "vbus_mos_gpio");
	if (ret) {
		chr_err("%s failed to request vbus_mos_gpio\n", __func__);
	} else {
		gpio_direction_output(info->vbus_mos_gpio, 1);
		gpio_set_value(info->vbus_mos_gpio, 0);
	}
	//-PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func

	//+PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)
	info->is_fast_charging = false;
	info->pr_is_fast_charging = false;
	//-PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)
	//+PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
	info->charging_enabled_user_control = 1;
	info->start_charging_test = 1;
	//-PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection

	info->pe5_online = false;
	info->pe2_online = false;
	info->pd_online = false;
	info->qc20_online = false;
	info->real_type = WT_BATERY_TYPE_UNKNOWN;
	info->pd_hardreset_times = 0;
	info->pe50_auth_ta_fail = false;
	info->sink_vbus_mV = 0;
	info->sink_vbus_mA = 0;

	#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
	info->running_batt_protection = false;
	info->force_is_chg_done = false;
	info->force_pe5_is_chg_done = false;
	info->batt_recharge_running = false;
	#endif
}

static void mtk_charger_start_timer(struct mtk_charger *info)
{
	struct timespec64 end_time, time_now;
	ktime_t ktime, ktime_now;
	int ret = 0;

	/* If the timer was already set, cancel it */
	ret = alarm_try_to_cancel(&info->charger_timer);
	if (ret < 0) {
		chr_err("%s: callback was running, skip timer\n", __func__);
		return;
	}

	ktime_now = ktime_get_boottime();
	time_now = ktime_to_timespec64(ktime_now);
	end_time.tv_sec = time_now.tv_sec + info->polling_interval;
	end_time.tv_nsec = time_now.tv_nsec + 0;
	info->endtime = end_time;
	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chr_err("%s: alarm timer start:%d, %lld %ld\n", __func__, ret,
		info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&info->charger_timer, ktime);
}

static void check_battery_exist(struct mtk_charger *info)
{
	unsigned int i = 0;
	int count = 0;
	//int boot_mode = get_boot_mode();

	if (is_disable_charger(info))
		return;

	for (i = 0; i < 3; i++) {
		if (is_battery_exist(info) == false)
			count++;
	}

#ifdef FIXME
	if (count >= 3) {
		if (boot_mode == META_BOOT || boot_mode == ADVMETA_BOOT ||
		    boot_mode == ATE_FACTORY_BOOT)
			chr_info("boot_mode = %d, bypass battery check\n",
				boot_mode);
		else {
			chr_err("battery doesn't exist, shutdown\n");
			orderly_poweroff(true);
		}
	}
#endif
}

static void check_dynamic_mivr(struct mtk_charger *info)
{
	//int i = 0, ret = 0;
	int vbat = 0;
	bool is_fast_charge = false;
	//struct chg_alg_device *alg = NULL;

	if (!info->enable_dynamic_mivr)
		return;
//+PERIDOT-35, yangpingao.wt, 20240305, mod, disable dvchg when ibat <= swchg_term
#if 0
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_fast_charge = true;
			break;
		}
	}
#endif
//-PERIDOT-35, yangpingao.wt, 20240305, mod, disable dvchg when ibat <= swchg_term
	if (!is_fast_charge) {
		vbat = get_battery_voltage(info);
		if (vbat < info->data.min_charger_voltage_2 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_2);
		else if (vbat < info->data.min_charger_voltage_1 / 1000 - 200)
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage_1);
		else
			charger_dev_set_mivr(info->chg1_dev,
				info->data.min_charger_voltage);
	}
}

/* sw jeita */
void do_sw_jeita_state_machine(struct mtk_charger *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;
	chr_debug("[SW_JEITA]pre_sm:%d,sm:%d,tmp:%d",sw_jeita->pre_sm,sw_jeita->sm,info->battery_temp);

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	//+PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	//if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4) {
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
			sw_jeita->fcc = info->data.jeita_temp_above_t4_fcc;
			sw_jeita->term = info->data.jeita_temp_above_t4_term;
		} else if (sw_jeita->sm == TEMP_T3_TO_T4) {
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
			sw_jeita->fcc = info->data.jeita_temp_t3_to_t4_fcc;
			sw_jeita->term = info->data.jeita_temp_t3_to_t4_term;
		} else if (sw_jeita->sm == TEMP_T2_TO_T3) {
			sw_jeita->cv = info->data.jeita_temp_t2_to_t3_cv;
			sw_jeita->fcc = info->data.jeita_temp_t2_to_t3_fcc;
			sw_jeita->term = info->data.jeita_temp_t2_to_t3_term;
		} else if (sw_jeita->sm == TEMP_T1_TO_T2) {
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
			sw_jeita->fcc = info->data.jeita_temp_t1_to_t2_fcc;
			sw_jeita->term = info->data.jeita_temp_t1_to_t2_term;
		} else if (sw_jeita->sm == TEMP_T0_TO_T1) {
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
			sw_jeita->fcc = info->data.jeita_temp_t0_to_t1_fcc;
			sw_jeita->term = info->data.jeita_temp_t0_to_t1_term;
		} else if (sw_jeita->sm == TEMP_BELOW_T0) {
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
			sw_jeita->fcc = info->data.jeita_temp_below_t0_fcc;
			sw_jeita->term = info->data.jeita_temp_below_t0_term;
		} else {
			sw_jeita->cv = info->data.battery_cv;
		}

	//+PERIDOT-422, liyiying.wt, 20240419, add, The development process requires the use of the de-temperature-controlled version and the addition of macro control
	#if IS_ENABLED(CONFIG_MTK_DISABLE_TEMP_PROTECT)
	sw_jeita->cv = 4100000;
	sw_jeita->fcc = info->data.jeita_temp_t1_to_t2_fcc;
	sw_jeita->term = info->data.jeita_temp_t1_to_t2_term;
	#endif
	//-PERIDOT-422, liyiying.wt, 20240419, add, The development process requires the use of the de-temperature-controlled version and the addition of macro control

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d fcc:%d term:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv, sw_jeita->fcc, sw_jeita->term);
	//-PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
}

static int mtk_chgstat_notify(struct mtk_charger *info)
{
	int ret = 0;
	char *env[2] = { "CHGSTAT=1", NULL };

	chr_err("%s: 0x%x\n", __func__, info->notify_code);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		chr_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static void mtk_charger_set_algo_log_level(struct mtk_charger *info, int level)
{
	struct chg_alg_device *alg;
	int i = 0, ret = 0;

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		if (alg == NULL)
			continue;

		ret = chg_alg_set_prop(alg, ALG_LOG_LEVEL, level);
		if (ret < 0)
			chr_err("%s: set ALG_LOG_LEVEL fail, ret =%d", __func__, ret);
	}
}

static ssize_t sw_jeita_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t sw_jeita_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_jeita);
/* sw jeita end*/

static ssize_t sw_ovp_threshold_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->data.max_charger_voltage);
	return sprintf(buf, "%d\n", pinfo->data.max_charger_voltage);
}

static ssize_t sw_ovp_threshold_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			pinfo->data.max_charger_voltage = pinfo->data.vbus_sw_ovp_voltage;
		else
			pinfo->data.max_charger_voltage = temp;
		chr_err("%s: %d\n", __func__, pinfo->data.max_charger_voltage);

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(sw_ovp_threshold);

static ssize_t chr_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->chr_type);
	return sprintf(buf, "%d\n", pinfo->chr_type);
}

static ssize_t chr_type_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0)
		pinfo->chr_type = temp;
	else
		chr_err("%s: format error!\n", __func__);

	return size;
}

static DEVICE_ATTR_RW(chr_type);

static ssize_t pd_type_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	char *pd_type_name = "None";

	switch (pinfo->pd_type) {
	case MTK_PD_CONNECT_NONE:
		pd_type_name = "None";
		break;
	case MTK_PD_CONNECT_PE_READY_SNK:
		pd_type_name = "PD";
		break;
	case MTK_PD_CONNECT_PE_READY_SNK_PD30:
		pd_type_name = "PD";
		break;
	case MTK_PD_CONNECT_PE_READY_SNK_APDO:
		pd_type_name = "PD with PPS";
		break;
	case MTK_PD_CONNECT_TYPEC_ONLY_SNK:
		pd_type_name = "normal";
		break;
	}
	chr_err("%s: %d\n", __func__, pinfo->pd_type);
	return sprintf(buf, "%s\n", pd_type_name);
}

static DEVICE_ATTR_RO(pd_type);


static ssize_t Pump_Express_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	bool is_ta_detected = false;
	struct mtk_charger *pinfo = dev->driver_data;
	struct chg_alg_device *alg = NULL;

	if (!pinfo) {
		chr_err("%s: pinfo is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = pinfo->alg[i];
		if (alg == NULL)
			continue;
		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	chr_err("%s: idx = %d, detect = %d\n", __func__, i, is_ta_detected);
	return sprintf(buf, "%d\n", is_ta_detected);
}

static DEVICE_ATTR_RO(Pump_Express);

static ssize_t Charging_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int ret = 0, i = 0;
	char *alg_name = "normal";
	bool is_ta_detected = false;
	struct mtk_charger *pinfo = dev->driver_data;
	struct chg_alg_device *alg = NULL;

	if (!pinfo) {
		chr_err("%s: pinfo is null\n", __func__);
		return sprintf(buf, "%d\n", is_ta_detected);
	}

	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = pinfo->alg[i];
		if (alg == NULL)
			continue;
		ret = chg_alg_is_algo_ready(alg);
		if (ret == ALG_RUNNING) {
			is_ta_detected = true;
			break;
		}
	}
	if (alg == NULL)
		return sprintf(buf, "%s\n", alg_name);

	switch (alg->alg_id) {
	case PE_ID:
		alg_name = "PE";
		break;
	case PE2_ID:
		alg_name = "PE2";
		break;
	case PDC_ID:
		alg_name = "PDC";
		break;
	case PE4_ID:
		alg_name = "PE4";
		break;
	case PE5_ID:
		alg_name = "P5";
		break;
	case PE5P_ID:
		alg_name = "P5P";
		break;
	}
	chr_err("%s: charging_mode: %s\n", __func__, alg_name);
	return sprintf(buf, "%s\n", alg_name);
}

static DEVICE_ATTR_RO(Charging_mode);

static ssize_t High_voltage_chg_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: hv_charging = %d\n", __func__, pinfo->enable_hv_charging);
	return sprintf(buf, "%d\n", pinfo->enable_hv_charging);
}

static DEVICE_ATTR_RO(High_voltage_chg_enable);

static ssize_t Rust_detect_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: Rust detect = %d\n", __func__, pinfo->record_water_detected);
	return sprintf(buf, "%d\n", pinfo->record_water_detected);
}

static DEVICE_ATTR_RO(Rust_detect);

static ssize_t Thermal_throttle_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	struct charger_data *chg_data = &(pinfo->chg_data[CHG1_SETTING]);

	return sprintf(buf, "%d\n", chg_data->thermal_throttle_record);
}

static DEVICE_ATTR_RO(Thermal_throttle);

static ssize_t fast_chg_indicator_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->fast_charging_indicator);
	return sprintf(buf, "%d\n", pinfo->fast_charging_indicator);
}

static ssize_t fast_chg_indicator_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->fast_charging_indicator = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if ((pinfo->fast_charging_indicator > 0) &&
	    (pinfo->bootmode == 8 || pinfo->bootmode == 9)) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(fast_chg_indicator);

static ssize_t alg_new_arbitration_show(struct device *dev, struct device_attribute *attr,
						char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->alg_new_arbitration);
	return sprintf(buf, "%d\n", pinfo->alg_new_arbitration);
}

static ssize_t alg_new_arbitration_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->alg_new_arbitration = temp;
	else
		chr_err("%s: format error!\n", __func__);

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(alg_new_arbitration);

static ssize_t alg_unchangeable_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->alg_unchangeable);
	return sprintf(buf, "%d\n", pinfo->alg_unchangeable);
}

static ssize_t alg_unchangeable_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->alg_unchangeable = temp;
	else
		chr_err("%s: format error!\n", __func__);

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(alg_unchangeable);

static ssize_t enable_meta_current_limit_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_meta_current_limit);
	return sprintf(buf, "%d\n", pinfo->enable_meta_current_limit);
}

static ssize_t enable_meta_current_limit_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0)
		pinfo->enable_meta_current_limit = temp;
	else
		chr_err("%s: format error!\n", __func__);

	if (pinfo->enable_meta_current_limit > 0) {
		pinfo->log_level = CHRLOG_DEBUG_LEVEL;
		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(enable_meta_current_limit);

static ssize_t vbat_mon_show(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_debug("%s: %d\n", __func__, pinfo->enable_vbat_mon);
	return sprintf(buf, "%d\n", pinfo->enable_vbat_mon);
}

static ssize_t vbat_mon_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int temp;

	if (kstrtouint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_vbat_mon = false;
		else
			pinfo->enable_vbat_mon = true;
	} else {
		chr_err("%s: format error!\n", __func__);
	}

	_wake_up_charger(pinfo);
	return size;
}

static DEVICE_ATTR_RW(vbat_mon);

static ssize_t ADC_Charger_Voltage_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int vbus = get_vbus(pinfo); /* mV */

	chr_err("%s: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR_RO(ADC_Charger_Voltage);

static ssize_t ADC_Charging_Current_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int ibat = get_battery_current(pinfo); /* mA */

	chr_err("%s: %d\n", __func__, ibat);
	return sprintf(buf, "%d\n", ibat);
}

static DEVICE_ATTR_RO(ADC_Charging_Current);

static ssize_t input_current_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int aicr = 0;

	aicr = pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit;
	chr_err("%s: %d\n", __func__, aicr);
	return sprintf(buf, "%d\n", aicr);
}

static ssize_t input_current_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	struct charger_data *chg_data;
	signed int temp;

	chg_data = &pinfo->chg_data[CHG1_SETTING];
	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0)
			chg_data->thermal_input_current_limit = 0;
		else
			chg_data->thermal_input_current_limit = temp;
	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(input_current);

static ssize_t charger_log_level_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_err("%s: %d\n", __func__, pinfo->log_level);
	return sprintf(buf, "%d\n", pinfo->log_level);
}

static ssize_t charger_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			chr_err("%s: val is invalid: %d\n", __func__, temp);
			temp = 0;
		}
		pinfo->log_level = temp;
		chr_err("%s: log_level=%d\n", __func__, pinfo->log_level);

		mtk_charger_set_algo_log_level(pinfo, pinfo->log_level);
		_wake_up_charger(pinfo);

	} else {
		chr_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_log_level);

static ssize_t BatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chr_info("%s: 0x%x\n", __func__, pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t BatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			chr_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		pinfo->notify_code = reg;
		chr_info("%s: store code=0x%x\n", __func__, pinfo->notify_code);
		mtk_chgstat_notify(pinfo);
	}
	return size;
}

static DEVICE_ATTR_RW(BatteryNotify);

/* procfs */
static int mtk_chg_set_cv_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d\n", pinfo->data.battery_cv);
	return 0;
}

static int mtk_chg_set_cv_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_set_cv_show, pde_data(node));
}

static ssize_t mtk_chg_set_cv_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int cv = 0;
	struct mtk_charger *info = pde_data(file_inode(file));
	struct power_supply *psy = NULL;
	union  power_supply_propval dynamic_cv;

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &cv);
	if (ret == 0) {
		if (cv >= BATTERY_CV) {
			info->data.battery_cv = BATTERY_CV;
			chr_info("%s: adjust charge voltage %dV too high, use default cv\n",
				  __func__, cv);
		} else {
			info->data.battery_cv = cv;
			chr_info("%s: adjust charge voltage = %dV\n", __func__, cv);
		}
		psy = power_supply_get_by_name("battery");
		if (!IS_ERR_OR_NULL(psy)) {
			dynamic_cv.intval = info->data.battery_cv;
			ret = power_supply_set_property(psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &dynamic_cv);
			if (ret < 0)
				chr_err("set gauge cv fail\n");
		}
		return count;
	}

	chr_err("%s: bad argument\n", __func__);
	return count;
}

static const struct proc_ops mtk_chg_set_cv_fops = {
	.proc_open = mtk_chg_set_cv_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_set_cv_write,
};

static int mtk_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static int mtk_chg_current_cmd_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_current_cmd_show, pde_data(node));
}

static ssize_t mtk_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32] = {0};
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_dev_do_event(info->chg1_dev,
					EVENT_DISCHARGE, 0);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_dev_do_event(info->chg1_dev,
					EVENT_RECHARGE, 0);
		}

		chr_info("%s: current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static const struct proc_ops mtk_chg_current_cmd_fops = {
	.proc_open = mtk_chg_current_cmd_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_current_cmd_write,
};

static int mtk_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static int mtk_chg_en_power_path_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_power_path_show, pde_data(node));
}

static ssize_t mtk_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		chr_info("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static const struct proc_ops mtk_chg_en_power_path_fops = {
	.proc_open = mtk_chg_en_power_path_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_power_path_write,
};

static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static int mtk_chg_en_safety_timer_open(struct inode *node, struct file *file)
{
	return single_open(file, mtk_chg_en_safety_timer_show, pde_data(node));
}

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32] = {0};
	unsigned int enable = 0;
	struct mtk_charger *info = pde_data(file_inode(file));

	if (!info)
		return -EINVAL;
	if (count <= 0)
		return -EINVAL;

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return -EFAULT;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		info->safety_timer_cmd = (int)enable;
		chr_info("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

static const struct proc_ops mtk_chg_en_safety_timer_fops = {
	.proc_open = mtk_chg_en_safety_timer_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = mtk_chg_en_safety_timer_write,
};

int sc_get_sys_time(void)
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

int sc_get_left_time(int s, int e, int now)
{
	if (e >= s) {
		if (now >= s && now < e)
			return e-now;
	} else {
		if (now >= s)
			return 86400 - now + e;
		else if (now < e)
			return e-now;
	}
	return 0;
}

char *sc_solToStr(int s)
{
	switch (s) {
	case SC_IGNORE:
		return "ignore";
	case SC_KEEP:
		return "keep";
	case SC_DISABLE:
		return "disable";
	case SC_REDUCE:
		return "reduce";
	default:
		return "none";
	}
}

int smart_charging(struct mtk_charger *info)
{
	int time_to_target = 0;
	int time_to_full_default_current = -1;
	int time_to_full_default_current_limit = -1;
	int ret_value = SC_KEEP;
	int sc_real_time = sc_get_sys_time();
	int sc_left_time = sc_get_left_time(info->sc.start_time, info->sc.end_time, sc_real_time);
	int sc_battery_percentage = get_uisoc(info) * 100;
	int sc_charger_current = get_battery_current(info);

	time_to_target = sc_left_time - info->sc.left_time_for_cv;

	if (info->sc.enable == false || sc_left_time <= 0
		|| sc_left_time < info->sc.left_time_for_cv
		|| (sc_charger_current <= 0 && info->sc.last_solution != SC_DISABLE))
		ret_value = SC_IGNORE;
	else {
		if (sc_battery_percentage > info->sc.target_percentage * 100) {
			if (time_to_target > 0)
				ret_value = SC_DISABLE;
		} else {
			if (sc_charger_current != 0)
				time_to_full_default_current =
					info->sc.battery_size * 3600 / 10000 *
					(10000 - sc_battery_percentage)
						/ sc_charger_current;
			else
				time_to_full_default_current =
					info->sc.battery_size * 3600 / 10000 *
					(10000 - sc_battery_percentage);
			chr_err("sc1: %d %d %d %d %d\n",
				time_to_full_default_current,
				info->sc.battery_size,
				sc_battery_percentage,
				sc_charger_current,
				info->sc.current_limit);

			if (time_to_full_default_current < time_to_target &&
				info->sc.current_limit != -1 &&
				sc_charger_current > info->sc.current_limit) {
				time_to_full_default_current_limit =
					info->sc.battery_size / 10000 *
					(10000 - sc_battery_percentage)
					/ info->sc.current_limit;

				chr_err("sc2: %d %d %d %d\n",
					time_to_full_default_current_limit,
					info->sc.battery_size,
					sc_battery_percentage,
					info->sc.current_limit);

				if (time_to_full_default_current_limit < time_to_target &&
					sc_charger_current > info->sc.current_limit)
					ret_value = SC_REDUCE;
			}
		}
	}
	info->sc.last_solution = ret_value;
	if (info->sc.last_solution == SC_DISABLE)
		info->sc.disable_charger = true;
	else
		info->sc.disable_charger = false;
	chr_err("[sc]disable_charger: %d\n", info->sc.disable_charger);
	chr_err("[sc1]en:%d t:%d,%d,%d,%d t:%d,%d,%d,%d c:%d,%d ibus:%d uisoc: %d,%d s:%d ans:%s\n",
		info->sc.enable, info->sc.start_time, info->sc.end_time,
		sc_real_time, sc_left_time, info->sc.left_time_for_cv,
		time_to_target, time_to_full_default_current, time_to_full_default_current_limit,
		sc_charger_current, info->sc.current_limit,
		get_ibus(info), get_uisoc(info), info->sc.target_percentage,
		info->sc.battery_size, sc_solToStr(info->sc.last_solution));

	return ret_value;
}

void sc_select_charging_current(struct mtk_charger *info, struct charger_data *pdata)
{
	if (info->bootmode == 4 || info->bootmode == 1
		|| info->bootmode == 8 || info->bootmode == 9) {
		info->sc.sc_ibat = -1;	/* not normal boot */
		return;
	}
	info->sc.solution = info->sc.last_solution;
	chr_debug("debug: %d, %d, %d\n", info->bootmode,
		info->sc.disable_in_this_plug, info->sc.solution);
	if (info->sc.disable_in_this_plug == false) {
		chr_debug("sck: %d %d %d %d %d\n",
			info->sc.pre_ibat,
			info->sc.sc_ibat,
			pdata->charging_current_limit,
			pdata->thermal_charging_current_limit,
			info->sc.solution);
		if (info->sc.pre_ibat == -1 || info->sc.solution == SC_IGNORE
			|| info->sc.solution == SC_DISABLE) {
			info->sc.sc_ibat = -1;
		} else {
			if (info->sc.pre_ibat == pdata->charging_current_limit
				&& info->sc.solution == SC_REDUCE
				&& ((pdata->charging_current_limit - 100000) >= 500000)) {
				if (info->sc.sc_ibat == -1)
					info->sc.sc_ibat = pdata->charging_current_limit - 100000;

				else {
					if (info->sc.sc_ibat - 100000 >= 500000)
						info->sc.sc_ibat = info->sc.sc_ibat - 100000;
					else
						info->sc.sc_ibat = 500000;
				}
			}
		}
	}
	info->sc.pre_ibat = pdata->charging_current_limit;

	if (pdata->thermal_charging_current_limit != -1) {
		if (pdata->thermal_charging_current_limit <
		    pdata->charging_current_limit)
			pdata->charging_current_limit =
					pdata->thermal_charging_current_limit;
		info->sc.disable_in_this_plug = true;
	} else if ((info->sc.solution == SC_REDUCE || info->sc.solution == SC_KEEP)
		&& info->sc.sc_ibat <
		pdata->charging_current_limit &&
		info->sc.disable_in_this_plug == false) {
		pdata->charging_current_limit = info->sc.sc_ibat;
	}
}

void sc_init(struct smartcharging *sc)
{
	sc->enable = false;
	sc->battery_size = 3000;
	sc->start_time = 0;
	sc->end_time = 80000;
	sc->current_limit = 2000;
	sc->target_percentage = 80;
	sc->left_time_for_cv = 3600;
	sc->pre_ibat = -1;
}

static ssize_t enable_sc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[enable smartcharging] : %d\n",
	info->sc.enable);

	return sprintf(buf, "%d\n", info->sc.enable);
}

static ssize_t enable_sc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[enable smartcharging] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val == 0)
			info->sc.enable = false;
		else
			info->sc.enable = true;

		chr_err(
			"[enable smartcharging]enable smartcharging=%d\n",
			info->sc.enable);
	}
	return size;
}
static DEVICE_ATTR_RW(enable_sc);

static ssize_t sc_stime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging stime] : %d\n",
	info->sc.start_time);

	return sprintf(buf, "%d\n", info->sc.start_time);
}

static ssize_t sc_stime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging stime] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging stime] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.start_time = (int)val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.start_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_stime);

static ssize_t sc_etime_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging etime] : %d\n",
	info->sc.end_time);

	return sprintf(buf, "%d\n", info->sc.end_time);
}

static ssize_t sc_etime_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging etime] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging etime] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.end_time = (int)val;

		chr_err(
			"[smartcharging stime]enable smartcharging=%d\n",
			info->sc.end_time);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_etime);

static ssize_t sc_tuisoc_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging target uisoc] : %d\n",
	info->sc.target_percentage);

	return sprintf(buf, "%d\n", info->sc.target_percentage);
}

static ssize_t sc_tuisoc_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging tuisoc] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging tuisoc] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.target_percentage = (int)val;

		chr_err(
			"[smartcharging stime]tuisoc=%d\n",
			info->sc.target_percentage);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_tuisoc);

static ssize_t sc_ibat_limit_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	chr_err(
	"[smartcharging ibat limit] : %d\n",
	info->sc.current_limit);

	return sprintf(buf, "%d\n", info->sc.current_limit);
}

static ssize_t sc_ibat_limit_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		chr_err("[smartcharging ibat limit] buf is %s\n", buf);
		ret = kstrtol(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val < 0) {
			chr_err(
				"[smartcharging ibat limit] val is %ld ??\n",
				val);
			val = 0;
		}

		if (val >= 0)
			info->sc.current_limit = (int)val;

		chr_err(
			"[smartcharging ibat limit]=%d\n",
			info->sc.current_limit);
	}
	return size;
}
static DEVICE_ATTR_RW(sc_ibat_limit);

static ssize_t enable_power_path_show(
	struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;
	bool power_path_en = true;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	charger_dev_is_powerpath_enabled(info->chg1_dev, &power_path_en);
	return sprintf(buf, "%d\n", power_path_en);
}

static ssize_t enable_power_path_store(
	struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	long val = 0;
	int ret;
	bool enable = true;
	struct power_supply *chg_psy = NULL;
	struct mtk_charger *info = NULL;

	chg_psy = power_supply_get_by_name("mtk-master-charger");
	if (chg_psy == NULL || IS_ERR(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
		return -EINVAL;
	}
	info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
	if (info == NULL)
		return -EINVAL;

	if (buf != NULL && size != 0) {
		ret = kstrtoul(buf, 10, &val);
		if (ret == -ERANGE || ret == -EINVAL)
			return -EINVAL;
		if (val == 0)
			enable = false;
		else
			enable = true;

		charger_dev_enable_powerpath(info->chg1_dev, enable);
		info->cmd_pp = enable;
		chr_err("%s: enable power path = %d\n", __func__, enable);
	}

	return size;
}
static DEVICE_ATTR_RW(enable_power_path);

int mtk_chg_enable_vbus_ovp(bool enable)
{
	static struct mtk_charger *pinfo;
	int ret = 0;
	u32 sw_ovp = 0;
	struct power_supply *psy;

	if (pinfo == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL) {
			chr_err("[%s]psy is not rdy\n", __func__);
			return -1;
		}

		pinfo = (struct mtk_charger *)power_supply_get_drvdata(psy);
		if (pinfo == NULL) {
			chr_err("[%s]mtk_gauge is not rdy\n", __func__);
			return -1;
		}
	}

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = pinfo->data.vbus_sw_ovp_voltage;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	disable_hw_ovp(pinfo, enable);

	chr_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}
EXPORT_SYMBOL(mtk_chg_enable_vbus_ovp);

/* return false if vbus is over max_charger_voltage */
static bool mtk_chg_check_vbus(struct mtk_charger *info)
{
	int vchr = 0;

	vchr = get_vbus(info) * 1000; /* uV */
	if (vchr > info->data.max_charger_voltage) {
		chr_err("%s: vbus(%d mV) > %d mV\n", __func__, vchr / 1000,
			info->data.max_charger_voltage / 1000);
		return false;
	}
	return true;
}

//+PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time
#define SWCHG_SAFE_TIME_MAX	1800
static bool mtk_chg_check_charging_safe_time(struct mtk_charger *info)
{
	int safe_charging_time = 0;

	if (info->is_chg_done == true || info->pe5_is_chg_done == true) {
		info->safe_charging_count = 0;
	}

	info->safe_charging_count++;
	safe_charging_time = (info->safe_charging_count * info->polling_interval) /60;
	chr_debug("%s: chg_done: %d, pe5_chg_done: %d, safe charging_time(%d min)\n",
		__func__, info->is_chg_done, info->pe5_is_chg_done, safe_charging_time);

	if (safe_charging_time >= SWCHG_SAFE_TIME_MAX) {
		chr_err("%s: charging safety timeout !\n", __func__);
		info->safety_timeout = true;
	}

	return info->safety_timeout;
}
//-PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time

//+PERIDOT-35, yangpingao.wt, 20240305, mod, disable dvchg when ibat <= swchg_term
#if 0
#define SWCHG_TERM_VOLTAGE_MAX	4500
#define SWCHG_TERM_CURRENT_MAX	1700
#define SWCHG_FULL_COUNT_MAX	3
static bool mtk_chg_check_charge_full(struct mtk_charger *info)
{
	int ibat, vbat, soc = 0;

	ibat = get_battery_current(info);
	vbat = get_battery_voltage(info);
	soc = get_uisoc(info);

	chr_err("%s: battery(soc:%d, vbat:%d mV, ibat:%d mA), full_count: %d, chg_full: %d\n",
				__func__, soc, vbat, ibat, info->swchg_full_count, info->swchg_is_chg_full);
	if (info->swchg_is_chg_full == true)
		return false;

	if (vbat  >= (SWCHG_TERM_VOLTAGE_MAX -20)  &&
		ibat  <= SWCHG_TERM_CURRENT_MAX) {
		if(info->swchg_full_count++ >= SWCHG_FULL_COUNT_MAX) {
			info->swchg_is_chg_full  = true;
			chr_err("%s: battery charge full !!!\n", __func__);
			return false;
		}
	} else {
		info->swchg_full_count = 0;
	}

	return true;
}
#endif
//-PERIDOT-35, yangpingao.wt, 20240305, mod, disable dvchg when ibat <= swchg_term

static void mtk_battery_notify_VCharger_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	int vchr = 0;

	vchr = get_vbus(info) * 1000; /* uV */
	if (vchr < info->data.max_charger_voltage)
		info->notify_code &= ~CHG_VBUS_OV_STATUS;
	else {
		info->notify_code |= CHG_VBUS_OV_STATUS;
		chr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, info->data.max_charger_voltage / 1000);
		mtk_chgstat_notify(info);
	}
#endif
}

static void mtk_battery_notify_VBatTemp_check(struct mtk_charger *info)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)
	if (info->battery_temp >= info->thermal.max_charge_temp) {
		info->notify_code |= CHG_BAT_OT_STATUS;
		chr_err("[BATTERY] bat_temp(%d) out of range(too high)\n",
			info->battery_temp);
		mtk_chgstat_notify(info);
	} else {
		info->notify_code &= ~CHG_BAT_OT_STATUS;
	}

	if (info->enable_sw_jeita == true) {
		if (info->battery_temp < info->data.temp_neg_10_thres) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
	} else {
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
		if (info->battery_temp < info->thermal.min_charge_temp) {
			info->notify_code |= CHG_BAT_LT_STATUS;
			chr_err("bat_temp(%d) out of range(too low)\n",
				info->battery_temp);
			mtk_chgstat_notify(info);
		} else {
			info->notify_code &= ~CHG_BAT_LT_STATUS;
		}
#endif
	}
#endif
}

static void mtk_battery_notify_VChargerDPDM_check(struct mtk_charger *info)
{
	if (!info->dpdmov_stat)
		info->notify_code &= ~CHG_DPDM_OV_STATUS;
	else {
		info->notify_code |= CHG_DPDM_OV_STATUS;
		chr_err("[BATTERY] DP/DM over voltage!\n");
	}
	if (info->dpdmov_stat != info->lst_dpdmov_stat) {
		mtk_chgstat_notify(info);
		info->lst_dpdmov_stat = info->dpdmov_stat;
	}
}

static void mtk_battery_notify_UI_test(struct mtk_charger *info)
{
	switch (info->notify_test_mode) {
	case 1:
		info->notify_code = CHG_VBUS_OV_STATUS;
		chr_debug("[%s] CASE_0001_VCHARGER\n", __func__);
		break;
	case 2:
		info->notify_code = CHG_BAT_OT_STATUS;
		chr_debug("[%s] CASE_0002_VBATTEMP\n", __func__);
		break;
	case 3:
		info->notify_code = CHG_OC_STATUS;
		chr_debug("[%s] CASE_0003_ICHARGING\n", __func__);
		break;
	case 4:
		info->notify_code = CHG_BAT_OV_STATUS;
		chr_debug("[%s] CASE_0004_VBAT\n", __func__);
		break;
	case 5:
		info->notify_code = CHG_ST_TMO_STATUS;
		chr_debug("[%s] CASE_0005_TOTAL_CHARGINGTIME\n", __func__);
		break;
	case 6:
		info->notify_code = CHG_BAT_LT_STATUS;
		chr_debug("[%s] CASE6: VBATTEMP_LOW\n", __func__);
		break;
	case 7:
		info->notify_code = CHG_TYPEC_WD_STATUS;
		chr_debug("[%s] CASE7: Moisture Detection\n", __func__);
		break;
	default:
		chr_debug("[%s] Unknown BN_TestMode Code: %x\n",
			__func__, info->notify_test_mode);
	}
	mtk_chgstat_notify(info);
}

static void mtk_battery_notify_check(struct mtk_charger *info)
{
	if (info->notify_test_mode == 0x0000) {
		mtk_battery_notify_VCharger_check(info);
		mtk_battery_notify_VBatTemp_check(info);
		mtk_battery_notify_VChargerDPDM_check(info);
	} else {
		mtk_battery_notify_UI_test(info);
	}
}

static void mtk_chg_get_tchg(struct mtk_charger *info)
{
	int ret;
	int tchg_min = -127, tchg_max = -127;
	struct charger_data *pdata;

	pdata = &info->chg_data[CHG1_SETTING];
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);
	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (info->chg2_dev) {
		pdata = &info->chg_data[CHG2_SETTING];
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg1_dev) {
		pdata = &info->chg_data[DVCHG1_SETTING];
		ret = charger_dev_get_adc(info->dvchg1_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->dvchg2_dev) {
		pdata = &info->chg_data[DVCHG2_SETTING];
		ret = charger_dev_get_adc(info->dvchg2_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->hvdvchg1_dev) {
		pdata = &info->chg_data[HVDVCHG1_SETTING];
		ret = charger_dev_get_adc(info->hvdvchg1_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}

	if (info->hvdvchg2_dev) {
		pdata = &info->chg_data[HVDVCHG2_SETTING];
		ret = charger_dev_get_adc(info->hvdvchg2_dev,
					  ADC_CHANNEL_TEMP_JC,
					  &tchg_min, &tchg_max);
		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}
}

static void charger_check_status(struct mtk_charger *info)
{
	bool charging = true;
	bool chg_dev_chgen = true;
	int temperature;
	struct battery_thermal_protection_data *thermal;
	int uisoc = 0;

	if (get_charger_type(info) == POWER_SUPPLY_TYPE_UNKNOWN)
		return;

	temperature = info->battery_temp;
	thermal = &info->thermal;
	uisoc = get_uisoc(info);

	info->setting.vbat_mon_en = true;
	if (info->enable_sw_jeita == true || info->enable_vbat_mon != true ||
	    info->batpro_done == true)
		info->setting.vbat_mon_en = false;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temp) {
			if (temperature < thermal->min_charge_temp) {
				chr_err("Battery Under Temperature or NTC fail %d %d\n",
					temperature, thermal->min_charge_temp);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >=
				    thermal->min_charge_temp_plus_x_degree) {
					chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
					thermal->min_charge_temp,
					temperature,
					thermal->min_charge_temp_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temp) {
			chr_err("Battery over Temperature or NTC fail %d %d\n",
				temperature, thermal->max_charge_temp);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature
			    < thermal->max_charge_temp_minus_x_degree) {
				chr_err("Battery Temperature raise from %d to %d(%d), allow charging!!\n",
				thermal->max_charge_temp,
				temperature,
				thermal->max_charge_temp_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	mtk_chg_get_tchg(info);

	//+PERIDOT-35, yangpingao.wt, 20240305, mod, disable dvchg when ibat <= swchg_term
	#if 0
	if (!mtk_chg_check_charge_full(info)) {
		charging = false;
		goto stop_charging;
	}
	#endif
	//-PERIDOT-35, yangpingao.wt, 20240305, mod, disable swchg when ibat <= swchg_term

	if (!mtk_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;
	if (info->dpdmov_stat)
		charging = false;
	if (info->sc.disable_charger == true)
		charging = false;
	//+PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
	if (info->input_suspend_user_control == 1) {
		charging = false;
		mtk_charger_enable_power_path(info,CHG1_SETTING, false);
	}
	if (info->charging_enabled_user_control == 0)
		charging = false;
	if (info->start_charging_test == false)
		charging = false;
	//-PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
	//+PERIDOT-576, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control soc is over 80
	#ifdef WT_COMPILE_FACTORY_VERSION
	if (wtchg_ato_charge_manage(info) == ATO_SOC_CONTROL_DISCHARGING) {
		charging = false;
		mtk_charger_enable_power_path(info,CHG1_SETTING, false);
	}
	if (wtchg_ato_charge_manage(info) == ATO_SOC_CONTROL_CHARGING) {
		charging = true;
	}
	#endif
	//-PERIDOT-576, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control soc is over 80
	//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
	#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
	if (wtchg_is_batt_protection_setting_eu(info) == true) {
		charging = false;
	}
	if (wtchg_check_charge_full(info) == true) {
		charging = false;
	}
	#endif
	//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
stop_charging:
	//+PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time
	if (info->pre_charging == false && charging == true) {
		info->safe_charging_count = 0;
	} else if (charging == true) {
		mtk_chg_check_charging_safe_time(info);
	}
	info->pre_charging = charging;
	//-PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time

	mtk_battery_notify_check(info);

	if (charging && uisoc < 80 && info->batpro_done == true) {
		info->setting.vbat_mon_en = true;
		info->batpro_done = false;
		info->stop_6pin_re_en = false;
	}

	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d %d sc:%d %d %d saf_cmd:%d bat_mon:%d %d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat, info->dpdmov_stat, info->sc.disable_charger,
		info->can_charging, charging, info->safety_timer_cmd,
		info->enable_vbat_mon, info->batpro_done);

	charger_dev_is_enabled(info->chg1_dev, &chg_dev_chgen);

	if (charging != info->can_charging)
		_mtk_enable_charging(info, charging);
	else if (charging == false && chg_dev_chgen == true)
		_mtk_enable_charging(info, charging);

	info->can_charging = charging;
}

static bool charger_init_algo(struct mtk_charger *info)
{
	struct chg_alg_device *alg;
	int idx = 0;

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev)
		chr_err("%s, Found primary charger\n", __func__);
	else {
		chr_err("%s, *** Error : can't find primary charger ***\n"
			, __func__);
		return false;
	}

	alg = get_chg_alg_by_name("pe5p");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe5p fail\n");
	else {
		chr_err("get pe5p success\n");
		alg->config = info->config;
		alg->alg_id = PE5P_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("hvbp");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get hvbp fail\n");
	else {
		chr_err("get hvbp success\n");
		alg->config = info->config;
		alg->alg_id = HVBP_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe5");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe5 fail\n");
	else {
		chr_err("get pe5 success\n");
		alg->config = info->config;
		alg->alg_id = PE5_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe45");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe45 fail\n");
	else {
		chr_err("get pe45 success\n");
		alg->config = info->config;
		alg->alg_id = PE4_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe4");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe4 fail\n");
	else {
		chr_err("get pe4 success\n");
		alg->config = info->config;
		alg->alg_id = PE4_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pd");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pd fail\n");
	else {
		chr_err("get pd success\n");
		alg->config = info->config;
		alg->alg_id = PDC_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe2");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe2 fail\n");
	else {
		chr_err("get pe2 success\n");
		alg->config = info->config;
		alg->alg_id = PE2_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}
	idx++;

	alg = get_chg_alg_by_name("pe");
	info->alg[idx] = alg;
	if (alg == NULL)
		chr_err("get pe fail\n");
	else {
		chr_err("get pe success\n");
		alg->config = info->config;
		alg->alg_id = PE_ID;
		chg_alg_init_algo(alg);
		register_chg_alg_notifier(alg, &info->chg_alg_nb);
	}

	chr_err("config is %d\n", info->config);
	if (info->config == DUAL_CHARGERS_IN_SERIES) {
		info->chg2_dev = get_charger_by_name("secondary_chg");
		if (info->chg2_dev)
			chr_err("Found secondary charger\n");
		else {
			chr_err("*** Error : can't find secondary charger ***\n");
			return false;
		}
	} else if (info->config == DIVIDER_CHARGER ||
		   info->config == DUAL_DIVIDER_CHARGERS) {
		info->dvchg1_dev = get_charger_by_name("primary_dvchg");
		if (info->dvchg1_dev)
			chr_err("Found primary divider charger\n");
		else {
			chr_err("*** Error : can't find primary divider charger ***\n");
			return false;
		}
		if (info->config == DUAL_DIVIDER_CHARGERS) {
			info->dvchg2_dev =
				get_charger_by_name("secondary_dvchg");
			if (info->dvchg2_dev)
				chr_err("Found secondary divider charger\n");
			else {
				chr_err("*** Error : can't find secondary divider charger ***\n");
				return false;
			}
		}
	} else if (info->config == HVDIVIDER_CHARGER ||
		   info->config == DUAL_HVDIVIDER_CHARGERS) {
		info->hvdvchg1_dev = get_charger_by_name("hvdiv2_chg1");
		if (info->hvdvchg1_dev)
			chr_err("Found primary hvdivider charger\n");
		else {
			chr_err("*** Error : can't find primary hvdivider charger ***\n");
			return false;
		}
		if (info->config == DUAL_HVDIVIDER_CHARGERS) {
			info->hvdvchg2_dev = get_charger_by_name("hvdiv2_chg2");
			if (info->hvdvchg2_dev)
				chr_err("Found secondary hvdivider charger\n");
			else {
				chr_err("*** Error : can't find secondary hvdivider charger ***\n");
				return false;
			}
		}
	}

	chr_err("register chg1 notifier %d %d\n",
		info->chg1_dev != NULL, info->algo.do_event != NULL);
	if (info->chg1_dev != NULL && info->algo.do_event != NULL) {
		chr_err("register chg1 notifier done\n");
		info->chg1_nb.notifier_call = info->algo.do_event;
		register_charger_device_notifier(info->chg1_dev,
						&info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}

	chr_err("register dvchg chg1 notifier %d %d\n",
		info->dvchg1_dev != NULL, info->algo.do_dvchg1_event != NULL);
	if (info->dvchg1_dev != NULL && info->algo.do_dvchg1_event != NULL) {
		chr_err("register dvchg chg1 notifier done\n");
		info->dvchg1_nb.notifier_call = info->algo.do_dvchg1_event;
		register_charger_device_notifier(info->dvchg1_dev,
						&info->dvchg1_nb);
		charger_dev_set_drvdata(info->dvchg1_dev, info);
	}

	chr_err("register dvchg chg2 notifier %d %d\n",
		info->dvchg2_dev != NULL, info->algo.do_dvchg2_event != NULL);
	if (info->dvchg2_dev != NULL && info->algo.do_dvchg2_event != NULL) {
		chr_err("register dvchg chg2 notifier done\n");
		info->dvchg2_nb.notifier_call = info->algo.do_dvchg2_event;
		register_charger_device_notifier(info->dvchg2_dev,
						 &info->dvchg2_nb);
		charger_dev_set_drvdata(info->dvchg2_dev, info);
	}

	chr_err("register hvdvchg chg1 notifier %d %d\n",
		info->hvdvchg1_dev != NULL,
		info->algo.do_hvdvchg1_event != NULL);
	if (info->hvdvchg1_dev != NULL &&
	    info->algo.do_hvdvchg1_event != NULL) {
		chr_err("register hvdvchg chg1 notifier done\n");
		info->hvdvchg1_nb.notifier_call = info->algo.do_hvdvchg1_event;
		register_charger_device_notifier(info->hvdvchg1_dev,
						 &info->hvdvchg1_nb);
		charger_dev_set_drvdata(info->hvdvchg1_dev, info);
	}

	chr_err("register hvdvchg chg2 notifier %d %d\n",
		info->hvdvchg2_dev != NULL,
		info->algo.do_hvdvchg2_event != NULL);
	if (info->hvdvchg2_dev != NULL &&
	    info->algo.do_hvdvchg2_event != NULL) {
		chr_err("register hvdvchg chg2 notifier done\n");
		info->hvdvchg2_nb.notifier_call = info->algo.do_hvdvchg2_event;
		register_charger_device_notifier(info->hvdvchg2_dev,
						 &info->hvdvchg2_nb);
		charger_dev_set_drvdata(info->hvdvchg2_dev, info);
	}

	return true;
}

static int mtk_charger_force_disable_power_path(struct mtk_charger *info,
	int idx, bool disable);
static int mtk_charger_plug_out(struct mtk_charger *info)
{
	struct charger_data *pdata1 = &info->chg_data[CHG1_SETTING];
	struct charger_data *pdata2 = &info->chg_data[CHG2_SETTING];
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i;

	chr_err("%s\n", __func__);
	info->chr_type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->charger_thread_polling = false;
	info->pd_reset = false;
	info->dpdmov_stat = false;
	info->lst_dpdmov_stat = false;

	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;

	notify.evt = EVT_PLUG_OUT;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
		chg_alg_plugout_reset(alg);
	}
	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	charger_dev_set_input_current(info->chg1_dev, 100000);
	charger_dev_set_mivr(info->chg1_dev, info->data.min_charger_voltage);
	charger_dev_plug_out(info->chg1_dev);
	mtk_charger_force_disable_power_path(info, CHG1_SETTING, true);

	if (info->enable_vbat_mon)
		charger_dev_enable_6pin_battery_charging(info->chg1_dev, false);

	//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
	//#ifdef WT_COMPILE_FACTORY_VERSION
	info->wt_discharging_state &= DISCHARGING_BY_HIZ;
	//#endif
	info->is_fast_charging = false;
	info->pr_is_fast_charging = false;
	//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

	info->pe5_online = false;
	info->pe2_online = false;
	info->pd_online = false;
	info->qc20_online = false;
	info->real_type = WT_BATERY_TYPE_UNKNOWN;

	//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
	charger_dev_reset_qc20_ta(info->chg1_dev);
	//-PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

	info->pd_hardreset_times = 0;
	info->pe50_auth_ta_fail = false;
	info->sink_vbus_mV = 0;
	info->sink_vbus_mA = 0;

	#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
	info->running_batt_protection = false;
	info->force_is_chg_done = false;
	info->force_pe5_is_chg_done = false;
	info->batt_recharge_running = false;
	#endif

	return 0;
}

static int mtk_charger_plug_in(struct mtk_charger *info,
				int chr_type)
{
	struct chg_alg_device *alg;
	struct chg_alg_notify notify;
	int i, vbat;

	chr_debug("%s\n",
		__func__);

	info->chr_type = chr_type;
	info->usb_type = get_usb_type(info);
	info->charger_thread_polling = true;

	info->can_charging = true;
	//info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;
	info->old_cv = 0;
	info->stop_6pin_re_en = false;
	info->batpro_done = false;
	smart_charging(info);
	chr_err("mtk_is_charger_on plug in, type:%d\n", chr_type);

	vbat = get_battery_voltage(info);

	notify.evt = EVT_PLUG_IN;
	notify.value = 0;
	for (i = 0; i < MAX_ALG_NO; i++) {
		alg = info->alg[i];
		chg_alg_notifier_call(alg, &notify);
		chg_alg_set_prop(alg, ALG_REF_VBAT, vbat);
	}

	memset(&info->sc.data, 0, sizeof(struct scd_cmd_param_t_1));
	info->sc.disable_in_this_plug = false;

	#ifdef WT_COMPILE_FACTORY_VERSION
	if (info->en_charging == ATO_SOC_CONTROL_DISCHARGING && get_uisoc(info) != 80) {
		mtk_charger_enable_power_path(info,CHG1_SETTING, true);
		info->en_charging = 0;
	}
	#endif

	charger_dev_plug_in(info->chg1_dev);
	mtk_charger_force_disable_power_path(info, CHG1_SETTING, false);

	//+PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time
	info->pre_charging = false;
	charger_dev_enable_safety_timer(info->chg1_dev, false);
	info->safe_charging_count = 0;
	//-PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time

	return 0;
}

static bool mtk_is_charger_on(struct mtk_charger *info)
{
	int chr_type;

	chr_type = get_charger_type(info);
	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		if (info->chr_type != POWER_SUPPLY_TYPE_UNKNOWN) {
			mtk_charger_plug_out(info);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	} else {
		if (info->chr_type != chr_type)
			mtk_charger_plug_in(info, chr_type);

		if (info->cable_out_cnt > 0) {
			mtk_charger_plug_out(info);
			mtk_charger_plug_in(info, chr_type);
			mutex_lock(&info->cable_out_lock);
			info->cable_out_cnt = 0;
			mutex_unlock(&info->cable_out_lock);
		}
	}

	if (chr_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return false;

	return true;
}

static void charger_send_kpoc_uevent(struct mtk_charger *info)
{
	static bool first_time = true;
	ktime_t ktime_now;

	if (first_time) {
		info->uevent_time_check = ktime_get();
		first_time = false;
	} else {
		ktime_now = ktime_get();
		if ((ktime_ms_delta(ktime_now, info->uevent_time_check) / 1000) >= 60) {
			mtk_chgstat_notify(info);
			info->uevent_time_check = ktime_now;
		}
	}
}

static void kpoc_power_off_check(struct mtk_charger *info)
{
	unsigned int boot_mode = info->bootmode;
	int vbus = 0;
	int counter = 0;
	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (boot_mode == 8 || boot_mode == 9) {
		vbus = get_vbus(info);
		if (vbus >= 0 && vbus < 2500 && !mtk_is_charger_on(info) &&
		    !info->pd_reset && info->cc_hi <= 0) {
			chr_err("Unplug Charger/USB in KPOC mode, vbus=%d, shutdown\n", vbus);
			while (1) {
				if (counter >= 20000) {
					chr_err("%s, wait too long\n", __func__);
					kernel_power_off();
					break;
				}
				if (info->is_suspend == false) {
					chr_err("%s, not in suspend, shutdown\n", __func__);
					//peridot-7112, liyiying.wt, mod, 20240506, When the prototype is powered off, repeatedly plugging in the charger may lead to a situation where it doesn't charge
					mdelay(3*1000);    //peridot-4099,wt,xiaohongyu,add,20240415,show power off plug out animation
					kernel_power_off();
					break;
				} else {
					chr_err("%s, suspend! cannot shutdown\n", __func__);
					msleep(20);
				}
				counter++;
			}
		}
		charger_send_kpoc_uevent(info);
	}
}

static void charger_status_check(struct mtk_charger *info)
{
	union power_supply_propval online = {0}, status = {0};
	struct power_supply *chg_psy = NULL;
	int ret;
	bool charging = true;

	chg_psy = power_supply_get_by_name("primary_chg");
	if (IS_ERR_OR_NULL(chg_psy)) {
		chr_err("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &online);

		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_STATUS, &status);

		if (!online.intval)
			charging = false;
		else {
			if (status.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
				charging = false;
		}
	}
	if (charging != info->is_charging)
		power_supply_changed(info->psy1);
	info->is_charging = charging;
}


static char *dump_charger_type(int chg_type, int usb_type)
{
	switch (chg_type) {
	case POWER_SUPPLY_TYPE_UNKNOWN:
		return "none";
	case POWER_SUPPLY_TYPE_USB:
		if (usb_type == POWER_SUPPLY_USB_TYPE_SDP)
			return "usb";
		else
			return "nonstd";
	case POWER_SUPPLY_TYPE_USB_CDP:
		return "usb-h";
	case POWER_SUPPLY_TYPE_USB_DCP:
		return "std";
	//case POWER_SUPPLY_TYPE_USB_FLOAT:
	//	return "nonstd";
	default:
		return "unknown";
	}
}

//+PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func
struct thermal_zone_device *thermal_dev = NULL;
int temperature;
static void usb_temp_protect(struct mtk_charger *info, int compare_temp, int temp_diff)
{
	int temp_limit = compare_temp + temp_diff;
	int ret;
	thermal_dev = thermal_zone_get_zone_by_name("usb_board_ntc");
	if (IS_ERR(thermal_dev)) {
		chr_err("%s fail get thermal_dev \n", __func__);
	} else {
		ret = thermal_zone_get_temp(thermal_dev, &temperature);
		chr_err("%s  get thermal temp  is %d \n", __func__, temperature);
	}

	if (temperature > temp_limit) {
		gpio_set_value(info->vbus_mos_gpio, 1);
		chr_err("%s set vbus_mos_gpio hight \n", __func__);
	} else if (temperature < (temp_limit - compare_temp)) {
		gpio_set_value(info->vbus_mos_gpio, 0);
		chr_err("%s set vbus_mos_gpio low\n", __func__);
	}
	chr_err("%s  lyy usb_temp is %d, vbus_gpio is %d \n", __func__, temperature, info->vbus_mos_gpio);

	return;
}
//-PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func

static int usb_state = 0;
/*
* 0x81: usb suspend
* 0x80: no usb suspend
* 0x00: no usb suspend
*/
static void set_usb_state(int val)
{
	int usb_current_mask;

	chr_err("[wt_debug] %s val: %x\n", __func__, val);
	usb_current_mask = (val >> 8) & 0xFF;  // ->0x80
	val = val & 0xFF;  // ->0x0 or 0x1
	usb_state = usb_current_mask | val;
	chr_err("[wt_debug] %s usb_current_mask: %x, val: %x, usb_state: %x\n",
		__func__, usb_current_mask, val, usb_state);
}

static int charger_routine_thread(void *arg)
{
	struct mtk_charger *info = arg;
	unsigned long flags;
	unsigned int init_times = 3;
	static bool is_module_init_done;
	bool is_charger_on;
	int ret;
	int vbat_min = 0;
	int vbat_max = 0;
	u32 chg_cv = 0;

	while (1) {
		ret = wait_event_interruptible(info->wait_que,
			(info->charger_thread_timeout == true));
		if (ret < 0) {
			chr_err("%s: wait event been interrupted(%d)\n", __func__, ret);
			continue;
		}

		//+PERIDOT-7519, liyiying.wt, 20240508, add, PPS charger does not boost voltage after power-on
		if (is_module_init_done == false) {
			msleep(2000);
		}
		//-PERIDOT-7519, liyiying.wt, 20240508, add, PPS charger does not boost voltage after power-on

		while (is_module_init_done == false) {
			if (charger_init_algo(info) == true) {
				is_module_init_done = true;
				if (info->charger_unlimited) {
					info->enable_sw_safety_timer = false;
					charger_dev_enable_safety_timer(info->chg1_dev, false);
				}

				//+PERIDOT-11546, yangpingao.wt, 20240618, mod, disable sw ovp
				if (get_vbus(info) >= HVDCP_VBUS_VOLT_MIN) {
					mtk_chg_enable_vbus_ovp(false);
				}
				//-PERIDOT-11546, yangpingao.wt, 20240618, mod, disable sw ovp
			}
			else {
				if (init_times > 0) {
					chr_err("retry to init charger\n");
					init_times = init_times - 1;
					msleep(10000);
				} else {
					chr_err("holding to init charger\n");
					msleep(60000);
				}
			}
		}

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
		if (!info->charger_wakelock->active)
			__pm_stay_awake(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		info->charger_thread_timeout = false;

		info->battery_temp = get_battery_temperature(info);
		ret = charger_dev_get_adc(info->chg1_dev,
			ADC_CHANNEL_VBAT, &vbat_min, &vbat_max);
		if (ret < 0)
			chr_err("failed to get vbat_min\n");
		ret = charger_dev_get_constant_voltage(info->chg1_dev, &chg_cv);

		if (vbat_min != 0)
			vbat_min = vbat_min / 1000;

		chr_err("Vbat=%d vbats=%d vbus:%d ibus:%d I=%d T=%d uisoc:%d type:%s>%s pd:%d swchg_ibat:%d cv:%d cmd_pp:%d\n",
			get_battery_voltage(info),
			vbat_min,
			get_vbus(info),
			get_ibus(info),
			get_battery_current(info),
			info->battery_temp,
			get_uisoc(info),
			dump_charger_type(info->chr_type, info->usb_type),
			dump_charger_type(get_charger_type(info), get_usb_type(info)),
			info->pd_type, get_ibat(info), chg_cv, info->cmd_pp);

		is_charger_on = mtk_is_charger_on(info);

		//+PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func
		if (is_charger_on) {
			usb_temp_protect(info, 1000, 74000);
			#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
			if ((info->charging_enabled_user_control == 0 ||
				info->running_batt_protection == true)&&
			#else
			if (info->charging_enabled_user_control == 0 &&
			#endif
				info->usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN) {
				if(info->usb_type == POWER_SUPPLY_USB_TYPE_SDP) {
					charger_dev_set_input_current(info->chg1_dev, 500000);
				} else if (info->usb_type == POWER_SUPPLY_USB_TYPE_CDP) {
					charger_dev_set_input_current(info->chg1_dev, 1500000);
				} else {
					charger_dev_set_input_current(info->chg1_dev, 3000000);
				}
			}
		}
		//-PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func

		if (info->charger_thread_polling == true)
			mtk_charger_start_timer(info);

		check_battery_exist(info);
		check_dynamic_mivr(info);
		charger_check_status(info);
		kpoc_power_off_check(info);

		if (is_disable_charger(info) == false &&
			is_charger_on == true &&
			info->can_charging == true) {
			if (info->algo.do_algorithm)
				info->algo.do_algorithm(info);
			charger_status_check(info);
		} else {
			chr_debug("disable charging %d %d %d\n",
			    is_disable_charger(info), is_charger_on, info->can_charging);
		}
		if (info->bootmode != 1 && info->bootmode != 2 && info->bootmode != 4
			&& info->bootmode != 8 && info->bootmode != 9)
			smart_charging(info);
		spin_lock_irqsave(&info->slock, flags);
		__pm_relax(info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		chr_debug("%s end , %d\n",
			__func__, info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);

		if (info->enable_boot_volt &&
			ktime_get_seconds() > RESET_BOOT_VOLT_TIME &&
			!info->reset_boot_volt_times) {
			ret = charger_dev_set_boot_volt_times(info->chg1_dev, 0);
			if (ret < 0)
				chr_err("reset boot_battery_voltage times fails %d\n", ret);
			else {
				info->reset_boot_volt_times = 1;
				chr_err("reset boot_battery_voltage times\n");
			}
		}
	}

	return 0;
}


#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	ktime_t ktime_now;
	struct timespec64 now;
	struct mtk_charger *info;

	info = container_of(notifier,
		struct mtk_charger, pm_notifier);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		info->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		info->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		ktime_now = ktime_get_boottime();
		now = ktime_to_timespec64(ktime_now);

		if (timespec64_compare(&now, &info->endtime) >= 0 &&
			info->endtime.tv_sec != 0 &&
			info->endtime.tv_nsec != 0) {
			chr_err("%s: alarm timeout, wake up charger\n",
				__func__);
			__pm_relax(info->charger_wakelock);
			info->endtime.tv_sec = 0;
			info->endtime.tv_nsec = 0;
			_wake_up_charger(info);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	mtk_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct mtk_charger *info =
	container_of(alarm, struct mtk_charger, charger_timer);

	if (info->is_suspend == false) {
		_wake_up_charger(info);
	} else {
		__pm_stay_awake(info->charger_wakelock);
	}

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_init_timer(struct mtk_charger *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			mtk_charger_alarm_timer_func);
	mtk_charger_start_timer(info);

}

static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
	struct proc_dir_entry *battery_dir = NULL, *entry = NULL;
	struct mtk_charger *info = platform_get_drvdata(pdev);

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_ovp_threshold);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chr_type);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_meta_current_limit);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_fast_chg_indicator);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Charging_mode);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_pd_type);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_High_voltage_chg_enable);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Rust_detect);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Thermal_throttle);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_alg_new_arbitration);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_alg_unchangeable);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_vbat_mon);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_Pump_Express);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charging_Current);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;

	/* sysfs node */
	ret = device_create_file(&(pdev->dev), &dev_attr_enable_sc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_stime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_etime);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_tuisoc);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_sc_ibat_limit);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_enable_power_path);
	if (ret)
		goto _out;

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("%s: mkdir /proc/mtk_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

	entry = proc_create_data("current_cmd", 0644, battery_dir,
			&mtk_chg_current_cmd_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_power_path", 0644, battery_dir,
			&mtk_chg_en_power_path_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("en_safety_timer", 0644, battery_dir,
			&mtk_chg_en_safety_timer_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}
	entry = proc_create_data("set_cv", 0644, battery_dir,
			&mtk_chg_set_cv_fops, info);
	if (!entry) {
		ret = -ENODEV;
		goto fail_procfs;
	}

	return 0;

fail_procfs:
	remove_proc_subtree("mtk_battery_cmd", NULL);
_out:
	return ret;
}

void mtk_charger_get_atm_mode(struct mtk_charger *info)
{
	char atm_str[64] = {0};
	char *ptr = NULL, *ptr_e = NULL;
	char keyword[] = "androidboot.atm=";
	int size = 0;

	ptr = strstr(chg_get_cmd(), keyword);
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");
		if (ptr_e == 0)
			goto end;

		size = ptr_e - (ptr + strlen(keyword));
		if (size <= 0)
			goto end;
		strncpy(atm_str, ptr + strlen(keyword), size);
		atm_str[size] = '\0';
		chr_err("%s: atm_str: %s\n", __func__, atm_str);

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
	}
end:
	chr_err("%s: atm_enabled = %d\n", __func__, info->atm_enabled);
}

static int psy_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return 1;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static const enum power_supply_usb_type charger_psy_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
};

static const enum power_supply_property charger_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int psy_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct mtk_charger *info;
	struct charger_device *chg;
	int ret = 0, idx;
	struct chg_alg_device *alg = NULL;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		chr_err("%s: get info failed\n", __func__);
		return -EINVAL;
	}
	chr_debug("%s psp:%d\n", __func__, psp);

	if (info->psy1 == psy) {
		chg = info->chg1_dev;
		idx = CHG1_SETTING;
	} else if (info->psy2 == psy) {
		chg = info->chg2_dev;
		idx = CHG2_SETTING;
	} else if (info->psy_dvchg1 == psy) {
		chg = info->dvchg1_dev;
		idx = DVCHG1_SETTING;
	} else if (info->psy_dvchg2 == psy) {
		chg = info->dvchg2_dev;
		idx = DVCHG2_SETTING;
	} else if (info->psy_hvdvchg1 == psy) {
		chg = info->hvdvchg1_dev;
		idx = HVDVCHG1_SETTING;
	} else if (info->psy_hvdvchg2 == psy) {
		chg = info->hvdvchg2_dev;
		idx = HVDVCHG2_SETTING;
	} else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (idx == DVCHG1_SETTING || idx == DVCHG2_SETTING ||
		    idx == HVDVCHG1_SETTING || idx == HVDVCHG2_SETTING) {
			val->intval = false;
			alg = get_chg_alg_by_name("pe5");
			if (alg == NULL)
				chr_err("get pe5 fail\n");
			else {
				ret = chg_alg_is_algo_ready(alg);
				if (ret == ALG_RUNNING)
					val->intval = true;
			}
			break;
		}

		val->intval = is_charger_exist(info);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (chg != NULL)
			val->intval = true;
		else
			val->intval = false;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = info->enable_hv_charging;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = get_vbus(info);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = info->chg_data[idx].junction_temp_max * 10;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval =
			info->chg_data[idx].thermal_charging_current_limit;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval =
			info->chg_data[idx].thermal_input_current_limit;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_BOOT:
		val->intval = get_charger_zcv(info, chg);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		switch (info->pd_type) {
		case MTK_PD_CONNECT_PE_READY_SNK_APDO:
			val->intval = POWER_SUPPLY_USB_TYPE_PD_PPS;
			break;
		case MTK_PD_CONNECT_PE_READY_SNK:
		case MTK_PD_CONNECT_PE_READY_SNK_PD30:
			val->intval = POWER_SUPPLY_USB_TYPE_PD;
			break;
		default:
			val->intval = info->usb_type;
			break;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mtk_charger_enable_power_path(struct mtk_charger *info,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct charger_device *chg_dev = NULL;

	if (!info)
		return -EINVAL;

	switch (idx) {
	case CHG1_SETTING:
		chg_dev = get_charger_by_name("primary_chg");
		break;
	case CHG2_SETTING:
		chg_dev = get_charger_by_name("secondary_chg");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chg_dev)) {
		chr_err("%s: chg_dev not found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&info->pp_lock[idx]);
	info->enable_pp[idx] = en;

	if (info->force_disable_pp[idx])
		goto out;

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		goto out;
	}
	if (is_en == en) {
		chr_err("%s: power path is already en = %d\n", __func__, is_en);
		goto out;
	}

	pr_info("%s: enable power path = %d\n", __func__, en);
	ret = charger_dev_enable_powerpath(chg_dev, en);
out:
	mutex_unlock(&info->pp_lock[idx]);
	return ret;
}

static int mtk_charger_force_disable_power_path(struct mtk_charger *info,
	int idx, bool disable)
{
	int ret = 0;
	struct charger_device *chg_dev = NULL;

	if (!info)
		return -EINVAL;

	switch (idx) {
	case CHG1_SETTING:
		chg_dev = get_charger_by_name("primary_chg");
		break;
	case CHG2_SETTING:
		chg_dev = get_charger_by_name("secondary_chg");
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chg_dev)) {
		chr_err("%s: chg_dev not found\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&info->pp_lock[idx]);

	if (disable == info->force_disable_pp[idx])
		goto out;

	info->force_disable_pp[idx] = disable;
	ret = charger_dev_enable_powerpath(chg_dev,
		info->force_disable_pp[idx] ? false : info->enable_pp[idx]);
out:
	mutex_unlock(&info->pp_lock[idx]);
	return ret;
}

static int psy_charger_set_property(struct power_supply *psy,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	struct mtk_charger *info;
	int idx;

	chr_err("%s: prop:%d %d\n", __func__, psp, val->intval);

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		chr_err("%s: failed to get info\n", __func__);
		return -EINVAL;
	}

	if (info->psy1 == psy)
		idx = CHG1_SETTING;
	else if (info->psy2 == psy)
		idx = CHG2_SETTING;
	else if (info->psy_dvchg1 == psy)
		idx = DVCHG1_SETTING;
	else if (info->psy_dvchg2 == psy)
		idx = DVCHG2_SETTING;
	else if (info->psy_hvdvchg1 == psy)
		idx = HVDVCHG1_SETTING;
	else if (info->psy_hvdvchg2 == psy)
		idx = HVDVCHG2_SETTING;
	else {
		chr_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (val->intval > 0)
			info->enable_hv_charging = true;
		else
			info->enable_hv_charging = false;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		info->chg_data[idx].thermal_charging_current_limit =
			val->intval;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		info->chg_data[idx].thermal_input_current_limit =
			val->intval;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		if (val->intval == 0x8001)
			mtk_charger_enable_power_path(info, idx, false);
		else
			mtk_charger_enable_power_path(info, idx, true);

		set_usb_state(val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		if (val->intval > 0)
			mtk_charger_force_disable_power_path(info, idx, true);
		else
			mtk_charger_force_disable_power_path(info, idx, false);
		break;
	default:
		return -EINVAL;
	}
	_wake_up_charger(info);

	return 0;
}

static void mtk_charger_external_power_changed(struct power_supply *psy)
{
	struct mtk_charger *info;
	union power_supply_propval prop = {0};
	union power_supply_propval prop2 = {0};
	union power_supply_propval vbat0 = {0};
	struct power_supply *chg_psy = NULL;
	int ret;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
	if (info == NULL) {
		pr_notice("%s: failed to get info\n", __func__);
		return;
	}
	chg_psy = info->chg_psy;

	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
		chg_psy = power_supply_get_by_name("primary_chg");
		info->chg_psy = chg_psy;
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop2);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ENERGY_EMPTY, &vbat0);
	}

	if (info->vbat0_flag != vbat0.intval) {
		if (vbat0.intval) {
			info->enable_vbat_mon = false;
			charger_dev_enable_6pin_battery_charging(info->chg1_dev, false);
		} else
			info->enable_vbat_mon = info->enable_vbat_mon_bak;

		info->vbat0_flag = vbat0.intval;
	}

	pr_notice("%s event, name:%s online:%d type:%d vbus:%d\n", __func__,
		psy->desc->name, prop.intval, prop2.intval,
		get_vbus(info));

	_wake_up_charger(info);
}

int notify_adapter_event(struct notifier_block *notifier,
			unsigned long evt, void *val)
{
	struct mtk_charger *pinfo = NULL;
	u32 boot_mode = 0;
	bool report_psy = true;

	chr_err("%s %lu\n", __func__, evt);

	pinfo = container_of(notifier,
		struct mtk_charger, pd_nb);
	boot_mode = pinfo->bootmode;

	switch (evt) {
	case MTK_SINK_VBUS:
		//+Peridot-9631,xiaohongyu,wt, 20241023, mod, add input current different to wake up charger
		if (pinfo->sink_vbus_mA != pinfo->sink_vbus_mA_old || pinfo->sink_vbus_mV != pinfo->sink_vbus_mV_old) {
			chr_debug("%s: SINK VBUS:%d mV %d mA\n", __func__,pinfo->sink_vbus_mV,pinfo->sink_vbus_mA);
			_wake_up_charger(pinfo);
		}
		pinfo->sink_vbus_mA_old = pinfo->sink_vbus_mA;
		pinfo->sink_vbus_mV_old = pinfo->sink_vbus_mV;
		//-Peridot-9631,xiaohongyu,wt, 20241023, mod, add input current different to wake up charger
		break;

	case MTK_PD_CONNECT_NONE:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify Detach\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_DETACH, 0);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_HARD_RESET:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify HardReset\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		pinfo->pd_reset = true;
		//+Peridot-1559, liyiying.wt, 20240522, mod, when charging with a 30W charger charging process is intermittent
		if (pinfo->pd_hardreset_times < HARD_RESET_TIMES) {
			pinfo->pd_hardreset_times++;
		}
		//-Peridot-1559, liyiying.wt, 20240522, mod, when charging with a 30W charger charging process is intermittent
		mutex_unlock(&pinfo->pd_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_HARDRESET, 0);
		_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_SOFT_RESET:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify SoftReset\n");
		pinfo->pd_type = MTK_PD_CONNECT_SOFT_RESET;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		mtk_chg_alg_notify_call(pinfo, EVT_SOFTRESET, 0);
		_wake_up_charger(pinfo);
		/* reset PE50 */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify fixed voltage ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		/* PD is ready */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK_PD30:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify PD30 ready\r\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		/* PD30 is ready */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK_APDO:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify APDO Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		/* PE40 is ready */
		_wake_up_charger(pinfo);
		break;

	case MTK_PD_CONNECT_TYPEC_ONLY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chr_err("PD Notify Type-C Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_TYPEC_ONLY_SNK;
		pinfo->pd_reset = false;
		mutex_unlock(&pinfo->pd_lock);
		/* type C is ready */
		_wake_up_charger(pinfo);
		break;
	case MTK_TYPEC_WD_STATUS:
		chr_err("wd status = %d\n", *(bool *)val);
		pinfo->water_detected = *(bool *)val;
		if (pinfo->water_detected == true) {
			pinfo->notify_code |= CHG_TYPEC_WD_STATUS;
			pinfo->record_water_detected = true;
			if (boot_mode == 8 || boot_mode == 9)
				pinfo->enable_hv_charging = false;
		} else {
			pinfo->notify_code &= ~CHG_TYPEC_WD_STATUS;
			if (boot_mode == 8 || boot_mode == 9)
				pinfo->enable_hv_charging = true;
		}
		mtk_chgstat_notify(pinfo);
		report_psy = boot_mode == 8 || boot_mode == 9;
		break;
	case MTK_TYPEC_CC_HI_STATUS:
		chr_err("cc_hi = %d\n", *(int *)val);
		pinfo->cc_hi = *(int *)val;
		_wake_up_charger(pinfo);
		break;
	}
	if (report_psy)
		power_supply_changed(pinfo->psy1);
	return NOTIFY_DONE;
}

int chg_alg_event(struct notifier_block *notifier,
			unsigned long event, void *data)
{
	chr_err("%s: evt:%lu\n", __func__, event);

	return NOTIFY_DONE;
}

static char *mtk_charger_supplied_to[] = {
	"battery"
};

//+Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel
#if IS_ENABLED(CONFIG_LENOVO_CHARGE_SYSFS)
static int wtchg_get_mm8013_uisoc(int *soc)
{
	int ret;
	struct power_supply *ext_psy;
	union power_supply_propval ext_soc;

	ext_psy = power_supply_get_by_name("battery_mm8013");
	if (ext_psy == NULL) {
		chr_err("[%s] mm8013 psy is not ready\n", __func__);
		return false;
	} else {
		ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_CAPACITY, &ext_soc);
		if (ret < 0) {
			chr_err("[%s] mm8013 get soc fail\n", __func__);
			return false;
		} else {
			*soc = ext_soc.intval;
		}
	}

	return true;
}

static bool wtchg_check_charge_full(struct mtk_charger *info)
{
	int uisoc, ret;

	ret = wtchg_get_mm8013_uisoc(&uisoc);
	if (ret != true) {
		return false;
	}

	if ((uisoc <= info->batt_recharge_setting)) {
		info->force_is_chg_done = false;
		info->force_pe5_is_chg_done = false;
	}

	if ((info->force_is_chg_done ||info->force_pe5_is_chg_done) &&
		(info->batt_recharge_setting != 0)
		//&& (uisoc == 100)) {
		) {
		info->batt_recharge_running = true;
	}

	if ((uisoc <= info->batt_recharge_setting) &&
		(info->batt_recharge_running == true)) {

		struct chg_alg_device *alg;
		alg = get_chg_alg_by_name("pe5");
		chg_alg_set_prop(alg, ALG_CHG_DONE, false);

		info->batt_recharge_running = false;
	}

	chr_info("%s is_chg_done:%d, pe5_is_chg_done:%d, uisoc:%d, batt_recharge_setting:%d\n", __func__,
		info->is_chg_done, info->pe5_is_chg_done, uisoc, info->batt_recharge_setting);
	chr_info("%s force_is_chg_done:%d, force_pe5_is_chg_done:%d, batt_recharge_running:%d\n", __func__,
		info->force_is_chg_done, info->force_pe5_is_chg_done, info->batt_recharge_running);

	return info->batt_recharge_running;
}

static bool wtchg_is_batt_protection_setting_eu(struct mtk_charger *info)
{
	int mm8013_soc, ret;
	struct power_supply *ext_psy;
	union power_supply_propval ext_soc;
	bool stop_charging = false;

	chr_info("%s %d %d %d", __func__, info->batt_protection_a, info->batt_protection_bb, info->batt_protection_ccc);

	ext_psy = power_supply_get_by_name("battery_mm8013");
	if (ext_psy == NULL) {
		chr_err("[%s] mm8013 psy is not ready\n", __func__);
		return false;
	} else {
		ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_CAPACITY, &ext_soc);
		if (ret < 0) {
			chr_err("[%s] mm8013 get soc fail\n", __func__);
			return false;
		} else {
			mm8013_soc = ext_soc.intval;
		}
	}

	if (info->batt_protection_setting_eu == 0) {
		info->running_batt_protection = false;
		return false;
	} else {
		/*
		* A 6-digit integer type, ABBCCC
		*
		* A: 0 indicates the charging protection function is off,
			1 indicates the charging protection function is on
		* BB: Lower limit of power control
		* CCC: Upper limit of power control
		*/
		if (info->batt_protection_setting_eu < 100000 ||
			info->batt_protection_setting_eu > 199100) {
			//0BBCCC
			info->running_batt_protection = false;
			return false;
		} else {
			info->batt_protection_a = 1;
			info->batt_protection_bb = (info->batt_protection_setting_eu / 1000) % 100;
			info->batt_protection_ccc = info->batt_protection_setting_eu % 1000;

			if (info->batt_protection_bb == 0) {
				if (info->batt_recharge_setting != 0 &&
					info->batt_recharge_setting < info->batt_protection_ccc) {
					info->batt_protection_bb = info->batt_recharge_setting;
				} else {
					info->batt_protection_bb = 1;
				}
			}

			if ((info->wt_discharging_state & DISCHARGING_STATE_PROTECT) != 0 &&
				(mm8013_soc <= info->batt_protection_bb)) {
				info->wt_discharging_state &= ~DISCHARGING_STATE_PROTECT;
				if ((info->wt_discharging_state & DISCHARGING_STATE_PROTECT) == 0) {
					chr_info("%s re-charging, capacity: %d %d %d\n", __func__, mm8013_soc, info->batt_protection_bb, info->batt_protection_ccc);
					stop_charging = false;
					info->running_batt_protection = false;
				}
			} else if ((info->wt_discharging_state & DISCHARGING_STATE_PROTECT) != 0 ||
				(mm8013_soc >= info->batt_protection_ccc)) {
				info->wt_discharging_state |= DISCHARGING_STATE_PROTECT;
				chr_info("%s stop-charging, capacity: %d %d %d \n", __func__, mm8013_soc, info->batt_protection_bb, info->batt_protection_ccc);
				stop_charging = true;
				info->running_batt_protection = true;
			}

			chr_info("%s outside\n", __func__);
		}
	}

	return stop_charging;
}
#endif
//-Peridot-8228, liyiying.wt, 20240625, EU Battery Characteristics Integrated Kernel

//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
#ifdef WT_COMPILE_FACTORY_VERSION
static void wtchg_stay_awake(struct wtchg_wakeup_source *source)
{
	if (__test_and_clear_bit(0, &source->disabled)) {
		__pm_stay_awake(source->source);
		pr_debug("enable source %s", source->source->name);
	}
}

static void wtchg_relax(struct wtchg_wakeup_source *source)
{
	if (!__test_and_clear_bit(0, &source->disabled)) {
		__pm_relax(source->source);
		pr_debug("disable source %s", source->source->name);
	}
}

static bool wtchg_wake_active(struct wtchg_wakeup_source *source)
{
	return !source->disabled;
}

static void wtchg_ato_soc_user_control_work(struct work_struct *work)
{
	struct mtk_charger *info;

	info = container_of(work, struct mtk_charger, ato_soc_user_control_work.work);

	info->ato_soc_user_control  = false;
	chr_err("%s: user close ato control timeout\n", __func__);

	if (wtchg_wake_active(&info->wtchg_ato_soc_wake_source)) {
		wtchg_relax(&info->wtchg_ato_soc_wake_source);
	}
}

#define ATO_BATT_SOC_MAX	80
#define ATO_BATT_SOC_MIN	60
static int wtchg_ato_charge_manage(struct mtk_charger *info)
{
	int ret = 0, en_charging = 0;
	int mm8013_soc;
	#if IS_ENABLED(CONFIG_GAUGE_MM8013A)
	struct power_supply *ext_psy;
	union power_supply_propval ext_soc;
	#endif

	#if IS_ENABLED(CONFIG_GAUGE_MM8013A)
	ext_psy = power_supply_get_by_name("battery_mm8013");
	if (ext_psy == NULL) {
		chr_err("[%s] mm8013 psy is not ready\n", __func__);
	} else {
		ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_CAPACITY, &ext_soc);
		if (ret < 0) {
			chr_err("[%s] mm8013 get soc fail\n", __func__);
		} else {
			mm8013_soc = ext_soc.intval;
		}
	}
	#endif

	if ((info->wt_discharging_state & DISCHARGING_STATE_ATO) != 0 &&
		(info->ato_soc_user_control || mm8013_soc <= ATO_BATT_SOC_MIN)) {
		info->wt_discharging_state &= ~DISCHARGING_STATE_ATO;
		chr_err("%s capacity: %d %d, ato_soc_user_control: %d, wt_discharging_state: %d, ready charging\n", __func__,
						mm8013_soc, ATO_BATT_SOC_MIN, info->ato_soc_user_control, info->wt_discharging_state);
		if ((info->wt_discharging_state & DISCHARGING_BY_DISABLE) == 0) {
			/*!!!!enable charging!!!!!*/
			//charger_dev_enable(info->chg1_dev, true);
			mtk_charger_enable_power_path(info,CHG1_SETTING, true);
			en_charging = ATO_SOC_CONTROL_CHARGING;
			info->en_charging = ATO_SOC_CONTROL_CHARGING;
			chr_err("%s start charging\n", __func__);
			msleep(10);
		}
	} else if((info->wt_discharging_state & DISCHARGING_STATE_ATO) != 0 ||
		(!info->ato_soc_user_control && mm8013_soc >= ATO_BATT_SOC_MAX)){
		info->wt_discharging_state |= DISCHARGING_STATE_ATO;
		/*!!!!disable charging!!!!!*/
		mtk_charger_enable_power_path(info,CHG1_SETTING, false);
		en_charging = ATO_SOC_CONTROL_DISCHARGING;
		info->en_charging = ATO_SOC_CONTROL_DISCHARGING;
		chr_err("%s capacity: %d %d, stop charging\n", __func__, mm8013_soc, ATO_BATT_SOC_MAX);
	}

	return en_charging;
}
#endif
//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

//+PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node
int _store_cc_orientation;
void store_cc_orientation(int cc_ori)
{
	_store_cc_orientation = cc_ori;
}
EXPORT_SYMBOL(store_cc_orientation);

static ssize_t typec_cc_orientation_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	//struct mt6375_chg_data *ddata = container_of(c, struct mt6375_chg_data, battery_class);

	//if (kstrtou32(buf, 0, &ddata->typec_cc_orientation)) {
	if (kstrtou32(buf, 0, &_store_cc_orientation)) {
		return -EINVAL;
	}

	return count;
}

static ssize_t typec_cc_orientation_show(struct class *c, struct class_attribute *attr, char *buf)
{
	//struct mt6375_chg_data *ddata = container_of(c, struct mt6375_chg_data, battery_class);

	//return scnprintf(buf, PAGE_SIZE, "%d\n", ddata->typec_cc_orientation);
	return scnprintf(buf, PAGE_SIZE, "%d\n", _store_cc_orientation);
}

static CLASS_ATTR_RW(typec_cc_orientation);

//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
#ifdef WT_COMPILE_FACTORY_VERSION
static ssize_t ato_soc_user_control_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		if (info->ato_soc_user_control == user_control) {
			return count;
		}

		info->ato_soc_user_control = !!user_control;

		if (info->ato_soc_user_control) {
			if (!wtchg_wake_active(&info->wtchg_ato_soc_wake_source)) {
				wtchg_stay_awake(&info->wtchg_ato_soc_wake_source);
			}
			cancel_delayed_work_sync(&info->ato_soc_user_control_work);
			schedule_delayed_work(&info->ato_soc_user_control_work, msecs_to_jiffies(60000));  //1mins
		} else {
			cancel_delayed_work_sync(&info->ato_soc_user_control_work);
			if (wtchg_wake_active(&info->wtchg_ato_soc_wake_source)) {
				wtchg_relax(&info->wtchg_ato_soc_wake_source);
			}
		}

		_wake_up_charger(info);
	}

	return count;
}

static ssize_t ato_soc_user_control_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->ato_soc_user_control);
}
static CLASS_ATTR_RW(ato_soc_user_control);
#endif
//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

//+PERIDOT-35, liyiying.wt, 20240306, add, add stoping_test node and startcharging_test node for mmi test
static ssize_t stopcharging_test_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	info->start_charging_test = false;

	charger_dev_enable(info->chg1_dev, info->start_charging_test);
	//charger_dev_enable(info->dvchg1_dev, info->start_charging_test);
	//charger_dev_do_event(info->chg1_dev, EVENT_DISCHARGE, 0);
	//mtk_charger_force_disable_power_path(info,CHG1_SETTING, !(info->start_charging_test));
	mtk_charger_enable_power_path(info, CHG1_SETTING, info->start_charging_test);
	chr_err("%s\n", __func__);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->start_charging_test);
}
static CLASS_ATTR_RO(stopcharging_test);

static ssize_t startcharging_test_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	info->start_charging_test = true;

	charger_dev_enable(info->chg1_dev, info->start_charging_test);
	//charger_dev_enable(info->dvchg1_dev, info->start_charging_test);
	//charger_dev_do_event(info->chg1_dev, EVENT_RECHARGE, 0);
	//mtk_charger_force_disable_power_path(info,CHG1_SETTING, !(info->start_charging_test));
	mtk_charger_enable_power_path(info, CHG1_SETTING, info->start_charging_test);
	chr_err("%s\n", __func__);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->start_charging_test);
}
static CLASS_ATTR_RO(startcharging_test);
//-PERIDOT-35, liyiying.wt, 20240306, add, add stoping_test node and startcharging_test node for mmi test


#define USER_SET_CURRENT_MIN		0
#define USER_SET_CURRENT_MAX		9000
static ssize_t user_set_charge_current_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int ibat_fcc;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &ibat_fcc) == 0) {
		if (info->user_set_charge_current == ibat_fcc) {
			return count;
		}

		if (ibat_fcc <= USER_SET_CURRENT_MIN) {
			info->user_set_charge_current_flag = false;
			info->user_set_charge_current = USER_SET_CURRENT_MIN;
		} else if (ibat_fcc >= USER_SET_CURRENT_MAX){
			info->user_set_charge_current_flag = true;
			info->user_set_charge_current = USER_SET_CURRENT_MAX * 1000;
		} else {
			info->user_set_charge_current_flag = true;
			info->user_set_charge_current = ibat_fcc * 1000;
		}
		_wake_up_charger(info);
	}

	return count;
}

static ssize_t user_set_charge_current_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->user_set_charge_current);
}

static CLASS_ATTR_RW(user_set_charge_current);


//+PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)
static ssize_t pd_pe_online_state_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;
	int online = 0;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (info->is_fast_charging == true) {
		online = 1;
	} else if ((info->chr_type == POWER_SUPPLY_TYPE_USB) && (info->usb_type != POWER_SUPPLY_USB_TYPE_SDP)) {
		online = 2;
	}

	chr_err("%s online is %d\n", __func__, online);

	return scnprintf(buf, PAGE_SIZE, "%d\n", online);
}
static CLASS_ATTR_RO(pd_pe_online_state);
//-PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)

//+PERIDOT-193,xiaohongyu,wt,mod,20240411,add power off charging node(0:normal 1:PD_PPS 2:FLOAT)
static ssize_t kpoc_pd_pe_online_state_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;
	int online = 0;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (info->is_fast_charging == true || info->qc20_online == true) {
		online = 1;
	} else if ((info->chr_type == POWER_SUPPLY_TYPE_USB) && (info->usb_type != POWER_SUPPLY_USB_TYPE_SDP)) {
		online = 2;
	}

	chr_err("%s online is %d\n", __func__, online);

	return scnprintf(buf, PAGE_SIZE, "%d\n", online);
}
static CLASS_ATTR_RO(kpoc_pd_pe_online_state);
//-PERIDOT-193,xiaohongyu,wt,mod,20240411,add power off charging node(0:normal 1:PD_PPS 2:FLOAT)

//+PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
static ssize_t input_suspend_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		info->input_suspend_user_control = user_control;

		chr_err("%s input_suspend_user_control is %d, user_control is %d\n", __func__, info->input_suspend_user_control, user_control);

		if (info->input_suspend_user_control == 1) {
			mtk_charger_force_disable_power_path(info,CHG1_SETTING, true);  //set  true
			mtk_charger_enable_power_path(info,CHG1_SETTING, false);
			/* how to control cp */
			//_mtk_enable_charging(info, false);  //set false
		} else if (info->input_suspend_user_control == 0) {
			mtk_charger_force_disable_power_path(info,CHG1_SETTING, false);  //set false
			mtk_charger_enable_power_path(info,CHG1_SETTING, true);
			/* how to control cp */
			//_mtk_enable_charging(info, true);  //set true
		}else {
			chr_err("%s Invalid param\n", __func__);
		}
	}

	return count;
}

static ssize_t input_suspend_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s input_suspend_user_control is %d\n", __func__, info->input_suspend_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->input_suspend_user_control);
}
static CLASS_ATTR_RW(input_suspend);

static ssize_t charging_enabled_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		info->charging_enabled_user_control = user_control;

		chr_err("%s charging_enabled_user_control is %d, user_control is %d\n", __func__, info->charging_enabled_user_control, user_control);

		if (info->charging_enabled_user_control == 0) {
			charger_dev_enable(info->chg1_dev, info->charging_enabled_user_control);
			/* need add disable cp charging */
			//_mtk_enable_charging(info, false);  //set false
		} else if (info->charging_enabled_user_control == 1 &&
				(info->wt_discharging_state & DISCHARGING_BY_DISABLE) == 0 &&
				info->can_charging != false) {
			charger_dev_enable(info->chg1_dev, info->charging_enabled_user_control);
			/* need add enable cp charging */
			//_mtk_enable_charging(info, true);  //set true
		} else {
			chr_err("%s Invalid param: %d\n", __func__, user_control);
		}
	}

	return count;
}

static ssize_t charging_enabled_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s charging_enabled_user_control is %d\n", __func__, info->charging_enabled_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->charging_enabled_user_control);
}
static CLASS_ATTR_RW(charging_enabled);
//-PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection

//+PERIDOT-203, liyiying.wt, mod, 20240313, add aidl shipmode interface
static ssize_t shipping_mode_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		if (user_control == 5526789) {
			info->shipping_mode_user_control = 1;
			//charger_dev_set_shipping_mode(info->chg1_dev, user_control);
			chr_err("%s shipping_mode_user_control is %d user_control is %d\n", __func__, info->shipping_mode_user_control, user_control);
		} else {
			info->shipping_mode_user_control = 0;
		}
	}

	return count;
}

static ssize_t shipping_mode_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s shipping_mode_user_control is %d\n", __func__, info->shipping_mode_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->shipping_mode_user_control);
}
static CLASS_ATTR_RW(shipping_mode);
//-PERIDOT-203, liyiying.wt, mod, 20240313, add aidl shipmode interface

#define USER_SET_BATT_TEMP_MIN		(-200)
#define USER_SET_BATT_TEMP_MAX		700
static ssize_t user_set_batt_temp_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int debug_temp;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &debug_temp) == 0) {
		if (info->user_set_batt_temp == debug_temp) {
			return count;
		}

		if (debug_temp <= USER_SET_BATT_TEMP_MIN) {
			info->user_set_batt_temp_flag = false;
			info->user_set_batt_temp = USER_SET_BATT_TEMP_MIN;
		} else if (debug_temp >= USER_SET_BATT_TEMP_MAX){
			info->user_set_batt_temp_flag = true;
			info->user_set_batt_temp = USER_SET_BATT_TEMP_MAX;
		} else {
			info->user_set_batt_temp_flag = true;
			info->user_set_batt_temp = debug_temp;
		}
		_wake_up_charger(info);
	}

	return count;
}

static ssize_t user_set_batt_temp_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->user_set_batt_temp);
}

static CLASS_ATTR_RW(user_set_batt_temp);

static ssize_t swchg_ibat_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	//do nothing

	return count;
}

static ssize_t swchg_ibat_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;
	int swchg_ibat;

	info = container_of(c,
		struct mtk_charger, battery_class);

	swchg_ibat = get_ibat(info) * 1000;
	return scnprintf(buf, PAGE_SIZE, "%d\n", swchg_ibat);
}

static CLASS_ATTR_RW(swchg_ibat);

//+PERIDOT-198, liyiying.wt, mod, 20240316, battery and charging - battery maintenance
static ssize_t battery_maintenance_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		info->battery_maintenance_v10_user_control = user_control;
	}

	chr_err("%s battery_maintenance_v10_user_control is %d\n", __func__, info->battery_maintenance_v10_user_control);

	return count;
}

static ssize_t battery_maintenance_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s battery_maintenance_v10_user_control is %d\n", __func__, info->battery_maintenance_v10_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->battery_maintenance_v10_user_control);
}
static CLASS_ATTR_RW(battery_maintenance);
//-PERIDOT-198, liyiying.wt, mod, 20240316, battery and charging - battery maintenance

//+PERIDOT-204, liyiying.wt, mod, 20240316, battery and charging - battery maintenance 2.0
static ssize_t battery_maintenance20_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		info->battery_maintenance_v20_user_control = user_control;
	}

	return count;
}

static ssize_t battery_maintenance20_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s battery_maintenance_v20_user_control is %d\n", __func__, info->battery_maintenance_v20_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->battery_maintenance_v20_user_control);
}
static CLASS_ATTR_RW(battery_maintenance20);

static ssize_t battery_cycle_store(struct class *c, struct class_attribute *attr, const  char *buf, size_t count)
{
	struct mtk_charger *info;
	int user_control;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (kstrtoint(buf, 10, &user_control) == 0) {
		info->battery_cycle_user_control = user_control;
	}

	return count;
}

static ssize_t battery_cycle_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	chr_err("%s battery_cycle_user_control is %d\n", __func__, info->battery_cycle_user_control);

	return scnprintf(buf, PAGE_SIZE, "%d\n", info->battery_cycle_user_control);
}
static CLASS_ATTR_RW(battery_cycle);
//-PERIDOT-204, liyiying.wt, mod, 20240316, battery and charging - battery maintenance 2.0

//+PERIDOT-35, liyiying.wt, add, 20240420, add real_type node
static const char *get_usb_type_name(u32 usb_type)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(real_type_test); i++) {
		if (i == usb_type) {
			return real_type_test[i];
		}
	}

	return "Unknown";
}

static ssize_t real_type_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct mtk_charger *info;

	info = container_of(c,
		struct mtk_charger, battery_class);

	if (info->pe5_online == true) {
		info->real_type = WT_BATERY_TYPE_PD_PPS;
	} else if (info->pd_online == true) {
		info->real_type = WT_BATERY_TYPE_PD;
	} else if (info->pe2_online == true) {
		info->real_type = WT_BATERY_TYPE_PE2;
	} else if (info->qc20_online == true) {
		info->real_type = WT_BATERY_TYPE_HVDCP;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB) {
		if (info->usb_type != POWER_SUPPLY_USB_TYPE_SDP) {
			info->real_type = WT_BATERY_TYPE_BRICKID;
		} else {
			info->real_type = WT_BATERY_TYPE_SDP;
		}
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_CDP) {
		info->real_type = WT_BATERY_TYPE_CDP;
	} else if (info->chr_type == POWER_SUPPLY_TYPE_USB_DCP) {
		info->real_type = WT_BATERY_TYPE_DCP;
	} else {
		info->real_type = WT_BATERY_TYPE_UNKNOWN;
	}

	chr_err("%s real_type is %d\n", __func__, info->real_type);

	return scnprintf(buf, PAGE_SIZE, "%s\n", get_usb_type_name(info->real_type));
}
static CLASS_ATTR_RO(real_type);
//-PERIDOT-35, liyiying.wt, add, 20240420, add real_type node

//+Peridot-7655, liyiying.wt, add, 20240515, add battery_healthd node
static ssize_t battery_healthd_show(struct class *c, struct class_attribute *attr, char *buf)
{
	struct power_supply *ext_psy;
	union power_supply_propval ext_battery_healthd = {0};
	int ret = 0;
	int battery_healthd_status = 0;

	ext_psy = power_supply_get_by_name("battery_mm8013");

	if (ext_psy == NULL) {
		chr_info("[%s] mm8013 psy is not ready \n", __func__);
	} else {
		ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_PRECHARGE_CURRENT, &ext_battery_healthd);
		if (ret < 0) {
			chr_info("[%s] power_supply_get_property fail  \n", __func__);
		} else {
			battery_healthd_status = ext_battery_healthd.intval;
			chr_debug("[%s] ext_battery_healthd is %d \n", __func__, ext_battery_healthd.intval);
		}
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n", battery_healthd_status);
}
static CLASS_ATTR_RO(battery_healthd);
//-Peridot-7655, liyiying.wt, add, 20240515, add battery_healthd node

static struct attribute *battery_class_attrs[] = {
	&class_attr_typec_cc_orientation.attr,
	#ifdef WT_COMPILE_FACTORY_VERSION
	&class_attr_ato_soc_user_control.attr,
	#endif
	&class_attr_stopcharging_test.attr,
	&class_attr_startcharging_test.attr,
	&class_attr_user_set_charge_current.attr,
	&class_attr_pd_pe_online_state.attr,
	&class_attr_input_suspend.attr,
	&class_attr_charging_enabled.attr,
	&class_attr_shipping_mode.attr,
	&class_attr_user_set_batt_temp.attr,
	&class_attr_swchg_ibat.attr,
	&class_attr_battery_maintenance.attr,
	&class_attr_battery_maintenance20.attr,
	&class_attr_battery_cycle.attr,
	&class_attr_kpoc_pd_pe_online_state.attr,
	&class_attr_real_type.attr,
	&class_attr_battery_healthd.attr,
	NULL,
};
ATTRIBUTE_GROUPS(battery_class);

static struct attribute *battery_attributes[] = {
	NULL,
};

static const struct attribute_group battery_attr_group = {
	.attrs = battery_attributes,
};

static const struct attribute_group *battery_attr_groups[] = {
	&battery_attr_group,
	NULL,
};

static int wt_init_dev_class(struct mtk_charger *info, struct device *dev)
{
	int rc = -EINVAL;

	info->battery_class.name = "wt-battery";
	info->battery_class.class_groups = battery_class_groups;
	rc = class_register(&info->battery_class);
	if (rc < 0) {
		chr_err("%s: Failed to create battery_class rc=%d\n", __func__, rc);
	}

	info->batt_device.class = &info->battery_class;
	dev_set_name(&info->batt_device, "odm_battery");
	info->batt_device.parent = dev;
	info->batt_device.groups = battery_attr_groups;
	rc = device_register(&info->batt_device);
	if (rc < 0) {
		chr_err("%s: Failed to create battery_class rc=%d\n", __func__, rc);
	}

	return rc;
}
//-PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node

static int mtk_charger_probe(struct platform_device *pdev)
{
	struct mtk_charger *info = NULL;
	int i;
	char *name = NULL;
	int ret;

	chr_err("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mtk_charger_parse_dt(info, &pdev->dev);

	mutex_init(&info->cable_out_lock);
	mutex_init(&info->charger_lock);
	mutex_init(&info->pd_lock);
	for (i = 0; i < CHG2_SETTING + 1; i++) {
		mutex_init(&info->pp_lock[i]);
		info->force_disable_pp[i] = false;
		info->enable_pp[i] = true;
	}
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s",
		"charger suspend wakelock");
	info->charger_wakelock =
		wakeup_source_register(NULL, name);
	spin_lock_init(&info->slock);

	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	mtk_charger_init_timer(info);
#ifdef CONFIG_PM
	if (register_pm_notifier(&info->pm_notifier)) {
		chr_err("%s: register pm failed\n", __func__);
		return -ENODEV;
	}
	info->pm_notifier.notifier_call = charger_pm_event;
#endif /* CONFIG_PM */
	srcu_init_notifier_head(&info->evt_nh);
	mtk_charger_setup_files(pdev);
	mtk_charger_get_atm_mode(info);

	for (i = 0; i < CHGS_SETTING_MAX; i++) {
		info->chg_data[i].thermal_charging_current_limit = -1;
		info->chg_data[i].thermal_input_current_limit = -1;
		info->chg_data[i].input_current_limit_by_aicl = -1;
	}
	info->enable_hv_charging = true;

	info->psy_desc1.name = "mtk-master-charger";
	info->psy_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc1.usb_types = charger_psy_usb_types;
	info->psy_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_desc1.properties = charger_psy_properties;
	info->psy_desc1.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc1.get_property = psy_charger_get_property;
	info->psy_desc1.set_property = psy_charger_set_property;
	info->psy_desc1.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_desc1.external_power_changed =
		mtk_charger_external_power_changed;
	info->psy_cfg1.drv_data = info;
	info->psy_cfg1.supplied_to = mtk_charger_supplied_to;
	info->psy_cfg1.num_supplicants = ARRAY_SIZE(mtk_charger_supplied_to);
	info->psy1 = power_supply_register(&pdev->dev, &info->psy_desc1,
			&info->psy_cfg1);

	info->chg_psy = power_supply_get_by_name("primary_chg");
	if (IS_ERR_OR_NULL(info->chg_psy))
		chr_err("%s: devm power fail to get chg_psy\n", __func__);

	info->bc12_psy = power_supply_get_by_name("primary_chg");
	if (IS_ERR_OR_NULL(info->bc12_psy))
		chr_err("%s: devm power fail to get bc12_psy\n", __func__);

	info->bat_psy = devm_power_supply_get_by_phandle(&pdev->dev,
		"gauge");
	if (IS_ERR_OR_NULL(info->bat_psy))
		chr_err("%s: devm power fail to get bat_psy\n", __func__);

	if (IS_ERR(info->psy1))
		chr_err("register psy1 fail:%ld\n",
			PTR_ERR(info->psy1));

	info->psy_desc2.name = "mtk-slave-charger";
	info->psy_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc2.usb_types = charger_psy_usb_types;
	info->psy_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_desc2.properties = charger_psy_properties;
	info->psy_desc2.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc2.get_property = psy_charger_get_property;
	info->psy_desc2.set_property = psy_charger_set_property;
	info->psy_desc2.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_cfg2.drv_data = info;
	info->psy2 = power_supply_register(&pdev->dev, &info->psy_desc2,
			&info->psy_cfg2);

	if (IS_ERR(info->psy2))
		chr_err("register psy2 fail:%ld\n",
			PTR_ERR(info->psy2));

	info->psy_dvchg_desc1.name = "mtk-mst-div-chg";
	info->psy_dvchg_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc1.usb_types = charger_psy_usb_types;
	info->psy_dvchg_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_dvchg_desc1.properties = charger_psy_properties;
	info->psy_dvchg_desc1.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc1.get_property = psy_charger_get_property;
	info->psy_dvchg_desc1.set_property = psy_charger_set_property;
	info->psy_dvchg_desc1.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg1.drv_data = info;
	info->psy_dvchg1 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc1,
						 &info->psy_dvchg_cfg1);
	if (IS_ERR(info->psy_dvchg1))
		chr_err("register psy dvchg1 fail:%ld\n",
			PTR_ERR(info->psy_dvchg1));

	info->psy_dvchg_desc2.name = "mtk-slv-div-chg";
	info->psy_dvchg_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_dvchg_desc2.usb_types = charger_psy_usb_types;
	info->psy_dvchg_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_dvchg_desc2.properties = charger_psy_properties;
	info->psy_dvchg_desc2.num_properties =
		ARRAY_SIZE(charger_psy_properties);
	info->psy_dvchg_desc2.get_property = psy_charger_get_property;
	info->psy_dvchg_desc2.set_property = psy_charger_set_property;
	info->psy_dvchg_desc2.property_is_writeable =
		psy_charger_property_is_writeable;
	info->psy_dvchg_cfg2.drv_data = info;
	info->psy_dvchg2 = power_supply_register(&pdev->dev,
						 &info->psy_dvchg_desc2,
						 &info->psy_dvchg_cfg2);
	if (IS_ERR(info->psy_dvchg2))
		chr_err("register psy dvchg2 fail:%ld\n",
			PTR_ERR(info->psy_dvchg2));

	info->psy_hvdvchg_desc1.name = "mtk-mst-hvdiv-chg";
	info->psy_hvdvchg_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_hvdvchg_desc1.usb_types = charger_psy_usb_types;
	info->psy_hvdvchg_desc1.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_hvdvchg_desc1.properties = charger_psy_properties;
	info->psy_hvdvchg_desc1.num_properties =
					     ARRAY_SIZE(charger_psy_properties);
	info->psy_hvdvchg_desc1.get_property = psy_charger_get_property;
	info->psy_hvdvchg_desc1.set_property = psy_charger_set_property;
	info->psy_hvdvchg_desc1.property_is_writeable =
					      psy_charger_property_is_writeable;
	info->psy_hvdvchg_cfg1.drv_data = info;
	info->psy_hvdvchg1 = power_supply_register(&pdev->dev,
						   &info->psy_hvdvchg_desc1,
						   &info->psy_hvdvchg_cfg1);
	if (IS_ERR(info->psy_hvdvchg1))
		chr_err("register psy hvdvchg1 fail:%ld\n",
					PTR_ERR(info->psy_hvdvchg1));

	info->psy_hvdvchg_desc2.name = "mtk-slv-hvdiv-chg";
	info->psy_hvdvchg_desc2.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_hvdvchg_desc2.usb_types = charger_psy_usb_types;
	info->psy_hvdvchg_desc2.num_usb_types = ARRAY_SIZE(charger_psy_usb_types);
	info->psy_hvdvchg_desc2.properties = charger_psy_properties;
	info->psy_hvdvchg_desc2.num_properties =
					     ARRAY_SIZE(charger_psy_properties);
	info->psy_hvdvchg_desc2.get_property = psy_charger_get_property;
	info->psy_hvdvchg_desc2.set_property = psy_charger_set_property;
	info->psy_hvdvchg_desc2.property_is_writeable =
					      psy_charger_property_is_writeable;
	info->psy_hvdvchg_cfg2.drv_data = info;
	info->psy_hvdvchg2 = power_supply_register(&pdev->dev,
						   &info->psy_hvdvchg_desc2,
						   &info->psy_hvdvchg_cfg2);
	if (IS_ERR(info->psy_hvdvchg2))
		chr_err("register psy hvdvchg2 fail:%ld\n",
					PTR_ERR(info->psy_hvdvchg2));

	info->log_level = CHRLOG_ERROR_LEVEL;

	info->pd_adapter = get_adapter_by_name("pd_adapter");
	if (!info->pd_adapter)
		chr_err("%s: No pd adapter found\n", __func__);
	else {
		info->pd_nb.notifier_call = notify_adapter_event;
		register_adapter_device_notifier(info->pd_adapter,
						 &info->pd_nb);
	}

	sc_init(&info->sc);
	info->chg_alg_nb.notifier_call = chg_alg_event;

	//ExtB EKCANCUN-11, yangpingao.wt, 2023/02/01, add fast charging indicator
	//info->fast_charging_indicator = 0x16;
	info->enable_meta_current_limit = 1;
	info->is_charging = false;
	info->safety_timer_cmd = -1;
	info->cmd_pp = -1;

	//+PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node
	ret = wt_init_dev_class(info, &pdev->dev);
	if (ret < 0) {
		chr_err("failed to init mtk-battery power supply\n");
	}
	//-PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node

	//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
	#ifdef WT_COMPILE_FACTORY_VERSION
	INIT_DELAYED_WORK(&info->ato_soc_user_control_work, wtchg_ato_soc_user_control_work);
	info->wtchg_ato_soc_wake_source.source = wakeup_source_register(NULL, "wtchg_ato_user_wake");
	info->wtchg_ato_soc_wake_source.disabled= 1;
	#endif
	//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

	info->sw_jeita.sm = -1;	//+PERIDOT-6523,xiaohongyu,wt,20240509,add jeita control

	/* 8 = KERNEL_POWER_OFF_CHARGING_BOOT */
	/* 9 = LOW_POWER_OFF_CHARGING_BOOT */
	if (info != NULL && info->bootmode != 8 && info->bootmode != 9)
		mtk_charger_force_disable_power_path(info, CHG1_SETTING, true);
	read_sys_protection_setting();
	kthread_run(charger_routine_thread, info, "charger_thread");

	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
	#ifdef WT_COMPILE_FACTORY_VERSION
	struct mtk_charger *info = platform_get_drvdata(dev);

	wakeup_source_unregister(info->wtchg_ato_soc_wake_source.source);
	#endif

	return 0;
}

static void mtk_charger_shutdown(struct platform_device *dev)
{
	struct mtk_charger *info = platform_get_drvdata(dev);
	int i;

	//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
	if (get_charger_real_type(info) == POWER_SUPPLY_BC12_TYPE_QC20) {
		charger_dev_reset_qc20_ta(info->chg1_dev);
	}
	//-PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (info->alg[i] == NULL)
			continue;
		chg_alg_stop_algo(info->alg[i]);
	}

	//+RERIDOT-11704, liyiying.wt, mod, 20240624, delay enter shipmode
	if (info->shipping_mode_user_control == 1) {
		charger_dev_set_shipping_mode(info->chg1_dev, 5526789);
	}
	//+RERIDOT-11704, liyiying.wt, mod, 20240624, delay enter shipmode
}

static const struct of_device_id mtk_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_charger_of_match);

struct platform_device mtk_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver mtk_charger_driver = {
	.probe = mtk_charger_probe,
	.remove = mtk_charger_remove,
	.shutdown = mtk_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = mtk_charger_of_match,
	},
};

static int __init mtk_charger_init(void)
{
	return platform_driver_register(&mtk_charger_driver);
}
module_init(mtk_charger_init);

static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&mtk_charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_AUTHOR("wy.chuang <wy.chuang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
