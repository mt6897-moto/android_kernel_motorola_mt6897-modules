/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTK_CHARGER_H
#define __MTK_CHARGER_H

#include <linux/alarmtimer.h>
#include "charger_class.h"
#include "adapter_class.h"
#include "mtk_charger_algorithm_class.h"
#include <linux/power_supply.h>
#include "mtk_smartcharging.h"

#define CHARGING_INTERVAL 10
#define CHARGING_FULL_INTERVAL 20

#define CHRLOG_ERROR_LEVEL	1
#define CHRLOG_INFO_LEVEL	2
#define CHRLOG_DEBUG_LEVEL	3

#define SC_TAG "smartcharging"

extern int chr_get_debug_level(void);

#define chr_err(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_ERROR_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

#define chr_info(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_INFO_LEVEL) {	\
		pr_notice_ratelimited(fmt, ##args);		\
	}							\
} while (0)

#define chr_debug(fmt, args...)					\
do {								\
	if (chr_get_debug_level() >= CHRLOG_DEBUG_LEVEL) {	\
		pr_notice(fmt, ##args);				\
	}							\
} while (0)

struct mtk_charger;
struct charger_data;
#define BATTERY_CV 4350000
#define V_CHARGER_MAX 6500000 /* 6.5 V */
#define V_CHARGER_MIN 4600000 /* 4.6 V */
#define VBUS_OVP_VOLTAGE 15000000 /* 15V */

#define USB_CHARGER_CURRENT_SUSPEND		0 /* def CONFIG_USB_IF */
#define USB_CHARGER_CURRENT_UNCONFIGURED	70000 /* 70mA */
#define USB_CHARGER_CURRENT_CONFIGURED		500000 /* 500mA */
#define USB_CHARGER_CURRENT			500000 /* 500mA */
#define AC_CHARGER_CURRENT			2050000
#define AC_CHARGER_INPUT_CURRENT		3200000
#define NON_STD_AC_CHARGER_CURRENT		500000
#define CHARGING_HOST_CHARGER_CURRENT		650000

//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
#define APPLE_5W_CHARGER_CURRENT	1000000
#define APPLE_10W_CHARGER_CURRENT	2000000
#define APPLE_12W_CHARGER_CURRENT	2400000
#define SAMSUNG_CHARGER_CURRENT		2000000
#define QC20_CHARGER_CURRENT		3000000
#define QC20_CHARGER_INPUT_CURRENT	2000000
//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

/* dynamic mivr */
#define V_CHARGER_MIN_1 4400000 /* 4.4 V */
#define V_CHARGER_MIN_2 4200000 /* 4.2 V */
#define MAX_DMIVR_CHARGER_CURRENT 1800000 /* 1.8 A */

/* battery warning */
#define BATTERY_NOTIFY_CASE_0001_VCHARGER
#define BATTERY_NOTIFY_CASE_0002_VBATTEMP

/* charging abnormal status */
#define CHG_VBUS_OV_STATUS	(1 << 0)
#define CHG_BAT_OT_STATUS	(1 << 1)
#define CHG_OC_STATUS		(1 << 2)
#define CHG_BAT_OV_STATUS	(1 << 3)
#define CHG_ST_TMO_STATUS	(1 << 4)
#define CHG_BAT_LT_STATUS	(1 << 5)
#define CHG_TYPEC_WD_STATUS	(1 << 6)
#define CHG_DPDM_OV_STATUS	(1 << 7)

/* Battery Temperature Protection */
#define MIN_CHARGE_TEMP  0
#define MIN_CHARGE_TEMP_PLUS_X_DEGREE	6
#define MAX_CHARGE_TEMP  50
#define MAX_CHARGE_TEMP_MINUS_X_DEGREE	47

#define MAX_ALG_NO 10

#define RESET_BOOT_VOLT_TIME 50

#define HARD_RESET_TIMES	5

enum bat_temp_state_enum {
	BAT_TEMP_LOW = 0,
	BAT_TEMP_NORMAL,
	BAT_TEMP_HIGH
};

enum chg_dev_notifier_events {
	EVENT_FULL,
	EVENT_RECHARGE,
	EVENT_DISCHARGE,
};

struct battery_thermal_protection_data {
	int sm;
	bool enable_min_charge_temp;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
};

/* sw jeita */
#define JEITA_TEMP_ABOVE_T4_CV	4240000
#define JEITA_TEMP_T3_TO_T4_CV	4240000
#define JEITA_TEMP_T2_TO_T3_CV	4340000
#define JEITA_TEMP_T1_TO_T2_CV	4240000
#define JEITA_TEMP_T0_TO_T1_CV	4040000
#define JEITA_TEMP_BELOW_T0_CV	4040000
//+PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
#define JEITA_TEMP_ABOVE_T4_FCC	500000
#define JEITA_TEMP_T3_TO_T4_FCC	500000
#define JEITA_TEMP_T2_TO_T3_FCC	2000000
#define JEITA_TEMP_T1_TO_T2_FCC	2000000
#define JEITA_TEMP_T0_TO_T1_FCC	500000
#define JEITA_TEMP_BELOW_T0_FCC	500000

#define JEITA_TEMP_ABOVE_T4_TERM	250000
#define JEITA_TEMP_T3_TO_T4_TERM	250000
#define JEITA_TEMP_T2_TO_T3_TERM	250000
#define JEITA_TEMP_T1_TO_T2_TERM	250000
#define JEITA_TEMP_T0_TO_T1_TERM	250000
#define JEITA_TEMP_BELOW_T0_TERM	250000
#define CHARGE_BASIC_TERM 250000
//-PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
#define TEMP_T4_THRES  50
#define TEMP_T4_THRES_MINUS_X_DEGREE 47
#define TEMP_T3_THRES  45
#define TEMP_T3_THRES_MINUS_X_DEGREE 39
#define TEMP_T2_THRES  10
#define TEMP_T2_THRES_PLUS_X_DEGREE 16
#define TEMP_T1_THRES  0
#define TEMP_T1_THRES_PLUS_X_DEGREE 6
#define TEMP_T0_THRES  0
#define TEMP_T0_THRES_PLUS_X_DEGREE  0
#define TEMP_NEG_10_THRES 0

/*
 * Software JEITA
 * T0: -10 degree Celsius
 * T1: 0 degree Celsius
 * T2: 10 degree Celsius
 * T3: 45 degree Celsius
 * T4: 50 degree Celsius
 */
enum sw_jeita_state_enum {
	TEMP_BELOW_T0 = 0,
	TEMP_T0_TO_T1,
	TEMP_T1_TO_T2,
	TEMP_T2_TO_T3,
	TEMP_T3_TO_T4,
	TEMP_ABOVE_T4
};

struct sw_jeita_data {
	int sm;
	int pre_sm;
	int cv;
	int fcc;	//+PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
	int term;
	bool charging;
	bool error_recovery_flag;
};

struct mtk_charger_algorithm {

	int (*do_algorithm)(struct mtk_charger *info);
	int (*enable_charging)(struct mtk_charger *info, bool en);
	int (*do_event)(struct notifier_block *nb, unsigned long ev, void *v);
	int (*do_dvchg1_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_dvchg2_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_hvdvchg1_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*do_hvdvchg2_event)(struct notifier_block *nb, unsigned long ev,
			       void *v);
	int (*change_current_setting)(struct mtk_charger *info);
	void *algo_data;
};

struct charger_custom_data {
	int battery_cv;	/* uv */
	int max_charger_voltage;
	int max_charger_voltage_setting;
	int min_charger_voltage;
	int vbus_sw_ovp_voltage;

	int usb_charger_current;
	int ac_charger_current;
	int ac_charger_input_current;
	int charging_host_charger_current;

	//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
	int apple_5w_charger_current;
	int apple_10w_charger_current;
	int apple_12w_charger_current;
	int samsung_charger_current;
	int qc20_charger_current;
	int qc20_charger_input_current;
	//-PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

	/* sw jeita */
	int jeita_temp_above_t4_cv;
	int jeita_temp_t3_to_t4_cv;
	int jeita_temp_t2_to_t3_cv;
	int jeita_temp_t1_to_t2_cv;
	int jeita_temp_t0_to_t1_cv;
	int jeita_temp_below_t0_cv;

	//+PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic
	int jeita_temp_above_t4_fcc;
	int jeita_temp_t3_to_t4_fcc;
	int jeita_temp_t2_to_t3_fcc;
	int jeita_temp_t1_to_t2_fcc;
	int jeita_temp_t0_to_t1_fcc;
	int jeita_temp_below_t0_fcc;

	int jeita_temp_above_t4_term;
	int jeita_temp_t3_to_t4_term;
	int jeita_temp_t2_to_t3_term;
	int jeita_temp_t1_to_t2_term;
	int jeita_temp_t0_to_t1_term;
	int jeita_temp_below_t0_term;
	//-PERIDOT-35, yangpingao.wt, 20240314, mod, adjust battery jeita logic

	//+Peridot-198, yangpingao.wt, 20240329, mod, adjust battery maintenance logic
	int maintenance_v10_level0_cycle;
	int maintenance_v10_level1_cycle;
	int maintenance_v10_level2_cycle;
	int maintenance_v10_level3_cycle;

	int maintenance_v10_level0_cv;
	int maintenance_v10_level1_cv;
	int maintenance_v10_level2_cv;
	int maintenance_v10_level3_cv;

	int maintenance_v10_level0_fg_cv;
	int maintenance_v10_level1_fg_cv;
	int maintenance_v10_level2_fg_cv;
	int maintenance_v10_level3_fg_cv;

	int maintenance_v20_level0_cv;
	int maintenance_v20_level0_fg_cv;
	//-Peridot-198, yangpingao.wt, 20240329, mod, adjust battery maintenance logic

	int temp_t4_thres;
	int temp_t4_thres_minus_x_degree;
	int temp_t3_thres;
	int temp_t3_thres_minus_x_degree;
	int temp_t2_thres;
	int temp_t2_thres_plus_x_degree;
	int temp_t1_thres;
	int temp_t1_thres_plus_x_degree;
	int temp_t0_thres;
	int temp_t0_thres_plus_x_degree;
	int temp_neg_10_thres;

	/* battery temperature protection */
	int mtk_temperature_recharge_support;
	int max_charge_temp;
	int max_charge_temp_minus_x_degree;
	int min_charge_temp;
	int min_charge_temp_plus_x_degree;

	/* dynamic mivr */
	int min_charger_voltage_1;
	int min_charger_voltage_2;
	int max_dmivr_charger_current;

};

struct charger_data {
	int input_current_limit;
	int charging_current_limit;

	int force_charging_current;
	int thermal_input_current_limit;
	int thermal_charging_current_limit;
	bool thermal_throttle_record;
	int disable_charging_count;
	int input_current_limit_by_aicl;
	int junction_temp_min;
	int junction_temp_max;
};

enum chg_data_idx_enum {
	CHG1_SETTING,
	CHG2_SETTING,
	DVCHG1_SETTING,
	DVCHG2_SETTING,
	HVDVCHG1_SETTING,
	HVDVCHG2_SETTING,
	CHGS_SETTING_MAX,
};

//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
struct wtchg_wakeup_source {
	struct wakeup_source *source;
	unsigned long		disabled;
};
//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic

struct mtk_charger {
	struct platform_device *pdev;
	struct charger_device *chg1_dev;
	struct notifier_block chg1_nb;
	struct charger_device *chg2_dev;
	struct charger_device *dvchg1_dev;
	struct notifier_block dvchg1_nb;
	struct charger_device *dvchg2_dev;
	struct notifier_block dvchg2_nb;
	struct charger_device *hvdvchg1_dev;
	struct notifier_block hvdvchg1_nb;
	struct charger_device *hvdvchg2_dev;
	struct notifier_block hvdvchg2_nb;
	struct charger_device *bkbstchg_dev;
	struct notifier_block bkbstchg_nb;

	struct charger_data chg_data[CHGS_SETTING_MAX];
	struct chg_limit_setting setting;
	enum charger_configuration config;

	struct power_supply_desc psy_desc1;
	struct power_supply_config psy_cfg1;
	struct power_supply *psy1;

	struct power_supply_desc psy_desc2;
	struct power_supply_config psy_cfg2;
	struct power_supply *psy2;

	struct power_supply_desc psy_dvchg_desc1;
	struct power_supply_config psy_dvchg_cfg1;
	struct power_supply *psy_dvchg1;

	struct power_supply_desc psy_dvchg_desc2;
	struct power_supply_config psy_dvchg_cfg2;
	struct power_supply *psy_dvchg2;

	struct power_supply_desc psy_hvdvchg_desc1;
	struct power_supply_config psy_hvdvchg_cfg1;
	struct power_supply *psy_hvdvchg1;

	struct power_supply_desc psy_hvdvchg_desc2;
	struct power_supply_config psy_hvdvchg_cfg2;
	struct power_supply *psy_hvdvchg2;

	struct power_supply  *chg_psy;
	struct power_supply  *bc12_psy;
	struct power_supply  *bat_psy;
	struct adapter_device *pd_adapter;
	struct notifier_block pd_nb;
	struct mutex pd_lock;
	int pd_type;
	bool pd_reset;

	u32 bootmode;
	u32 boottype;

	int chr_type;
	int usb_type;
	int usb_state;

	struct mutex cable_out_lock;
	int cable_out_cnt;

	/* system lock */
	spinlock_t slock;
	struct wakeup_source *charger_wakelock;
	struct mutex charger_lock;

	/* thread related */
	wait_queue_head_t  wait_que;
	bool charger_thread_timeout;
	unsigned int polling_interval;
	bool charger_thread_polling;

	/* alarm timer */
	struct alarm charger_timer;
	struct timespec64 endtime;
	bool is_suspend;
	struct notifier_block pm_notifier;

	/* notify charger user */
	struct srcu_notifier_head evt_nh;

	/* common info */
	int log_level;
	bool usb_unlimited;
	bool charger_unlimited;
	bool disable_charger;
	bool disable_aicl;
	int battery_temp;
	bool can_charging;
	bool cmd_discharging;
	bool safety_timeout;
	int safety_timer_cmd;
	bool vbusov_stat;
	bool dpdmov_stat;
	bool lst_dpdmov_stat;
	bool is_chg_done;
	/* ATM */
	bool atm_enabled;

	const char *algorithm_name;
	struct mtk_charger_algorithm algo;

	/* dtsi custom data */
	struct charger_custom_data data;

	/* battery warning */
	unsigned int notify_code;
	unsigned int notify_test_mode;

	/* sw safety timer */
	bool enable_sw_safety_timer;
	bool sw_safety_timer_setting;
	struct timespec64 charging_begin_time;

	/* vbat monitor, 6pin bat */
	bool batpro_done;
	bool enable_vbat_mon;
	bool enable_vbat_mon_bak;
	int old_cv;
	bool stop_6pin_re_en;
	int vbat0_flag;

	/* sw jeita */
	bool enable_sw_jeita;
	struct sw_jeita_data sw_jeita;

	/* battery thermal protection */
	struct battery_thermal_protection_data thermal;

	struct chg_alg_device *alg[MAX_ALG_NO];
	int lst_rnd_alg_idx;
	bool alg_new_arbitration;
	bool alg_unchangeable;
	struct notifier_block chg_alg_nb;
	bool enable_hv_charging;

	/* water detection */
	bool water_detected;
	bool record_water_detected;

	int cc_hi;

	bool enable_dynamic_mivr;

	/* fast charging algo support indicator */
	bool enable_fast_charging_indicator;
	unsigned int fast_charging_indicator;

	/* diasable meta current limit for testing */
	unsigned int enable_meta_current_limit;

	struct smartcharging sc;

	/*daemon related*/
	struct sock *daemo_nl_sk;
	u_int g_scd_pid;
	struct scd_cmd_param_t_1 sc_data;

	/*charger IC charging status*/
	bool is_charging;

	ktime_t uevent_time_check;

	bool force_disable_pp[CHG2_SETTING + 1];
	bool enable_pp[CHG2_SETTING + 1];
	struct mutex pp_lock[CHG2_SETTING + 1];
	int cmd_pp;

	/* enable boot volt*/
	bool enable_boot_volt;
	bool reset_boot_volt_times;

	//+PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func
	int vbus_mos_gpio;
	//-PERIDOT-35, 20240220, liyiying.wt, add, add usb_mos otp func

	//+PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node
	int typec_cc_orientation;
	struct class battery_class;
	struct device batt_device;
	//-PERIDOT-422, liyiying.wt, 20240224, add, add sys/class/wt-battery/typec_cc_orientation node

	//+PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic
	int ato_soc_user_control;
	struct delayed_work ato_soc_user_control_work;
	struct wtchg_wakeup_source wtchg_ato_soc_wake_source;
	int wt_discharging_state;
	//-PERIDOT-422, liyiying.wt, 20240226, add, add ATO version battery  management (soc) control logic


	//+PERIDOT-35, liyiying.wt, 20240306, add, add stoping_test node and startcharging_test node for mmi test
	int start_charging_test;
	//-PERIDOT-35, liyiying.wt, 20240306, add, add stoping_test node and startcharging_test node for mmi test

	bool user_set_charge_current_flag;
	int user_set_charge_current;
	bool user_set_batt_temp_flag;
	int user_set_batt_temp;
	int charge_baisc_term;
	int pe5_is_chg_done;
	int bc12_real_type;

	//+PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)
	bool is_fast_charging;
	bool pr_is_fast_charging;
	//-PERIDOT-200, liyiying.wt, mod, 20240313, add fast charing node(0:normal 1:PD_PPS 2:FLOAT)
	//+PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
	int input_suspend_user_control;
	int charging_enabled_user_control;
	//-PERIDOT-196, liyiying.wt, mod, 20240313, battery and charging - charging protection
	//+PERIDOT-203, liyiying.wt, mod, 20240313, add aidl shipmode interface
	int shipping_mode_user_control;
	//-PERIDOT-203, liyiying.wt, mod, 20240313, add aidl shipmode interface

	//+PERIDOT-198, liyiying.wt, mod, 20240316, battery and charging - battery maintenance
	int battery_maintenance_v10_user_control;
	int battery_cycle_save;
	//-PERIDOT-198, liyiying.wt, mod, 20240316, battery and charging - battery maintenance
	//+PERIDOT-204, liyiying.wt, mod, 20240316, battery and charging - battery maintenance 2.0
	int battery_maintenance_v20_user_control;
	int battery_cycle_user_control;
	int maintenance_fg_batt_cv_pre;
	//-PERIDOT-204, liyiying.wt, mod, 20240316, battery and charging - battery maintenance 2.0

	//+PERIDOT-35, liyiying.wt, add, 20240420, add real_type node
	int real_type;
	int pd_online;
	int qc20_online;
	int pe2_online;
	int pe5_online;
	//-PERIDOT-35, liyiying.wt, add, 20240420, add real_type node

	//+PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time
	bool pre_charging;
	int safe_charging_count;
	//-PERIDOT-2665, yangpingao.wt, 20240507, mod, add software charging safe time

	//+Peridot-1559, liyiying.wt, 20240522, mod, when charging with a 30W charger charging process is intermittent
	int pd_hardreset_times;
	bool pe50_auth_ta_fail;
	//-Peridot-1559, liyiying.wt, 20240522, mod, when charging with a 30W charger charging process is intermittent

	//+Peridot-7898, liyiying.wt, 20240525, mod, optimize ttl node display to support chg_type is usb with pd capacilities
	int sink_vbus_mV;
	int sink_vbus_mA;
	//-Peridot-7898, liyiying.wt, 20240525, mod, optimize ttl node display to support chg_type is usb with pd capacilities
	//+Peridot-9631,xiaohongyu,wt, 20241023, mod, add input current different to wake up charger
	int sink_vbus_mV_old;
	int sink_vbus_mA_old;
	//-Peridot-9631,xiaohongyu,wt, 20241023, mod, add input current different to wake up charger

	int en_charging;

	int batt_protection_setting_eu;
	int batt_protection_a;
	int batt_protection_bb;
	int batt_protection_ccc;
	bool running_batt_protection;

	int batt_recharge_setting;
	int batt_recharge_running;
	int force_is_chg_done;
	int force_pe5_is_chg_done;
};

static inline int mtk_chg_alg_notify_call(struct mtk_charger *info,
					  enum chg_alg_notifier_events evt,
					  int value)
{
	int i;
	struct chg_alg_notify notify = {
		.evt = evt,
		.value = value,
	};

	for (i = 0; i < MAX_ALG_NO; i++) {
		if (info->alg[i])
			chg_alg_notifier_call(info->alg[i], &notify);
	}
	return 0;
}

/* functions which framework needs*/
extern int mtk_basic_charger_init(struct mtk_charger *info);
extern int mtk_pulse_charger_init(struct mtk_charger *info);
extern int get_uisoc(struct mtk_charger *info);
extern int get_battery_voltage(struct mtk_charger *info);
extern int get_battery_temperature(struct mtk_charger *info);
extern int get_battery_current(struct mtk_charger *info);
extern int get_vbus(struct mtk_charger *info);
extern int get_ibat(struct mtk_charger *info);
extern int get_ibus(struct mtk_charger *info);
extern bool is_battery_exist(struct mtk_charger *info);
extern int get_charger_type(struct mtk_charger *info);
extern int get_usb_type(struct mtk_charger *info);
extern int disable_hw_ovp(struct mtk_charger *info, int en);
extern bool is_charger_exist(struct mtk_charger *info);
extern int get_charger_temperature(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_charging_current(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_input_current(struct mtk_charger *info,
	struct charger_device *chg);
extern int get_charger_zcv(struct mtk_charger *info,
	struct charger_device *chg);
extern void _wake_up_charger(struct mtk_charger *info);

/* functions for other */
extern int mtk_chg_enable_vbus_ovp(bool enable);

//+PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic
#define HVDCP_VBUS_VOLT_MIN	6500
extern int get_charger_real_type(struct mtk_charger *info);

enum bc12_real_type {
	POWER_SUPPLY_BC12_TYPE_UNKNOWN,
	POWER_SUPPLY_BC12_TYPE_SDP,
	POWER_SUPPLY_BC12_TYPE_CDP,
	POWER_SUPPLY_BC12_TYPE_DCP,
	POWER_SUPPLY_BC12_TYPE_APPLE_5W,
	POWER_SUPPLY_BC12_TYPE_APPLE_10W,
	POWER_SUPPLY_BC12_TYPE_APPLE_12W,
	POWER_SUPPLY_BC12_TYPE_SAMSUNG,
	POWER_SUPPLY_BC12_TYPE_QC20,
	POWER_SUPPLY_BC12_TYPE_QC30,
	POWER_SUPPLY_BC12_TYPE_UNKNOWN_TA,
};
//-PERIDOT-2894, yangpingao.wt, 20240423, mod, qc2.0 detect logic

enum attach_type {
	ATTACH_TYPE_NONE,
	ATTACH_TYPE_PWR_RDY,
	ATTACH_TYPE_TYPEC,
	ATTACH_TYPE_PD,
	ATTACH_TYPE_PD_SDP,
	ATTACH_TYPE_PD_DCP,
	ATTACH_TYPE_PD_NONSTD,
	ATTACH_TYPE_MAX,
};

#define ONLINE(idx, attach)		((idx & 0xf) << 4 | (attach & 0xf))
#define ONLINE_GET_IDX(online)		((online >> 4) & 0xf)
#define ONLINE_GET_ATTACH(online)	(online & 0xf)

#endif /* __MTK_CHARGER_H */
