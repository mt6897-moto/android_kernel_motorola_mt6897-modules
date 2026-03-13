// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Wingtech Inc.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#ifdef CONFIG_RT_REGMAP
#include <mt-plat/rt-regmap.h>
#endif /* CONFIG_RT_REGMAP */
#include "charger_class.h"
#include <linux/hardware_info.h>

static bool dbg_log_en;
module_param(dbg_log_en, bool, 0644);
#define nu_dbg(dev, fmt, ...) \
	do { \
		if (dbg_log_en) \
			dev_info(dev, "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

/* Information */
#define NU2115_DRV_VERSION	"1.0.0_WT"
#define NU2115_DEVID		0x90


/* Registers */

#define NU2115_REG_BAT_OVP 0x00

#define NU2115_REG_BAT_OVP_ALM 0x01

#define NU2115_REG_BAT_OCP 0x02

#define NU2115_REG_BAT_OCP_ALM 0x03

#define NU2115_REG_BAT_UCP_ALM 0x04

#define NU2115_REG_AC1_PROTECTION 0x05

#define NU2115_REG_AC2_PROTECTION 0x06

#define NU2115_REG_BUS_OVP 0x07

#define NU2115_REG_BUS_OVP_ALM 0x08

#define NU2115_REG_BUS_OCP_UCP 0x09

#define NU2115_REG_BUS_OCP_ALM 0x0A

#define NU2115_REG_VOUT_OVP 0x0B

#define NU2115_REG_CONVERTER_STATE 0x0C

#define NU2115_REG_CONTROL 0x0D

#define NU2115_REG_CHG_CTRL 0x0E

#define NU2115_REG_INT_STAT 0x0F

#define NU2115_REG_INT_FLAG 0x10

#define NU2115_REG_INT_MASK 0x11

#define NU2115_REG_FLT_STAT 0x12

#define NU2115_REG_FLT_FLAG 0x13

#define NU2115_REG_FLT_MASK 0x14

#define NU2115_REG_ADC_CTRL 0x15

#define NU2115_REG_ADC_FN_DIS 0x16

#define NU2115_REG_IBUS_ADC1 0x17

#define NU2115_REG_IBUS_ADC0 0x18

#define NU2115_REG_VBUS_ADC1 0x19

#define NU2115_REG_VBUS_ADC0 0x1A

#define NU2115_REG_VAC1_ADC1 0x1B

#define NU2115_REG_VAC1_ADC0 0x1C

#define NU2115_REG_VAC2_ADC1 0x1D

#define NU2115_REG_VAC2_ADC0 0x1E

#define NU2115_REG_VOUT_ADC1 0x1F

#define NU2115_REG_VOUT_ADC0 0x20

#define NU2115_REG_VBAT_ADC1 0x21

#define NU2115_REG_VBAT_ADC0 0x22

#define NU2115_REG_IBAT_ADC1 0x23

#define NU2115_REG_IBAT_ADC0 0x24

#define NU2115_REG_TSBUS_ADC1 0x25

#define NU2115_REG_TSBUS_ADC0 0x26

#define NU2115_REG_TSBAT_ADC1 0x27

#define NU2115_REG_TSBAT_ADC0 0x28

#define NU2115_REG_TDIE_ADC1 0x29

#define NU2115_REG_TDIE_ADC0 0x2A

#define NU2115_REG_TSBUS_FLT 0x2B

#define NU2115_REG_TSBAT_FLT 0x2C

#define NU2115_REG_TDIE_ALM 0x2D

#define NU2115_REG_IBUS_UCP_RCP_THRES 0x2E

#define NU2115_REG_VAC1_2_PRESENT_DET 0x2F

#define NU2115_REG_ACDRV1_2_CTRL 0x30

#define NU2115_REG_DEV_INFO 0x31

#define NU2115_REG_PMID2VOUT_UVP_OVP 0x32

#define NU2115_REG_DEGLITCH 0x33

#define NU2115_REG_CP_OPTION 0x34

#define NU2115_REG_CP_OPTION_1 0x35

#define NU2115_REG_CP_OPTION_2 0x36

#define NU2115_REG_MAX 0x36


/* Control bits */

/*0x00 BAT_OVP*/

#define NU2115_VBATOVP_MASK 0x7F


/*0x01 BAT_OVP_ALM*/

#define NU2115_VBATOVP_ALM_MASK 0x7F

#define NU2115_VBATOVP_ALMDIS_MASK BIT(7)


/*0x02 BAT_OCP*/

#define NU2115_IBATOCP_MASK 0x7F


/*0x05 AC1_PROTECTION*/

#define NU2115_VAC1_PD_EN_MASK BIT(4)

#define NU2115_VAC1_PD_EN_SHFT 4


/*0x06 AC2_PROTECTION*/

#define NU2115_VAC2_PD_EN_MASK BIT(4)

#define NU2115_VAC2_PD_EN_SHFT 4


/*0x07 BUS_OVP*/

#define NU2115_BUS_PD_EN_MASK BIT(7)

#define NU2115_BUS_PD_EN_SHFT 7

#define NU2115_VBUSOVP_MASK 0x7F


/*0x08 BUS_OVP_ALM*/

#define NU2115_VBUSOVP_ALM_MASK 0x3F

#define NU2115_VBUSOVP_ALMDIS_MASK BIT(7)


/*0x09 BUS_OCP_UCP*/

#define NU2115_IBUSOCP_MASK 0x0F


/*0x0C CONVERTER_STATE*/

#define NU2115_CONVACTIVE_MASK BIT(2)

#define NU2115_CONVACTIVE_SHFT 2


/*0x0D CONTROL*/

#define NU2115_WDTEN_MASK BIT(2)

#define NU2115_WDTMR_MASK 0x03

#define NU2115_RST_MASK BIT(7)


/*0x0E CHRG_CTRL*/

#define NU2115_CHGEN_MASK BIT(7)

#define NU2115_CHGEN_SHFT 7


/*0x15 ADC_CTRL*/

#define NU2115_ADCEN_MASK BIT(7)


/*0x2E IBUS_UCP_RCP_THRES*/

#define NU2115_REG_IBUS_UCP_RCP_THERES 0x2E

#define NU2115_MS_MASK 0x18

#define NU2115_MS_SHFT 3


/*0x2F VAC1/2_PRESENT_DET*/

#define NU2115_ACDRV2_STAT_MASK BIT(4)

#define NU2115_ACDRV2_STAT_SHIFT 4

#define NU2115_ACDRV1_STAT_MASK BIT(7)

#define NU2115_ACDRV1_STAT_SHIFT 7

#define NU2115_DIS_ADCRV_BOTH_MASK BIT(0)

#define NU2115_DIS_ADCRV_BOTH_SHIFT 0

#define NU2115_OTGEN_MASK BIT(1)

#define NU2115_OTGEN_SHFT 1


/*0x31 DEV_INFO*/

#define NU2115_REG_DEV_INFO 0x31

#define NU2115_DEVREV_MASK 0xF0

#define NU2115_DEVREV_SHFT 4

#define NU2115_DEVID_MASK 0xF0

/*0x32 PMID2VOUT_UVP_OVP*/

#define NU2115_PMID2VOUT_UVP_SHFT 6

#define NU2115_PMID2VOUT_OVP_SHFT 4

#define NU2115_PMID2VOUT_UVP_MASK 0xC0

#define NU2115_PMID2VOUT_OVP_MASK 0x30

/*0x35 CP_OPTION_1*/
#define NU2115_VBUSUCP_FLAG_SHFT 2


//#define NU2115_CHG_CONFIG_MASK BIT(3) //not found


#define NU2115_ADC_INVALID_BIT_7_MASK BIT(7)
#define NU2115_ADC_INVALID_BIT_5_6_7_MASK (BIT(5) | BIT(6) | BIT(7))
#define NU2115_ADC_INVALID_BIT_2_3_4_5_6_7_MASK (BIT(2) | BIT(3) | BIT(4)|BIT(5) | BIT(6) | BIT(7))

#define CHARGER_IC_SLAVE_NAME "NU2115"

enum nu2115_irqidx {
	NU2115_IRQIDX_VBATOVP = 0,
	NU2115_IRQIDX_VBATOVPALM,
	NU2115_IRQIDX_VOUTOVP,
	NU2115_IRQIDX_IBATOCP,
	NU2115_IRQIDX_IBATOCPALM,
	NU2115_IRQIDX_IBATUCPALM,
	NU2115_IRQIDX_VBUSOVP,
	NU2115_IRQIDX_VBUSOVPALM,

	NU2115_IRQIDX_IBUSOCP,
	NU2115_IRQIDX_IBUSOCPALM,
	NU2115_IRQIDX_IBUSUCPF,
	NU2115_IRQIDX_IBUSRCPF,

	NU2115_IRQIDX_VACOVP1,
	NU2115_IRQIDX_VACOVP2,
	NU2115_IRQIDX_VAC1INSERT,
	NU2115_IRQIDX_VAC2INSERT,
	NU2115_IRQIDX_VBUSPRESENT,
	NU2115_IRQIDX_ACRB1CONFIG,
	NU2115_IRQIDX_ACRB2CONFIG,

	NU2115_IRQIDX_ADCDONE,
	NU2115_IRQIDX_SSTIMEOUT,
	NU2115_IRQIDX_TSALM,
	NU2115_IRQIDX_TSFLT,
	NU2115_IRQIDX_TDIEALM,
	NU2115_IRQIDX_WDTIMEOUT,

	NU2115_IRQIDX_VBUSERRORHI,
	NU2115_IRQIDX_VBUSERRORLOW,

	NU2115_IRQIDX_PMID2VOUTOVP,
	NU2115_IRQIDX_PMID2VOUTUVP,

	NU2115_IRQIDX_MAX,
};

enum nu2115_notify {
	NU2115_NOTIFY_IBUSUCPF = 0,
	NU2115_NOTIFY_VBUSOVPALM,
	NU2115_NOTIFY_VBATOVPALM,
	NU2115_NOTIFY_IBUSOCP,
	NU2115_NOTIFY_VBUSOVP,
	NU2115_NOTIFY_IBATOCP,
	NU2115_NOTIFY_VBATOVP,
	NU2115_NOTIFY_VOUTOVP,
	NU2115_NOTIFY_VDROVP,
	NU2115_NOTIFY_MAX,
};

enum nu2115_statflag_idx {
	NU2115_SF_FLT_STAT = 0,
	NU2115_SF_FLT_FLAG,
	NU2115_SF_INT_STAT,
	NU2115_SF_INT_FLAG,
	NU2115_SF_VOUT_OVP_STAT_FLAG,
	NU2115_SF_BUS_UCP_STAT_FLAG,
	NU2115_SF_AC1_OVP_STAT_FLAG,
	NU2115_SF_AC2_OVP_STAT_FLAG,
	NU2115_SF_VAC1_2_PRESENT_STAT_FLAG,
	NU2115_SF_BUS_PRESENT_STAT_FLAG,
	NU2115_SF_ACDRV1_2_STAT_FLAG,
	NU2115_SF_SSTIMEOUT_FLAG,
	NU2115_SF_WDT_FLAG,
	NU2115_SF_VBUSERR_STAT,
	NU2115_SF_PMID2VOUTOVP_FLAG,
	NU2115_SF_MAX,
};

enum nu2115_type {
	NU2115_TYPE_STANDALONE = 0,
	NU2115_TYPE_SLAVE,
	NU2115_TYPE_MASTER,
	NU2115_TYPE_MAX,
};

static const char *nu2115_type_name[NU2115_TYPE_MAX] = {
	"standalone", "slave", "master",
};

static const u32 nu2115_chgdev_notify_map[NU2115_NOTIFY_MAX] = {
	CHARGER_DEV_NOTIFY_IBUSUCP_FALL,
	CHARGER_DEV_NOTIFY_VBUSOVP_ALARM,
	CHARGER_DEV_NOTIFY_VBATOVP_ALARM,
	CHARGER_DEV_NOTIFY_IBUSOCP,
	CHARGER_DEV_NOTIFY_VBUS_OVP,
	CHARGER_DEV_NOTIFY_IBATOCP,
	CHARGER_DEV_NOTIFY_BAT_OVP,
	CHARGER_DEV_NOTIFY_VOUTOVP,
	CHARGER_DEV_NOTIFY_VDROVP,
};

static const u8 nu2115_reg_sf[NU2115_SF_MAX] = {
	NU2115_REG_FLT_STAT,
	NU2115_REG_FLT_FLAG,
	NU2115_REG_INT_STAT,
	NU2115_REG_INT_FLAG,
	NU2115_REG_VOUT_OVP,
	NU2115_REG_BUS_OCP_UCP,
	NU2115_REG_AC1_PROTECTION,
	NU2115_REG_AC2_PROTECTION,
	NU2115_REG_VAC1_2_PRESENT_DET,
	NU2115_SF_BUS_PRESENT_STAT_FLAG,
	NU2115_REG_ACDRV1_2_CTRL,
	NU2115_REG_CONVERTER_STATE,
	NU2115_REG_CONTROL,
	NU2115_REG_CP_OPTION_1,
	NU2115_REG_PMID2VOUT_UVP_OVP,
};

struct nu2115_reg_defval {
	u8 reg;
	u8 value;
	u8 mask;
};

static const struct nu2115_reg_defval nu2115_init_chip_check_reg[] = {
	{
		.reg = NU2115_REG_BAT_OVP,
		.value = 0x2F,
		.mask = NU2115_VBATOVP_MASK,
	},
	{
		.reg = NU2115_REG_BAT_OCP,
		.value = 0x41,
		.mask = NU2115_IBATOCP_MASK,
	},
	{
		.reg = NU2115_REG_CONTROL,
		.value = 0x20,
		.mask = NU2115_WDTMR_MASK,
	},
};

struct nu2115_desc {
	const char *chg_name;
	const char *rm_name;
	u8 rm_slave_addr;
	u32 vbatovp;
	u32 vbatovp_alm;
	u32 ibatocp;
	u32 ibatocp_alm;
	u32 vbusovp;
	u32 vbusovp_alm;
	u32 ibusocp;
	u32 ibusocp_alm;
	u32 tdie_alm;
	u32 tsbus_flt;
	u32 tsbat_flt;
	u32 vac1ovp;
	u32 vac2ovp;
	u32 fsw_set;
	u32 wd_timeout;
	u32 ibat_rsense;
	u32 ss_timeout;
	u32 ibusucpf_deglitch;
	u32 vout_ovp;
	u32 pmid2out_uvp;
	u32 pmid2out_ovp;
	u32 vbus_valid_deg;
	u32 freq_shift;
	bool vbatovp_dis;
	bool vbatovp_alm_dis;
	bool ibatocp_dis;
	bool ibatocp_alm_dis;
	bool ibusucp_dis;
	bool vbus_errhi_dis;
	bool vbus_pd_en;
	bool vbusovp_alm_dis;
	bool ibusocp_dis;
	bool ibusocp_alm_dis;
	bool tshut_dis;
	bool tshut_alm_dis;
	bool tsbus_flt_dis;
	bool tsbat_flt_dis;
	bool vac1_pd_en;
	bool vac2_pd_en;
	bool wd_timeout_dis;
	bool voutovp_dis;
	bool ibusadc_dis;
	bool vbusadc_dis;
	bool vacadc1_dis;
	bool vacadc2_dis;
	bool voutadc_dis;
	bool vbatadc_dis;
	bool ibatadc_dis;
	bool tsbusadc_dis;
	bool tsbatadc_dis;
	bool tdieadc_dis;
};

static const struct nu2115_desc nu2115_desc_defval = {
	.chg_name = "divider_charger",
	.rm_name = "nu2115",
	.rm_slave_addr = 0x66,
	.vbatovp = 4400000,
	.vbatovp_alm = 4300000,
	.ibatocp = 8100000,
	.ibatocp_alm = 8000000,
	.vbusovp = 8900000,
	.vbusovp_alm = 8800000,
	.ibusocp = 4250000,
	.ibusocp_alm = 4000000,
	.tdie_alm = 1250, /* 125℃ */
	.tsbus_flt = 65000, /* 6.5% */
	.tsbat_flt = 65000, /* 6.5% */
	.vac1ovp = 12000000, /* 12V */
	.vac2ovp = 12000000, /* 12V */
	.fsw_set = 600, /* 600KhZ */
	.wd_timeout = 30000000, /* 30s */
	.ibat_rsense = 1, /* 0: 2mohm, 1: 5mohm */
	.ss_timeout = 10000000, /* 10s */
	.ibusucpf_deglitch = 5000, /* 5ms */
	.vout_ovp = 5000, /* 5000mV */
	.pmid2out_uvp = 5000, /* 5% */
	.pmid2out_ovp = 12500, /* 12.5% */
	.vbus_valid_deg = 1, /* 20ms */
	.freq_shift = 0,
	.vbatovp_dis = false,
	.vbatovp_alm_dis = false,
	.ibatocp_dis = false,
	.ibatocp_alm_dis = false,
	.ibusucp_dis = false,
	.vbus_errhi_dis = false,
	.vbus_pd_en = false,
	.vbusovp_alm_dis = false,
	.ibusocp_dis = false,
	//+PERIDOT-35, liyiying.wt, 20240305, mod, debug 45w requirement
	.ibusocp_alm_dis = false,
	//-PERIDOT-35, liyiying.wt, 20240305, mod, debug 45w requirement
	.tshut_dis = false,
	.tshut_alm_dis = false,
	.tsbus_flt_dis = false,
	.tsbat_flt_dis = false,
	.vac1_pd_en = false,
	.vac2_pd_en = false,
	.wd_timeout_dis = false,
	.voutovp_dis = false,
	.ibusadc_dis = false,
	.vbusadc_dis = false,
	.vacadc1_dis = false,
	.vacadc2_dis = false,
	.voutadc_dis = false,
	.vbatadc_dis = false,
	.ibatadc_dis = false,
	.tsbusadc_dis = false,
	.tsbatadc_dis = false,
	.tdieadc_dis = false,
};

struct nu2115_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex adc_lock;
	struct mutex stat_lock;
	struct mutex notify_lock;
	struct charger_device *chg_dev;
	struct charger_properties chg_prop;
	struct nu2115_desc *desc;
	struct gpio_desc *irq_gpio;
	struct task_struct *notify_task;
	int irq;
	int notify;
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum nu2115_type type;
	bool wdt_en;
	bool force_adc_en;
	bool stop_thread;
	wait_queue_head_t wq;

#ifdef CONFIG_RT_REGMAP
	struct rt_regmap_device *rm_dev;
	struct rt_regmap_properties *rm_prop;
#endif /* CONFIG_RT_REGMAP */
};

enum nu2115_adc_channel {
	NU2115_ADC_IBUS = 0,
	NU2115_ADC_VBUS,
	NU2115_ADC_VAC1,
	NU2115_ADC_VAC2,
	NU2115_ADC_VOUT,
	NU2115_ADC_VBAT,
	NU2115_ADC_IBAT,
	NU2115_ADC_TSBUS,
	NU2115_ADC_TSBAT,
	NU2115_ADC_TDIE,
	NU2115_ADC_MAX,
	NU2115_ADC_NOTSUPP = NU2115_ADC_MAX,
};

static const u8 nu2115_adc_reg[NU2115_ADC_MAX] = {
	NU2115_REG_IBUS_ADC1,
	NU2115_REG_VBUS_ADC1,
	NU2115_REG_VAC1_ADC1,
	NU2115_REG_VAC2_ADC1,
	NU2115_REG_VOUT_ADC1,
	NU2115_REG_VBAT_ADC1,
	NU2115_REG_IBAT_ADC1,
	NU2115_REG_TSBUS_ADC1,
	NU2115_REG_TSBAT_ADC1,
	NU2115_REG_TDIE_ADC1,
};

static const char *nu2115_adc_name[NU2115_ADC_MAX] = {
	"Ibus", "Vbus", "VAC1", "VAC2", "Vout", "Vbat", "Ibat", "TSBus", "TSBat", "TDie",
};

static const u32 nu2115_adc_accuracy_tbl[NU2115_ADC_MAX] = {//follow UPM6720, no change
	250000,	/* IBUS */
	35000,	/* VBUS*/
	35000,	/* VAC1*/
	35000,	/* VAC2*/
	20000,	/* VOUT */
	20000,	/* VBAT */
	200000,	/* IBAT*/
	0,	/* TSBUS */
	0,	/* TSBAT */
	4,	/* TDIE */
};

static int nu2115_read_device(void *client, u32 addr, int len, void *dst)
{
	struct i2c_client *i2c = (struct i2c_client *)client;

	return i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
}

static int nu2115_write_device(void *client, u32 addr, int len, const void *src)
{
	struct i2c_client *i2c = (struct i2c_client *)client;

	return i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
}

#ifdef CONFIG_RT_REGMAP
RT_REG_DECL(NU2115_REG_BAT_OVP, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BAT_OVP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BAT_OCP, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BAT_OCP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BAT_UCP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_AC1_PROTECTION, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_AC2_PROTECTION, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BUS_OVP, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BUS_OVP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_BUS_OCP_UCP, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_BUS_OCP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VOUT_OVP, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBUS_FLT, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_CONVERTER_STATE, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_CONTROL, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_CHG_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_INT_STAT, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_INT_FLAG, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_INT_MASK, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_FLT_STAT, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_FLT_FLAG, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_FLT_MASK, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_ADC_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_ADC_FN_DIS, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_IBUS_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_IBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VAC1_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VAC1_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VAC2_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VAC2_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VOUT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VOUT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VBAT_ADC1, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_VBAT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_IBAT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_IBAT_ADC0, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_TSBUS_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBAT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBAT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TDIE_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TDIE_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBUS_FLT, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_TSBAT_FLT, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_TDIE_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_IBUS_UCP_RCP_THRES, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_VAC1_2_PRESENT_DET, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_ACDRV1_2_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_DEV_INFO, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_PMID2VOUT_UVP_OVP, 1, RT_VOLATILE, {});

RT_REG_DECL(NU2115_REG_DEGLITCH, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_CP_OPTION, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_CP_OPTION_1, 1, RT_VOLATILE, {});
RT_REG_DECL(NU2115_REG_CP_OPTION_2, 1, RT_VOLATILE, {});


static const rt_register_map_t nu2115_regmap[] = {
	RT_REG(NU2115_REG_BAT_OVP),
	RT_REG(NU2115_REG_BAT_OVP_ALM),
	RT_REG(NU2115_REG_BAT_OCP),
	RT_REG(NU2115_REG_BAT_OCP_ALM),
	RT_REG(NU2115_REG_BAT_UCP_ALM),
	RT_REG(NU2115_REG_AC1_PROTECTION),
	RT_REG(NU2115_REG_AC2_PROTECTION),
	RT_REG(NU2115_REG_BUS_OVP),
	RT_REG(NU2115_REG_BUS_OVP_ALM),
	RT_REG(NU2115_REG_BUS_OCP_UCP),
	RT_REG(NU2115_REG_BUS_OCP_ALM),
	RT_REG(NU2115_REG_VOUT_OVP),
	RT_REG(NU2115_REG_CONVERTER_STATE),
	RT_REG(NU2115_REG_CONTROL),
	RT_REG(NU2115_REG_CHG_CTRL),
	RT_REG(NU2115_REG_INT_STAT),
	RT_REG(NU2115_REG_INT_FLAG),
	RT_REG(NU2115_REG_INT_MASK),
	RT_REG(NU2115_REG_FLT_STAT),
	RT_REG(NU2115_REG_FLT_FLAG),
	RT_REG(NU2115_REG_FLT_MASK),
	RT_REG(NU2115_REG_ADC_CTRL),
	RT_REG(NU2115_REG_ADC_FN_DIS),
	RT_REG(NU2115_REG_IBUS_ADC1),
	RT_REG(NU2115_REG_IBUS_ADC0),
	RT_REG(NU2115_REG_VBUS_ADC1),
	RT_REG(NU2115_REG_VBUS_ADC0),
	RT_REG(NU2115_REG_VAC1_ADC1),
	RT_REG(NU2115_REG_VAC1_ADC0),
	RT_REG(NU2115_REG_VAC2_ADC1),
	RT_REG(NU2115_REG_VAC2_ADC0),
	RT_REG(NU2115_REG_VOUT_ADC1),
	RT_REG(NU2115_REG_VOUT_ADC0),
	RT_REG(NU2115_REG_VBAT_ADC1),
	RT_REG(NU2115_REG_VBAT_ADC0),
	RT_REG(NU2115_REG_IBAT_ADC1),
	RT_REG(NU2115_REG_IBAT_ADC0),
	RT_REG(NU2115_REG_TSBUS_ADC1),
	RT_REG(NU2115_REG_TSBUS_ADC0),
	RT_REG(NU2115_REG_TSBAT_ADC1),
	RT_REG(NU2115_REG_TSBAT_ADC0),
	RT_REG(NU2115_REG_TDIE_ADC1),
	RT_REG(NU2115_REG_TDIE_ADC0),
	RT_REG(NU2115_REG_TSBUS_FLT),
	RT_REG(NU2115_REG_TSBAT_FLT),
	RT_REG(NU2115_REG_TDIE_ALM),
	RT_REG(NU2115_REG_IBUS_UCP_RCP_THRES),
	RT_REG(NU2115_REG_VAC1_2_PRESENT_DET),
	RT_REG(NU2115_REG_ACDRV1_2_CTRL),
	RT_REG(NU2115_REG_DEV_INFO),
	RT_REG(NU2115_REG_PMID2VOUT_UVP_OVP),
	RT_REG(NU2115_REG_DEGLITCH),
	RT_REG(NU2115_REG_CP_OPTION),
	RT_REG(NU2115_REG_CP_OPTION_1),
	RT_REG(NU2115_REG_CP_OPTION_2),
};

static struct rt_regmap_fops nu2115_rm_fops = {
	.read_device = nu2115_read_device,
	.write_device = nu2115_write_device,
};

static int nu2115_register_regmap(struct nu2115_chip *chip)
{
	struct i2c_client *client = chip->client;
	struct rt_regmap_properties *prop = NULL;

	nu_dbg(chip->dev, "\n");

	prop = devm_kzalloc(&client->dev, sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = chip->desc->rm_name;
	prop->aliases = chip->desc->rm_name;
	prop->register_num = ARRAY_SIZE(nu2115_regmap);
	prop->rm = nu2115_regmap;
	prop->rt_regmap_mode = RT_SINGLE_BYTE | RT_CACHE_DISABLE |
			       RT_IO_PASS_THROUGH;
	prop->io_log_en = 0;

	chip->rm_prop = prop;
	chip->rm_dev = rt_regmap_device_register_ex(chip->rm_prop,
						    &nu2115_rm_fops, chip->dev,
						    client,
						    chip->desc->rm_slave_addr,
						    chip);
	if (!chip->rm_dev) {
		dev_err(chip->dev, "%s register regmap dev fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}
#endif /* CONFIG_RT_REGMAP */

#define I2C_ACCESS_MAX_RETRY	5
static inline int __nu2115_i2c_write8(struct nu2115_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_write(chip->rm_dev, reg, 1, &data);
#else
		ret = nu2115_write_device(chip->client, reg, 1, &data);
#endif /* CONFIG_RT_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CW[0x%02X] = 0x%02X fail\n", __func__,
			reg, data);
		return ret;
	}

	return 0;
}

static inline int __nu2115_i2c_read8(struct nu2115_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_read(chip->rm_dev, reg, 1, data);
#else
		ret = nu2115_read_device(chip->client, reg, 1, data);
#endif /* CONFIG_RT_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}

	return 0;
}

static int nu2115_i2c_read8(struct nu2115_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __nu2115_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __nu2115_i2c_write_block(struct nu2115_chip *chip, u8 reg,
					   u32 len, const u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_write(chip->rm_dev, reg, len, data);
#else
	ret = nu2115_write_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

static inline int __nu2115_i2c_read_block(struct nu2115_chip *chip, u8 reg,
					  u32 len, u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_read(chip->rm_dev, reg, len, data);
#else
	ret = nu2115_read_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

static int nu2115_i2c_read_block(struct nu2115_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __nu2115_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static int nu2115_i2c_test_bit(struct nu2115_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = nu2115_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}
	*one = (data & BIT(shft)) ? true : false;
	return 0;
}

static int nu2115_i2c_update_bits(struct nu2115_chip *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	mutex_lock(&chip->io_lock);
	ret = __nu2115_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __nu2115_i2c_write8(chip, reg, _data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int nu2115_set_bits(struct nu2115_chip *chip, u8 reg, u8 mask)
{
	return nu2115_i2c_update_bits(chip, reg, mask, mask);
}

static inline int nu2115_clr_bits(struct nu2115_chip *chip, u8 reg, u8 mask)
{
	return nu2115_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 nu2115_val_toreg(u32 min, u32 max, u32 step, u32 target,
				  bool ru)
{
	if (target <= min)
		return 0;

	if (target >= max)
		return (max - min) / step;

	if (ru)
		return (target - min + step - 1) / step;
	return (target - min) / step;
}

static inline u8 nu2115_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
					  u32 target)
{
	int i;

	if (target < tbl[0])
		return 0;

	for (i = 0; i < tbl_size - 1; i++) {
		if (target >= tbl[i] && target < tbl[i + 1])
			return i;
	}

	return tbl_size - 1;
}

static u8 nu2115_vbatovp_toreg(u32 uV)
{
	u32 uV_gap;

	if (uV >= 3500000 )
		uV_gap = uV - 3500000;
	else
		uV_gap = 0;

	return nu2115_val_toreg(0, 1575000, 15600, uV_gap, false);
}

static u8 nu2115_vbatovp_alm_toreg(u32 uV)
{
	u32 uV_gap;

	if (uV >= 3500000 )
		uV_gap = uV - 3500000;
	else
		uV_gap = 0;

	return nu2115_val_toreg(0, 1905000, 15000, uV_gap, false);
}

static u8 nu2115_ibatocp_toreg(u32 uA)
{
	u32 uA_gap;

	if (uA >= 2000000 )
		uA_gap = uA - 2000000;
	else
		uA_gap = 0;

	return nu2115_val_toreg(0, 12700000, 100000, uA_gap, false);
}

static u8 nu2115_ibatocp_alm_toreg(u32 uA)
{
	u32 uA_gap;

	if (uA >= 2000000 )
		uA_gap = uA - 2000000;
	else
		uA_gap = 0;

	return nu2115_val_toreg(0, 12700000, 100000, uA_gap, false);
}

/*
static u8 nu2115_ibatucp_toreg(u32 uA)
{
	return nu2115_val_toreg(0, 6350000, 50000, uA, false);
}
*/

static u8 nu2115_vbusovp_toreg(u32 uV)
{
	u32 uV_gap;

	if (uV >= 6000000 )
		uV_gap = uV - 6000000;
	else
		uV_gap = 0;

	return nu2115_val_toreg(0, 6300000, 100000, uV_gap, false);
}

static u8 nu2115_vbusovp_alm_toreg(u32 uV)
{
	u32 uV_gap;

	if (uV >= 6000000 )
		uV_gap = uV - 6000000;
	else
		uV_gap = 0;

	return nu2115_val_toreg(0, 6300000, 100000, uV_gap, false);
}

static u8 nu2115_ibusocp_toreg(u32 uA)
{
	u32 uA_gap;

	if (uA >= 2500000 )
		uA_gap = uA - 2500000;
	else
		uA_gap = 0;

	return nu2115_val_toreg(0, 3750000, 250000, uA_gap, false);
}

static u8 nu2115_ibusocp_alm_toreg(u32 uA)
{
	u32 uA_gap;

	if (uA >= 2500000 )
		uA_gap = uA - 2500000;
	else
		uA_gap = 0;

	return nu2115_val_toreg(0, 3875000, 125000, uA_gap, false);
}

static u8 nu2115_tdie_alm_toreg(u32 temp)
{
	u32 temp_gap;

	if (temp >= 225 )
		temp_gap = temp - 225;
	else
		temp_gap = 0;

	return nu2115_val_toreg(0, 1275, 5, temp_gap, false);
}

static u8 nu2115_pmid2out_uvp_toreg(u32 percent)
{
	return nu2115_val_toreg(1250, 5000, 1250, percent, false);
}

static u8 nu2115_pmid2out_ovp_toreg(u32 percent)
{
	u32 percent_gap;

	if (percent >= 5000)
		percent_gap = percent - 5000;
	else
		percent_gap = 0;

	return nu2115_val_toreg(0, 7500, 2500, percent_gap, false);
}

static u8 nu2115_tsbat_flt_toreg(u32 percent)
{
	return nu2115_val_toreg(0, 498040, 1953, percent, true);
}

static const u32 nu2115_wdt[] = {
	500000, 1000000, 5000000, 30000000,
};

static u8 nu2115_wdt_toreg(u32 uS)
{
	return nu2115_val_toreg_via_tbl(nu2115_wdt, ARRAY_SIZE(nu2115_wdt), uS);
}

//static const u32 nu2115_vacovp[] = {
//	6500000, 10500000, 12000000, 14000000, 16000000, 18000000,
//};

static u8 nu2115_vacovp_toreg(u32 uV)
{
	u32 uV_gap;

	if (uV >= 10000000 )
		uV_gap = uV - 10000000;
	else
		uV_gap = 0;

	return nu2115_val_toreg(0, 3500000, 500000, uV_gap, false);
}

static const u32 nu2115_fsw_set[] = {
	300, 400, 500, 600, 700, 800, 900, 1000,
};

static u8 nu2115_fsw_set_toreg(u32 khZ)
{
	return nu2115_val_toreg_via_tbl(nu2115_fsw_set, ARRAY_SIZE(nu2115_fsw_set), khZ);
}

static const u32 nu2115_ss_timeout[] = {
	0, 12500, 25000, 50000, 100000, 400000, 1500000, 100000000,
};

static u8 nu2115_ss_timeout_toreg(u32 uS)
{
	return nu2115_val_toreg_via_tbl(nu2115_ss_timeout,
								ARRAY_SIZE(nu2115_ss_timeout), uS);
}

static const u32 nu2115_ibusucpf_dg[] = {
	10, 5000, 50000, 150000,
};

static u8 nu2115_ibusucpf_deglitch_toreg(u32 uS)
{
	return nu2115_val_toreg_via_tbl(nu2115_ibusucpf_dg,
								ARRAY_SIZE(nu2115_ibusucpf_dg), uS);
}

static int __nu2115_update_status(struct nu2115_chip *chip);
static int __nu2115_init_chip(struct nu2115_chip *chip);

/* Must be called while holding a lock */
static int nu2115_enable_wdt(struct nu2115_chip *chip, bool en)
{
	int ret;

	if (chip->wdt_en == en)
		return 0;
	ret = (en ? nu2115_clr_bits : nu2115_set_bits)
		(chip, NU2115_REG_CONTROL, NU2115_WDTEN_MASK);
	if (ret < 0)
		return ret;
	chip->wdt_en = en;
	return 0;
}

static int __nu2115_get_adc(struct nu2115_chip *chip,
			    enum nu2115_adc_channel chan, int *val)
{
	int ret;
	u8 data[2];

	ret = nu2115_set_bits(chip, NU2115_REG_ADC_CTRL, NU2115_ADCEN_MASK);
	if (ret < 0)
		goto out;

	usleep_range(60000, 70000);
	ret = nu2115_i2c_read_block(chip, nu2115_adc_reg[chan], 2, data);
	if (ret < 0)
		goto out_dis;
	switch (chan) {
		case NU2115_ADC_VBUS:
		case NU2115_ADC_VAC1:
		case NU2115_ADC_VAC2:
			data[0] &= ~NU2115_ADC_INVALID_BIT_7_MASK;
	 		*val = (data[1] | (data[0] << 8)) * 1000;
			break;
		case NU2115_ADC_IBUS:
		case NU2115_ADC_VOUT:
		case NU2115_ADC_VBAT:
		case NU2115_ADC_IBAT:
			data[0] &= ~NU2115_ADC_INVALID_BIT_5_6_7_MASK;
			*val = (data[1] | (data[0] << 8)) * 1000;
			break;
		case NU2115_ADC_TSBUS:
		case NU2115_ADC_TSBAT:
			data[0] &= ~NU2115_ADC_INVALID_BIT_2_3_4_5_6_7_MASK;
			*val = ((data[1] | (data[0] << 8)) * 100) / 1024;
			break;
		case NU2115_ADC_TDIE:
			data[0] &= ~NU2115_ADC_INVALID_BIT_2_3_4_5_6_7_MASK;
			*val = (data[1] | (data[0] << 8)) / 2;
			break;
		default:
			ret = -ENOTSUPP;
			break;
	}

	if (ret < 0)
		dev_err(chip->dev, "%s %s fail(%d)\n", __func__,
			nu2115_adc_name[chan], ret);
	else
		nu_dbg(chip->dev, "%s %d\n",
			 nu2115_adc_name[chan], *val);
out_dis:
	if (!chip->force_adc_en)
		ret = nu2115_clr_bits(chip, NU2115_REG_ADC_CTRL,
				      NU2115_ADCEN_MASK);
out:
	return ret;
}
static int nu2115_dump_reg(struct nu2115_chip *chip);
static int nu2115_set_pmid2out_ovp(struct nu2115_chip *chip, u32 percent);
static int nu2115_set_pmid2out_uvp(struct nu2115_chip *chip, u32 percent);
static int nu2115_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u32 err_check = BIT(NU2115_IRQIDX_VBUSOVP) |
			BIT(NU2115_IRQIDX_VACOVP1) |
			BIT(NU2115_IRQIDX_VACOVP2) |
			BIT(NU2115_IRQIDX_VBUSERRORHI) |
			BIT(NU2115_IRQIDX_VOUTOVP);
	u32 stat_check = BIT(NU2115_IRQIDX_VAC1INSERT);// |
//			 BIT(NU2115_IRQIDX_VOUTPRESENT);

//	nu2115_dump_reg(chip);
	nu_dbg(chip->dev, "%d\n", en);

	mutex_lock(&chip->adc_lock);
	chip->force_adc_en = en;
	if (!en) {
		ret = nu2115_clr_bits(chip, NU2115_REG_CHG_CTRL,
				      NU2115_CHGEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = nu2115_clr_bits(chip, NU2115_REG_ADC_CTRL,
				      NU2115_ADCEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = nu2115_enable_wdt(chip, false);
		goto out_unlock;
	}

	/* Enable ADC to check status before enable charging */
	ret = nu2115_set_bits(chip, NU2115_REG_ADC_CTRL, NU2115_ADCEN_MASK);
	if (ret < 0)
		goto out_unlock;
	mutex_unlock(&chip->adc_lock);
	usleep_range(60000, 70000);

	mutex_lock(&chip->stat_lock);
	__nu2115_update_status(chip);
	if ((chip->stat & err_check) ||
	    ((chip->stat & stat_check) != stat_check)) {
		dev_info(chip->dev, "error(0x%08X,0x%08X,0x%08X)\n",
			chip->stat, err_check, stat_check);
		ret = -EINVAL;
		mutex_unlock(&chip->stat_lock);
		goto out;
	}
	mutex_unlock(&chip->stat_lock);
	if (!chip->desc->wd_timeout_dis) {
		ret = nu2115_enable_wdt(chip, true);
		if (ret < 0)
			goto out;
	}

	usleep_range(1000, 2000);
	/*  Increase pmid2out ovp&ucp thresholds before enable charging */
	ret = nu2115_set_pmid2out_ovp(chip, 12500);/* 12.5% */
	if (ret < 0)
		goto out;
	ret = nu2115_set_pmid2out_uvp(chip, 5000);/* -5% */
	if (ret < 0)
		goto out;
	/*  enable charging */
	ret = nu2115_set_bits(chip, NU2115_REG_CHG_CTRL, NU2115_CHGEN_MASK);

	usleep_range(25000, 30000);
	/*  Recovery pmid2out ovp&ucp thresholds after enable charging */
	ret = nu2115_set_pmid2out_ovp(chip, 12500);/* 7.5% */
	if (ret < 0)
		goto out;
	ret = nu2115_set_pmid2out_uvp(chip, 5000);/* -2.5% */
	if (ret < 0)
		goto out;
	if (dbg_log_en)
		nu2115_dump_reg(chip);
	goto out;
out_unlock:
	mutex_unlock(&chip->adc_lock);
out:
	return ret;
}

static int nu2115_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	bool chg_en;
	bool conv_active;
	struct nu2115_chip *chip = charger_get_data(chg_dev);

	//check charge chg_en bit
	ret = nu2115_i2c_test_bit(chip, NU2115_REG_CHG_CTRL, NU2115_CHGEN_SHFT,
				  &chg_en);
	if (ret < 0)
		return ret;

	//check charge Converter Active Status
	ret = nu2115_i2c_test_bit(chip, NU2115_REG_CHG_CTRL, NU2115_CONVACTIVE_SHFT,
				  &conv_active);
	if (ret < 0)
		return ret;

	*en = chg_en & conv_active;
	nu_dbg(chip->dev, "%d\n",  *en);
	return 0;
}

static inline enum nu2115_adc_channel to_nu2115_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		return NU2115_ADC_VBUS;
	case ADC_CHANNEL_VBAT:
		return NU2115_ADC_VBAT;
	case ADC_CHANNEL_IBUS:
		return NU2115_ADC_IBUS;
	case ADC_CHANNEL_IBAT:
		return NU2115_ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC:
		return NU2115_ADC_TDIE;
	case ADC_CHANNEL_VOUT:
		return NU2115_ADC_VOUT;
	default:
		break;
	}
	return NU2115_ADC_NOTSUPP;
}

static int nu2115_get_adc(struct charger_device *chg_dev, enum adc_channel chan,
			  int *min, int *max)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	enum nu2115_adc_channel _chan = to_nu2115_adc(chan);

	if (_chan == NU2115_ADC_NOTSUPP)
		return -EINVAL;
	mutex_lock(&chip->adc_lock);
	ret = __nu2115_get_adc(chip, _chan, max);
	if (ret < 0)
		goto out;
	if (min != max)
		*min = *max;
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int nu2115_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	enum nu2115_adc_channel _chan = to_nu2115_adc(chan);

	if (_chan == NU2115_ADC_NOTSUPP)
		return -EINVAL;
	*min = *max = nu2115_adc_accuracy_tbl[_chan];
	return 0;
}

static int nu2115_set_pmid2out_uvp(struct nu2115_chip *chip, u32 percent)
{
	u8 reg = nu2115_pmid2out_uvp_toreg(percent);

	nu_dbg(chip->dev, "%d(0x%02X)\n", percent, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_PMID2VOUT_UVP_OVP,
				      reg << NU2115_PMID2VOUT_UVP_SHFT, NU2115_PMID2VOUT_UVP_MASK);
}

static int nu2115_set_pmid2out_ovp(struct nu2115_chip *chip, u32 percent)
{
	u8 reg = nu2115_pmid2out_ovp_toreg(percent);

	nu_dbg(chip->dev, "%d(0x%02X)\n", percent, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_PMID2VOUT_UVP_OVP,
				      reg << NU2115_PMID2VOUT_OVP_SHFT, NU2115_PMID2VOUT_OVP_MASK);
}

static int nu2115_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_vbusovp_toreg(uV);

	nu_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BUS_OVP, reg,
				      NU2115_VBUSOVP_MASK);
}

static int nu2115_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_ibusocp_toreg(uA);

	nu_dbg(chip->dev, "%d(0x%02X)\n", uA, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BUS_OCP_UCP, reg,
				      NU2115_IBUSOCP_MASK);
}

static int nu2115_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_vbatovp_toreg(uV);

	nu_dbg(chip->dev, " %d(0x%02X)\n", uV, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BAT_OVP, reg,
				      NU2115_VBATOVP_MASK);
}

static int nu2115_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_vbatovp_alm_toreg(uV);

	nu_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BAT_OVP_ALM, reg,
				      NU2115_VBATOVP_ALM_MASK);
}

static int nu2115_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 data;

	nu_dbg(chip->dev, "\n");
	mutex_lock(&chip->io_lock);
	ret = __nu2115_i2c_read8(chip, NU2115_REG_BAT_OVP_ALM, &data);
	if (ret < 0)
		goto out;
	data |= NU2115_VBATOVP_ALMDIS_MASK;
	ret = __nu2115_i2c_write8(chip, NU2115_REG_BAT_OVP_ALM, data);
	if (ret < 0)
		goto out;
	data &= ~NU2115_VBATOVP_ALMDIS_MASK;
	ret = __nu2115_i2c_write8(chip, NU2115_REG_BAT_OVP_ALM, data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int nu2115_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_vbusovp_alm_toreg(uV);

	nu_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BUS_OVP_ALM, reg,
				      NU2115_VBUSOVP_ALM_MASK);
}

static int nu2115_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);

	mutex_lock(&chip->adc_lock);
	ret = nu2115_set_bits(chip, NU2115_REG_ADC_CTRL, NU2115_ADCEN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(60000, 70000);
	ret = nu2115_i2c_test_bit(chip, NU2115_REG_CP_OPTION_1,
				  NU2115_VBUSUCP_FLAG_SHFT, err);

	if (!chip->force_adc_en)
		nu2115_clr_bits(chip, NU2115_REG_ADC_CTRL, NU2115_ADCEN_MASK);
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int nu2115_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 data;

	nu_dbg(chip->dev, "\n");
	mutex_lock(&chip->io_lock);
	ret = __nu2115_i2c_read8(chip, NU2115_REG_BUS_OVP_ALM, &data);
	if (ret < 0)
		goto out;
	data |= NU2115_VBUSOVP_ALMDIS_MASK;
	ret = __nu2115_i2c_write8(chip, NU2115_REG_BUS_OVP_ALM, data);
	if (ret < 0)
		goto out;
	data &= ~NU2115_VBUSOVP_ALMDIS_MASK;
	ret = __nu2115_i2c_write8(chip, NU2115_REG_BUS_OVP_ALM, data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int nu2115_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	u8 reg = nu2115_ibatocp_toreg(uA);

	nu_dbg(chip->dev, "%d(0x%02X)\n", uA, reg);
	return nu2115_i2c_update_bits(chip, NU2115_REG_BAT_OCP, reg,
				      NU2115_IBATOCP_MASK);
}

static int nu2115_init_chip(struct charger_device *chg_dev)
{
	int i, ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);
	const struct nu2115_reg_defval *reg_defval;
	u8 val;

	for (i = 0; i < ARRAY_SIZE(nu2115_init_chip_check_reg); i++) {
		reg_defval = &nu2115_init_chip_check_reg[i];
		ret = nu2115_i2c_read8(chip, reg_defval->reg, &val);
		if (ret < 0)
			return ret;
		if ((val & reg_defval->mask) == reg_defval->value) {
			nu_dbg(chip->dev, "chip reset happened, reinit\n");
			return __nu2115_init_chip(chip);
		}
	}
	return 0;
}

static int nu2115_dump_reg(struct nu2115_chip *chip)
{
	int i, ret;
	u8 val;

	for (i = 0; i <= NU2115_REG_MAX; i++) {
		ret = nu2115_i2c_read8(chip, i, &val);
		dev_info(chip->dev, "nu2115_reg[0x%02x] = 0x%02x\n",  i, val);
	}

    return ret;
}

static int nu2115_typec_mux_otg_enable(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct nu2115_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s: %d\n", __func__, en);

	if (en) {
		//1-- enable OTG mode: EN_OTG = 1
		ret = nu2115_set_bits(chip, NU2115_REG_VAC1_2_PRESENT_DET, NU2115_OTGEN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable otg mode fail !\n");
			return ret;
		}

		//2-- enable ACDRV1: ACDRV1_STAT = 1
		ret = nu2115_set_bits(chip, NU2115_REG_ACDRV1_2_CTRL, NU2115_ACDRV1_STAT_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable acdrv1_stat fail !\n");
			return ret;
		}
	} else {

		//1-- disable ACDRV1: ACDRV1_STAT = 0
		ret = nu2115_clr_bits(chip, NU2115_REG_ACDRV1_2_CTRL, NU2115_ACDRV1_STAT_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable acdrv1_stat fail !\n");
			return ret;
		}

		//2-- disable OTG mode: ENOTG = 0
		ret = nu2115_clr_bits(chip, NU2115_REG_VAC1_2_PRESENT_DET, NU2115_OTGEN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "disable otg mode fail !\n");
			return ret;
		}
	}

	return 0;
}

static inline void nu2115_set_notify(struct nu2115_chip *chip,
				     enum nu2115_notify notify)
{
	mutex_lock(&chip->notify_lock);
	chip->notify |= BIT(notify);
	mutex_unlock(&chip->notify_lock);
}

//NU2115_SF_FLAG1 IRQ
 static int nu2115_vbatovp_irq_handler(struct nu2115_chip *chip)
 {
	 dev_info(chip->dev, "%s\n", __func__);
	 nu2115_set_notify(chip, NU2115_NOTIFY_VBATOVP);
	 return 0;
 }

static int nu2115_vbatovpalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VBATOVPALM);
	return 0;
}

static int nu2115_voutovp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VOUTOVP);
	return 0;
}

static int nu2115_ibatocp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_IBATOCP);
	return 0;
}

static int nu2115_ibatocpalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_ibatucpalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vbusovp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VBUSOVP);
	return 0;
}

static int nu2115_vbusovpalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VBUSOVPALM);
	return 0;
}

//NU2115_SF_FLAG2 IRQ
static int nu2115_ibusocp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_IBUSOCP);
	return 0;
}

static int nu2115_ibusocpalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_ibusucpf_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_IBUSUCPF);
	return 0;
}

static int nu2115_ibusrcpf_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//NU2115_SF_FLAG3 IRQ
static int nu2115_vacovp1_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vacovp2_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vac1insert_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vac2insert_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vbuspresent_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_acrb1config_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_acrb2config_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}


//NU2115_SF_FLAG4 IRQ
static int nu2115_adcdone_irq_handler(struct nu2115_chip *chip)
{
	nu_dbg(chip->dev, "\n");
	return 0;
}

static int nu2115_sstimeout_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_tsalm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_tsflt_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_tdiealm_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_wdtimeout_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//NU2115_SF_FLAG5 IRQ
static int nu2115_vbuserrorhi_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int nu2115_vbuserrorlow_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//NU2115_SF_PMID2VOUTOVP_FLAG IRQ
static int nu2115_pmid2voutovp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VOUTOVP);
	return 0;
}

static int nu2115_pmid2voutuvp_irq_handler(struct nu2115_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	nu2115_set_notify(chip, NU2115_NOTIFY_VOUTOVP);
	return 0;
}

struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct nu2115_chip *chip);
	u8 flag_idx;
	u8 stat_idx;
	u8 flag_mask;
	u8 stat_mask;
	u32 irq_idx;
	bool stat_only;
};

#define NU2115_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = nu2115_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = (1 << _flag_s), .stat_mask = (1 << _stat_s), \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}


static const struct irq_map_desc nu2115_irq_map_tbl[NU2115_IRQIDX_MAX] = {
	NU2115_IRQ_DESC(vbatovp, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 7, 7,
			    NU2115_IRQIDX_VBATOVP, false),
	NU2115_IRQ_DESC(vbatovpalm, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 7, 7,
			    NU2115_IRQIDX_VBATOVPALM, false),
	NU2115_IRQ_DESC(voutovp, NU2115_SF_VOUT_OVP_STAT_FLAG, NU2115_SF_VOUT_OVP_STAT_FLAG, 1, 2,
			    NU2115_IRQIDX_VOUTOVP, false),
	NU2115_IRQ_DESC(ibatocp, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 6, 6,
			    NU2115_IRQIDX_IBATOCP, false),
	NU2115_IRQ_DESC(ibatocpalm, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 6, 6,
			    NU2115_IRQIDX_IBATOCPALM, false),
	NU2115_IRQ_DESC(ibatucpalm, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 3, 3,
			    NU2115_IRQIDX_IBATUCPALM, false),
	NU2115_IRQ_DESC(vbusovp, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 5, 5,
			    NU2115_IRQIDX_VBUSOVP, false),
	NU2115_IRQ_DESC(vbusovpalm, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 5, 5,
			    NU2115_IRQIDX_VBUSOVPALM, false),

	NU2115_IRQ_DESC(ibusocp, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 4, 4,
			    NU2115_IRQIDX_IBUSOCP, false),
	NU2115_IRQ_DESC(ibusocpalm, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 4, 4,
			    NU2115_IRQIDX_IBUSOCPALM, false),
	NU2115_IRQ_DESC(ibusucpf, NU2115_SF_BUS_UCP_STAT_FLAG, NU2115_SF_BUS_UCP_STAT_FLAG, 6, 6,//only flag, stat use flag
				NU2115_IRQIDX_IBUSUCPF, false),
	NU2115_IRQ_DESC(ibusrcpf, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 3, 3,
			    NU2115_IRQIDX_IBUSRCPF, false),

	NU2115_IRQ_DESC(vacovp1, NU2115_SF_AC1_OVP_STAT_FLAG, NU2115_SF_AC1_OVP_STAT_FLAG, 6, 7,
			    NU2115_IRQIDX_VACOVP1, false),
	NU2115_IRQ_DESC(vacovp2, NU2115_SF_AC2_OVP_STAT_FLAG, NU2115_SF_AC1_OVP_STAT_FLAG, 6, 7,
			    NU2115_IRQIDX_VACOVP2, false),

	NU2115_IRQ_DESC(vac1insert, NU2115_SF_VAC1_2_PRESENT_STAT_FLAG, NU2115_SF_VAC1_2_PRESENT_STAT_FLAG, 6, 7,
			    NU2115_IRQIDX_VAC1INSERT, false),
	NU2115_IRQ_DESC(vac2insert, NU2115_SF_VAC1_2_PRESENT_STAT_FLAG, NU2115_SF_VAC1_2_PRESENT_STAT_FLAG, 3, 4,
			    NU2115_IRQIDX_VAC2INSERT, false),
	NU2115_IRQ_DESC(vbuspresent, NU2115_SF_BUS_PRESENT_STAT_FLAG, NU2115_SF_BUS_PRESENT_STAT_FLAG, 6, 7,
			    NU2115_IRQIDX_VBUSPRESENT, false),
	NU2115_IRQ_DESC(acrb1config, NU2115_SF_ACDRV1_2_STAT_FLAG, NU2115_SF_ACDRV1_2_STAT_FLAG, 4, 5,
			    NU2115_IRQIDX_ACRB1CONFIG, false),
	NU2115_IRQ_DESC(acrb2config, NU2115_SF_ACDRV1_2_STAT_FLAG, NU2115_SF_ACDRV1_2_STAT_FLAG, 1, 2,
			    NU2115_IRQIDX_ACRB2CONFIG, false),

	NU2115_IRQ_DESC(adcdone, NU2115_SF_INT_FLAG, NU2115_SF_INT_STAT, 0, 0,
			    NU2115_IRQIDX_ADCDONE, false),
	NU2115_IRQ_DESC(sstimeout, NU2115_SF_SSTIMEOUT_FLAG, NU2115_SF_SSTIMEOUT_FLAG, 3, 3,//only flag, stat use flag
			    NU2115_IRQIDX_SSTIMEOUT, false),
	NU2115_IRQ_DESC(tsalm, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 2, 2,
			    NU2115_IRQIDX_TSALM, false),
	NU2115_IRQ_DESC(tsflt, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 1, 1,
			    NU2115_IRQIDX_TSFLT, false),
	NU2115_IRQ_DESC(tdiealm, NU2115_SF_FLT_FLAG, NU2115_SF_FLT_STAT, 0, 0,
			    NU2115_IRQIDX_TDIEALM, false),
	NU2115_IRQ_DESC(wdtimeout, NU2115_SF_WDT_FLAG, NU2115_SF_WDT_FLAG, 3, 3,//only flag, stat use flag
			    NU2115_IRQIDX_WDTIMEOUT, false),

	NU2115_IRQ_DESC(vbuserrorhi, NU2115_SF_SSTIMEOUT_FLAG, NU2115_SF_VBUSERR_STAT, 4, 3,
			    NU2115_IRQIDX_VBUSERRORHI, false),
	NU2115_IRQ_DESC(vbuserrorlow, NU2115_SF_SSTIMEOUT_FLAG, NU2115_SF_VBUSERR_STAT, 5, 2,
			    NU2115_IRQIDX_VBUSERRORLOW, false),

	NU2115_IRQ_DESC(pmid2voutovp, NU2115_SF_PMID2VOUTOVP_FLAG, NU2115_SF_VBUSERR_STAT, 2, 4,
			    NU2115_IRQIDX_PMID2VOUTOVP, false),
	NU2115_IRQ_DESC(pmid2voutuvp, NU2115_SF_PMID2VOUTOVP_FLAG, NU2115_SF_VBUSERR_STAT, 3, 5,
			    NU2115_IRQIDX_PMID2VOUTUVP, false),
};

static int __nu2115_update_status(struct nu2115_chip *chip)
{
	int i;
	u8 sf[NU2115_SF_MAX] = {0};
	const struct irq_map_desc *desc;

	for (i = 0; i < NU2115_SF_MAX; i++)
		nu2115_i2c_read8(chip, nu2115_reg_sf[i], &sf[i]);

	for (i = 0; i < ARRAY_SIZE(nu2115_irq_map_tbl); i++) {
		desc = &nu2115_irq_map_tbl[i];
		if (sf[desc->flag_idx] & desc->flag_mask) {
			if (!desc->stat_only)
				chip->flag |= BIT(desc->irq_idx);
		}
		if (sf[desc->stat_idx] & desc->stat_mask) {
			/* if (desc->stat_only &&
			    !(chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx); */
			chip->stat |= BIT(desc->irq_idx);
		} else {
			/* if (desc->stat_only &&
			    (chip->stat & BIT(desc->irq_idx)))
				chip->flag |= BIT(desc->irq_idx); */
			chip->stat &= ~BIT(desc->irq_idx);
		}
	}
	return 0;
}

static int nu2115_notify_task_threadfn(void *data)
{
	int i;
	struct nu2115_chip *chip = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->wq, chip->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(chip->dev);
		mutex_lock(&chip->notify_lock);
		nu2115_dump_reg(chip);
		for (i = 0; i < NU2115_NOTIFY_MAX; i++) {
			if (chip->notify & BIT(i)) {
				chip->notify &= ~BIT(i);
				mutex_unlock(&chip->notify_lock);
				charger_dev_notify(chip->chg_dev,
						   nu2115_chgdev_notify_map[i]);
				mutex_lock(&chip->notify_lock);
			}
		}
		mutex_unlock(&chip->notify_lock);
		pm_relax(chip->dev);
	}
out:
	return 0;
}

static irqreturn_t nu2115_irq_handler(int irq, void *data)
{
	int i;
	struct nu2115_chip *chip = data;
	const struct irq_map_desc *desc;

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->stat_lock);
	__nu2115_update_status(chip);
	for (i = 0; i < ARRAY_SIZE(nu2115_irq_map_tbl); i++) {
		desc = &nu2115_irq_map_tbl[i];
		if ((chip->flag & (1 << desc->irq_idx)) && desc->hdlr)
			desc->hdlr(chip);
	}
	chip->flag = 0;
	wake_up_interruptible(&chip->wq);
	mutex_unlock(&chip->stat_lock);
	pm_relax(chip->dev);
	return IRQ_HANDLED;
}

static const struct charger_ops nu2115_chg_ops = {
	.enable = nu2115_enable_chg,
	.is_enabled = nu2115_is_chg_enabled,
	.get_adc = nu2115_get_adc,
	.set_vbusovp = nu2115_set_vbusovp,
	.set_ibusocp = nu2115_set_ibusocp,
	.set_vbatovp = nu2115_set_vbatovp,
	.set_ibatocp = nu2115_set_ibatocp,
	.init_chip = nu2115_init_chip,
	.set_vbatovp_alarm = nu2115_set_vbatovp_alarm,
	.reset_vbatovp_alarm = nu2115_reset_vbatovp_alarm,
	.set_vbusovp_alarm = nu2115_set_vbusovp_alarm,
	.reset_vbusovp_alarm = nu2115_reset_vbusovp_alarm,
	.is_vbuslowerr = nu2115_is_vbuslowerr,
	.get_adc_accuracy = nu2115_get_adc_accuracy,
	.typec_mux_otg_enable = nu2115_typec_mux_otg_enable,
};

static int nu2115_register_chgdev(struct nu2115_chip *chip)
{
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name, chip->dev,
						chip, &nu2115_chg_ops,
						&chip->chg_prop);
	return chip->chg_dev ? 0 : -EINVAL;
}

static int nu2115_clearall_irq(struct nu2115_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < NU2115_SF_MAX; i++) {
		ret = nu2115_i2c_read8(chip, nu2115_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int nu2115_init_irq(struct nu2115_chip *chip)
{
	int ret = 0, len = 0;
	char *name = NULL;

	nu_dbg(chip->dev, "\n");
	ret = nu2115_clearall_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s clr all irq fail(%d)\n", __func__, ret);
		return ret;
	}

	chip->irq = gpiod_to_irq(chip->irq_gpio);
	if (chip->irq < 0) {
		dev_err(chip->dev, "%s irq mapping fail(%d)\n", __func__,
			chip->irq);
		return ret;
	}
	nu_dbg(chip->dev, "irq = %d\n", chip->irq);

	/* Request threaded IRQ */
	len = strlen(chip->desc->chg_name);
	name = devm_kzalloc(chip->dev, len + 5, GFP_KERNEL);
	snprintf(name, len + 5, "%s_irq", chip->desc->chg_name);
	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
		nu2115_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
		chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s request thread irq fail(%d)\n", __func__,
			ret);
		return ret;
	}
	device_init_wakeup(chip->dev, true);
	return 0;
}

#define NU2115_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct nu2115_desc, name), reg, shft, mask, func, base}

struct nu2115_dtprop {
	const char *name;
	size_t offset;
	u8 reg;
	u8 shft;
	u8 mask;
	u8 (*toreg)(u32 val);
	u8 base;
};

static inline void nu2115_parse_dt_u32(struct device_node *np, void *desc,
				       const struct nu2115_dtprop *props,
				       int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void nu2115_parse_dt_bool(struct device_node *np, void *desc,
					const struct nu2115_dtprop *props,
					int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		*((bool *)(desc + props[i].offset)) =
			of_property_read_bool(np, props[i].name);
	}
}

static inline int nu2115_apply_dt(struct nu2115_chip *chip, void *desc,
				  const struct nu2115_dtprop *props,
				  int prop_cnt)
{
	int i, ret;
	u32 val;

	for (i = 0; i < prop_cnt; i++) {
		val = *(u32 *)(desc + props[i].offset);
		if (props[i].toreg)
			val = props[i].toreg(val);
		val += props[i].base;
		ret = nu2115_i2c_update_bits(chip, props[i].reg,
					     val << props[i].shft,
					     props[i].mask);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct nu2115_dtprop nu2115_dtprops_u32[] = {
	//Reg00
	NU2115_DT_VALPROP(vbatovp, NU2115_REG_BAT_OVP, 0, 0x7f,
			  nu2115_vbatovp_toreg, 0),
	//Reg01
	NU2115_DT_VALPROP(vbatovp_alm, NU2115_REG_BAT_OVP_ALM, 0, 0x7f,
			  nu2115_vbatovp_alm_toreg, 0),
	//Reg02
	NU2115_DT_VALPROP(ibatocp, NU2115_REG_BAT_OCP, 0, 0x7f,
			  nu2115_ibatocp_toreg, 0),
	//Reg03
	NU2115_DT_VALPROP(ibatocp_alm, NU2115_REG_BAT_OCP_ALM, 0, 0x7f,
			  nu2115_ibatocp_alm_toreg, 0),
	//Reg07
	NU2115_DT_VALPROP(vbusovp, NU2115_REG_BUS_OVP, 0, 0x3f,
			  nu2115_vbusovp_toreg, 0),
	//Reg08
	NU2115_DT_VALPROP(vbusovp_alm, NU2115_REG_BUS_OVP_ALM, 0, 0x3f,
			  nu2115_vbusovp_alm_toreg, 0),
	//Reg09
	NU2115_DT_VALPROP(ibusocp, NU2115_REG_BUS_OCP_UCP, 0, 0x0f,
			  nu2115_ibusocp_toreg, 0),
	//Reg0A
	NU2115_DT_VALPROP(ibusocp_alm, NU2115_REG_BUS_OCP_ALM, 0, 0x1f,
			  nu2115_ibusocp_alm_toreg, 0),
	//Reg2D
	NU2115_DT_VALPROP(tdie_alm, NU2115_REG_TDIE_ALM, 0, 0xff,
			  nu2115_tdie_alm_toreg, 0),
	//Reg2B
	NU2115_DT_VALPROP(tsbus_flt, NU2115_REG_TSBUS_FLT, 0, 0xff,
			  nu2115_tsbat_flt_toreg, 0),
	//Reg2C
	NU2115_DT_VALPROP(tsbat_flt, NU2115_REG_TSBAT_FLT, 0, 0xff,
			  nu2115_tsbat_flt_toreg, 0),
	//Reg05
	NU2115_DT_VALPROP(vac1ovp, NU2115_REG_AC1_PROTECTION, 0, 0x07,
			  nu2115_vacovp_toreg, 0),
	//Reg06
	NU2115_DT_VALPROP(vac2ovp, NU2115_REG_AC2_PROTECTION, 0, 0x07,
			  nu2115_vacovp_toreg, 0),
	//Reg0D
	NU2115_DT_VALPROP(fsw_set, NU2115_REG_CONTROL, 4, 0x70,
			  nu2115_fsw_set_toreg, 0),
	NU2115_DT_VALPROP(wd_timeout, NU2115_REG_CONTROL, 0, 0x03,
			  nu2115_wdt_toreg, 0),
	//Reg2E
	NU2115_DT_VALPROP(ibat_rsense, NU2115_REG_IBUS_UCP_RCP_THRES, 1, 0x02, NULL, 0),
	NU2115_DT_VALPROP(ss_timeout, NU2115_REG_IBUS_UCP_RCP_THRES, 5, 0x70,
			  nu2115_ss_timeout_toreg, 0),
	//Reg33
	NU2115_DT_VALPROP(ibusucpf_deglitch, NU2115_REG_DEGLITCH, 0, 0x03,
			  nu2115_ibusucpf_deglitch_toreg, 0),
	NU2115_DT_VALPROP(vbus_valid_deg, NU2115_REG_DEGLITCH, 2, 0x04, NULL, 0),

	//Reg32
	NU2115_DT_VALPROP(pmid2out_uvp, NU2115_REG_PMID2VOUT_UVP_OVP, 6, 0xC0,
			  nu2115_pmid2out_uvp_toreg, 0),
	NU2115_DT_VALPROP(pmid2out_ovp, NU2115_REG_PMID2VOUT_UVP_OVP, 4, 0x30,
			  nu2115_pmid2out_ovp_toreg, 0),
	//Reg0D
	NU2115_DT_VALPROP(freq_shift, NU2115_REG_CONTROL, 4, 0x70, NULL, 0),
};

static const struct nu2115_dtprop nu2115_dtprops_bool[] = {
	//Reg00
	NU2115_DT_VALPROP(vbatovp_dis, NU2115_REG_BAT_OVP, 7, 0x80, NULL, 0),
	//Reg01
	NU2115_DT_VALPROP(vbatovp_alm_dis, NU2115_REG_BAT_OVP_ALM, 7, 0x80, NULL, 0),
	//Reg02
	NU2115_DT_VALPROP(ibatocp_dis, NU2115_REG_BAT_OCP, 7, 0x80, NULL, 0),
	//Reg03
	NU2115_DT_VALPROP(ibatocp_alm_dis, NU2115_REG_BAT_OCP_ALM, 7, 0x80, NULL, 0),
	//Reg09
	NU2115_DT_VALPROP(ibusucp_dis, NU2115_REG_BUS_OCP_UCP, 4, 0x10, NULL, 0),
	//Reg35
	NU2115_DT_VALPROP(vbus_errhi_dis, NU2115_REG_CP_OPTION_1, 7, 0x80, NULL, 0),
	//Reg07
	NU2115_DT_VALPROP(vbus_pd_en, NU2115_REG_BUS_OVP, 7, 0x80, NULL, 0),
	//Reg08
	NU2115_DT_VALPROP(vbusovp_alm_dis, NU2115_REG_BUS_OVP_ALM, 7, 0x80, NULL, 0),
	//Reg09
	NU2115_DT_VALPROP(ibusocp_dis, NU2115_REG_BUS_OCP_UCP, 7, 0x80, NULL, 0),
	//Reg0A
	NU2115_DT_VALPROP(ibusocp_alm_dis, NU2115_REG_BUS_OCP_ALM, 7, 0x80, NULL, 0),
	//Reg0E
	NU2115_DT_VALPROP(tshut_dis, NU2115_REG_CHG_CTRL, 0, 0x00, NULL, 0),
	//Reg05
	NU2115_DT_VALPROP(vac1_pd_en, NU2115_REG_AC1_PROTECTION, 4, 0x10, NULL, 0),
	//Reg06
	NU2115_DT_VALPROP(vac2_pd_en, NU2115_REG_AC2_PROTECTION, 4, 0x10, NULL, 0),
	//Reg0D
	NU2115_DT_VALPROP(wd_timeout_dis, NU2115_REG_CONTROL, 2, 0x04, NULL, 0),
	//Reg0B
	NU2115_DT_VALPROP(voutovp_dis, NU2115_REG_VOUT_OVP, 7, 0x80, NULL, 0),
	//Reg15
	NU2115_DT_VALPROP(ibusadc_dis, NU2115_REG_ADC_CTRL, 1, 0x02, NULL, 0),
	NU2115_DT_VALPROP(vbusadc_dis, NU2115_REG_ADC_CTRL, 0, 0x01, NULL, 0),
	//Reg16
	NU2115_DT_VALPROP(vacadc1_dis, NU2115_REG_ADC_FN_DIS, 7, 0x80, NULL, 0),
	NU2115_DT_VALPROP(vacadc2_dis, NU2115_REG_ADC_FN_DIS, 6, 0x40, NULL, 0),
	NU2115_DT_VALPROP(voutadc_dis, NU2115_REG_ADC_FN_DIS, 5, 0x20, NULL, 0),
	NU2115_DT_VALPROP(vbatadc_dis, NU2115_REG_ADC_FN_DIS, 4, 0x10, NULL, 0),
	NU2115_DT_VALPROP(ibatadc_dis, NU2115_REG_ADC_FN_DIS, 3, 0x08, NULL, 0),
	NU2115_DT_VALPROP(tsbusadc_dis, NU2115_REG_ADC_FN_DIS, 2, 0x04, NULL, 0),
	NU2115_DT_VALPROP(tsbatadc_dis, NU2115_REG_ADC_FN_DIS, 1, 0x02, NULL, 0),
	NU2115_DT_VALPROP(tdieadc_dis, NU2115_REG_ADC_FN_DIS, 0, 0x01, NULL, 0),
};

static int nu2115_parse_dt(struct nu2115_chip *chip)
{
	struct nu2115_desc *desc;
	struct device_node *np = chip->dev->of_node;
	struct device_node *child_np;

	if (!np)
		return -ENODEV;

	chip->irq_gpio = devm_gpiod_get(chip->dev, "nu2115,intr", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio))
		return PTR_ERR(chip->irq_gpio);

	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &nu2115_desc_defval, sizeof(*desc));
	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		nu_dbg(chip->dev, "no rm name\n");
	if (of_property_read_u8(np, "rm_slave_addr", &desc->rm_slave_addr) < 0)
		nu_dbg(chip->dev, "no regmap slave addr\n");
	child_np = of_get_child_by_name(np, nu2115_type_name[chip->type]);
	if (!child_np) {
		nu_dbg(chip->dev, "no node(%s) found\n",
			nu2115_type_name[chip->type]);
		return -ENODEV;
	}
	if (of_property_read_string(child_np, "chg_name", &desc->chg_name) < 0)
		nu_dbg(chip->dev, "no chg name\n");
	nu2115_parse_dt_u32(child_np, (void *)desc, nu2115_dtprops_u32,
			    ARRAY_SIZE(nu2115_dtprops_u32));
	nu2115_parse_dt_bool(child_np, (void *)desc, nu2115_dtprops_bool,
			     ARRAY_SIZE(nu2115_dtprops_bool));

	chip->desc = desc;
	return 0;
}

static int nu2115_reset_register(struct nu2115_chip *chip)
{
	int ret;

	ret = nu2115_set_bits(chip, NU2115_REG_CONTROL, NU2115_RST_MASK);
	nu_dbg(chip->dev, "ret(%d)\n", ret);
	usleep_range(5, 10);
	return ret;
}

static int __nu2115_init_chip(struct nu2115_chip *chip)
{
	int ret;

	nu_dbg(chip->dev, "\n");
	ret = nu2115_reset_register(chip);
	if (ret < 0)
		return ret;
	ret = nu2115_apply_dt(chip, (void *)chip->desc, nu2115_dtprops_u32,
			      ARRAY_SIZE(nu2115_dtprops_u32));
	if (ret < 0)
		return ret;
	ret = nu2115_apply_dt(chip, (void *)chip->desc, nu2115_dtprops_bool,
			      ARRAY_SIZE(nu2115_dtprops_bool));
	if (ret < 0)
		return ret;

	if (dbg_log_en == true) {
		nu2115_dump_reg(chip);
	}
	chip->wdt_en = !chip->desc->wd_timeout_dis;
	return chip->wdt_en ? nu2115_enable_wdt(chip, false) : 0;
}

static int nu2115_check_devinfo(struct i2c_client *client, u8 *chip_rev,
				enum nu2115_type *type)
{
	int ret;

	dev_info(&client->dev, "%s rev\n", __func__);
	ret = i2c_smbus_read_byte_data(client, NU2115_REG_DEV_INFO);
	if (ret < 0) {
		dev_err(&client->dev, "%s 1 ret=%d\n", __func__, ret);
		return ret;
	} else {
		dev_info(&client->dev, "%s 1 ret=%d\n", __func__, ret);
	}

	if ((ret & NU2115_DEVID_MASK) != NU2115_DEVID) {
		dev_info(&client->dev, "%s 2 ret=%d\n", __func__, ret);
		//return -ENODEV;
	}

	*chip_rev = (ret & NU2115_DEVREV_MASK) >> NU2115_DEVREV_SHFT;

	ret = i2c_smbus_read_byte_data(client, NU2115_REG_IBUS_UCP_RCP_THERES);
	if (ret < 0) {
		dev_err(&client->dev, "%s 3 ret=%d\n", __func__, ret);
		return ret;
	}
	*type = (ret & NU2115_MS_MASK) >> NU2115_MS_SHFT;
	if (*type < 0)
		return -EINVAL;
	dev_info(&client->dev, "%s rev(0x%02X), type(%s)\n", __func__,
		 *chip_rev, nu2115_type_name[*type]);

	return 0;
}

static int nu2115_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int ret;
	struct nu2115_chip *chip;
	u8 chip_rev;
	enum nu2115_type type;

	dev_info(&client->dev, "%s(%s)\n", __func__, NU2115_DRV_VERSION);

	ret = nu2115_check_devinfo(client, &chip_rev, &type);
	if (ret < 0)
		return ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = &client->dev;
	chip->client = client;
	chip->revision = chip_rev;
	chip->type = type;
	mutex_init(&chip->io_lock);
	mutex_init(&chip->adc_lock);
	mutex_init(&chip->stat_lock);
	mutex_init(&chip->notify_lock);
	init_waitqueue_head(&chip->wq);
	i2c_set_clientdata(client, chip);

	ret = nu2115_parse_dt(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}

#ifdef CONFIG_RT_REGMAP
	ret = nu2115_register_regmap(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg regmap fail(%d)\n", __func__, ret);
		goto err;
	}
#endif /* CONFIG_RT_REGMAP */

	ret = __nu2115_init_chip(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_unreg_regmap;
	}

	ret = nu2115_register_chgdev(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg chgdev fail(%d)\n", __func__, ret);
		goto err_unreg_regmap;
	}

	chip->notify_task = kthread_run(nu2115_notify_task_threadfn, chip,
					"notify_thread");
	if (IS_ERR(chip->notify_task)) {
		dev_err(chip->dev, "%s run notify thread fail(%d)\n", __func__,
			ret);
		ret = PTR_ERR(chip->notify_task);
		goto err_unreg_chgdev;
	}

	ret = nu2115_init_irq(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_unreg_chgdev;
	}
	//+Peridot-35,xiaohongyu,wt,add,20240223,add charge ic message in factory mode
	hardwareinfo_set_prop(HARDWARE_CHARGER_IC_SLAVE_INFO, CHARGER_IC_SLAVE_NAME);
	//-Peridot-35,xiaohongyu,wt,add,20240223,add charge ic message in factory mode

	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;
err_unreg_chgdev:
	charger_device_unregister(chip->chg_dev);
err_unreg_regmap:
#ifdef CONFIG_RT_REGMAP
	rt_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_RT_REGMAP */
err:
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return ret;
}

static void nu2115_i2c_shutdown(struct i2c_client *client)
{
	struct nu2115_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (chip)
		nu2115_reset_register(chip);
}

static void nu2115_i2c_remove(struct i2c_client *client)
{
	struct nu2115_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (!chip)
		return;
	if (chip->notify_task)
		kthread_stop(chip->notify_task);
	charger_device_unregister(chip->chg_dev);
#ifdef CONFIG_RT_REGMAP
	rt_regmap_device_unregister(chip->rm_dev);
#endif /* CONFIG_RT_REGMAP */
	mutex_destroy(&chip->notify_lock);
	mutex_destroy(&chip->stat_lock);
	mutex_destroy(&chip->adc_lock);
	mutex_destroy(&chip->io_lock);
	return;
}

static int __maybe_unused nu2115_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct nu2115_chip *chip = i2c_get_clientdata(i2c);

	nu_dbg(dev, "\n");
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	disable_irq(chip->irq);
	return 0;
}

static int __maybe_unused nu2115_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct nu2115_chip *chip = i2c_get_clientdata(i2c);

	nu_dbg(dev, "\n");
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);
	enable_irq(chip->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(nu2115_pm_ops, nu2115_i2c_suspend, nu2115_i2c_resume);

static const struct of_device_id nu2115_of_id[] = {
	{ .compatible = "nuvolta,nu2115" },
	{},
};
MODULE_DEVICE_TABLE(of, nu2115_of_id);

static const struct i2c_device_id nu2115_i2c_id[] = {
	{ "nu2115", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, nu2115_i2c_id);

static struct i2c_driver nu2115_i2c_driver = {
	.driver = {
		.name = "nu2115",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nu2115_of_id),
		.pm = &nu2115_pm_ops,
	},
	.probe = nu2115_i2c_probe,
	.shutdown = nu2115_i2c_shutdown,
	.remove = nu2115_i2c_remove,
	.id_table = nu2115_i2c_id,
};
module_i2c_driver(nu2115_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Nuvolta NU2115 ChargePump Driver");
MODULE_AUTHOR("Allan ouyang<yangpingao@wingtech.com>");
MODULE_VERSION(NU2115_DRV_VERSION);
