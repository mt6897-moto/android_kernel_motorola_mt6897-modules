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
#define upm_dbg(dev, fmt, ...) \
	do { \
		if (dbg_log_en) \
			dev_info(dev, "%s: " fmt, __func__, ##__VA_ARGS__); \
	} while (0)

/* Information */
#define UPM6720_DRV_VERSION	"1.0.0_WT"
#define UPM6720_DEVID		0x00

/* Registers */
#define UPM6720_REG_VBATOVP	0x00
#define UPM6720_REG_VBATOVP_ALM	0x01
#define UPM6720_REG_IBATOCP	0x02
#define UPM6720_REG_IBATOCP_ALM	0x03
#define UPM6720_REG_CONFIG1	0x04//
#define UPM6720_REG_CTRL1	0x05
#define UPM6720_REG_VBUSOVP	0x06
#define UPM6720_REG_VBUSOVP_ALM	0x07
#define UPM6720_REG_IBUSOCP	0x08
#define UPM6720_REG_IBUSOCP_ALM	0x09

#define UPM6720_REG_TEMP_CTRL	0x0A
#define UPM6720_REG_TDIE_ALM	0x0B
#define UPM6720_REG_TSBUS_FLT	0x0C
#define UPM6720_REG_TSBAT_FLT	0x0D
#define UPM6720_REG_VAC_CTRL	0x0E

#define UPM6720_REG_CTRL2	0x0F
#define UPM6720_REG_CTRL3	0x10
#define UPM6720_REG_CTRL4	0x11
#define UPM6720_REG_CTRL5	0x12

#define UPM6720_REG_STAT1	0x13
#define UPM6720_REG_STAT2	0x14
#define UPM6720_REG_STAT3	0x15
#define UPM6720_REG_STAT4	0x16
#define UPM6720_REG_STAT5	0x17
#define UPM6720_REG_FLAG1	0x18
#define UPM6720_REG_FLAG2	0x19
#define UPM6720_REG_FLAG3	0x1A
#define UPM6720_REG_FLAG4	0x1B
#define UPM6720_REG_FLAG5	0x1C
#define UPM6720_REG_MASK1	0x1D
#define UPM6720_REG_MASK2	0x1E
#define UPM6720_REG_MASK3	0x1F
#define UPM6720_REG_MASK4	0x20
#define UPM6720_REG_MASK5	0x21

#define UPM6720_REG_PART_INFO	0x22
#define UPM6720_REG_ADC_CTRL	0x23
#define UPM6720_REG_ADC_FN_DISABLE	0x24

#define UPM6720_REG_IBUS_ADC1	0x25
#define UPM6720_REG_IBUS_ADC0	0x26
#define UPM6720_REG_VBUS_ADC1	0x27
#define UPM6720_REG_VBUS_ADC0	0x28
#define UPM6720_REG_VAC1_ADC1	0x29
#define UPM6720_REG_VAC1_ADC0	0x2A
#define UPM6720_REG_VAC2_ADC1	0x2B
#define UPM6720_REG_VAC2_ADC0	0x2C

#define UPM6720_REG_VOUT_ADC1	0x2D
#define UPM6720_REG_VOUT_ADC0	0x2E
#define UPM6720_REG_VBAT_ADC1	0x2F
#define UPM6720_REG_VBAT_ADC0	0x30
#define UPM6720_REG_IBAT_ADC1	0x31
#define UPM6720_REG_IBAT_ADC0	0x32

#define UPM6720_REG_TSBUS_ADC1	0x33
#define UPM6720_REG_TSBUS_ADC0	0x34
#define UPM6720_REG_TSBAT_ADC1	0x35
#define UPM6720_REG_TSBAT_ADC0	0x36
#define UPM6720_REG_TDIE_ADC1	0x37
#define UPM6720_REG_TDIE_ADC0	0x38

//#define UPM6720_REG_CTRL6	0x40
//#define UPM6720_REG_PMID2OUT_OVP_UVP	0x42
#define UPM6720_REG_MAX 0x38


/* Control bits */
#define UPM6720_ACDRV2_STAT_MASK	BIT(0)
#define UPM6720_ACDRV2_STAT_SHFT	0
#define UPM6720_ACDRV1_STAT_MASK	BIT(1)
#define UPM6720_ACDRV1_STAT_SHFT	1
#define UPM6720_DIS_ADCRV_BOTH_MASK	BIT(2)
#define UPM6720_DIS_ADCRV_BOTH_SHFT	2

#define UPM6720_CHGEN_MASK	BIT(4)
#define UPM6720_CHGEN_SHFT	4
#define UPM6720_OTGEN_MASK	BIT(5)
#define UPM6720_OTGEN_SHFT	5

#define UPM6720_BUS_PD_EN_MASK	BIT(7)
#define UPM6720_BUS_PD_EN_SHFT	7

#define UPM6720_VAC2_PD_EN_MASK	BIT(0)
#define UPM6720_VAC2_PD_EN_SHFT	0
#define UPM6720_VAC1_PD_EN_MASK	BIT(1)
#define UPM6720_VAC1_PD_EN_SHFT	1

#define UPM6720_CONVACTIVE_MASK	BIT(6)
#define UPM6720_CONVACTIVE_SHFT	6
#define UPM6720_ADCEN_MASK	BIT(7)
#define UPM6720_CHG_CONFIG_MASK	BIT(3)
#define UPM6720_WDTEN_MASK	BIT(2)
#define UPM6720_WDTMR_MASK	0x18
#define UPM6720_RST_MASK	BIT(7)
#define UPM6720_DEVREV_MASK	0xF0
#define UPM6720_DEVREV_SHFT	4
#define UPM6720_DEVID_MASK	0x0F
#define UPM6720_MS_MASK		0x03
#define UPM6720_MS_SHFT		0
#define UPM6720_VBUSOVP_MASK	0x7F
#define UPM6720_IBUSOCP_MASK	0x1F
#define UPM6720_VBATOVP_MASK	0x7F
#define UPM6720_IBATOCP_MASK	0x7F
#define UPM6720_VBATOVP_ALM_MASK	0x7F
#define UPM6720_VBATOVP_ALMDIS_MASK	BIT(7)
#define UPM6720_VBUSOVP_ALM_MASK	0x7F
#define UPM6720_VBUSOVP_ALMDIS_MASK	BIT(7)
#define UPM6720_VBUSUCP_FLAG_SHFT 5

//+Peridot-35,xiaohongyu,wt,add,20240223,add charge ic message in factory mode
#define CHARGER_IC_SLAVE_NAME "UPM6720"
//-Peridot-35,xiaohongyu,wt,add,20240223,add charge ic message in factory mode


enum upm6720_irqidx {
	UPM6720_IRQIDX_VBATOVP = 0,
	UPM6720_IRQIDX_VBATOVPALM,
	UPM6720_IRQIDX_VOUTOVP,
	UPM6720_IRQIDX_IBATOCP,
	UPM6720_IRQIDX_IBATOCPALM,
	UPM6720_IRQIDX_IBATUCPALM,
	UPM6720_IRQIDX_VBUSOVP,
	UPM6720_IRQIDX_VBUSOVPALM,

	UPM6720_IRQIDX_IBUSOCP,
	UPM6720_IRQIDX_IBUSOCPALM,
	UPM6720_IRQIDX_IBUSUCPF,
	UPM6720_IRQIDX_IBUSRCPF,
	UPM6720_IRQIDX_CFLYSHORT,

	UPM6720_IRQIDX_VACOVP1,
	UPM6720_IRQIDX_VACOVP2,
	UPM6720_IRQIDX_VOUTPRESENT,
	UPM6720_IRQIDX_VAC1INSERT,
	UPM6720_IRQIDX_VAC2INSERT,
	UPM6720_IRQIDX_VBUSPRESENT,
	UPM6720_IRQIDX_ACRB1CONFIG,
	UPM6720_IRQIDX_ACRB2CONFIG,

	UPM6720_IRQIDX_ADCDONE,
	UPM6720_IRQIDX_SSTIMEOUT,
	UPM6720_IRQIDX_TSBUSTSBAT,
	UPM6720_IRQIDX_TSBUSFLT,
	UPM6720_IRQIDX_TSBATFLT,
	UPM6720_IRQIDX_TSHUT,
	UPM6720_IRQIDX_TDIEALM,
	UPM6720_IRQIDX_WDTIMEOUT,

	UPM6720_IRQIDX_REGNGOOD,
	UPM6720_IRQIDX_CONVACTIVE,
	UPM6720_IRQIDX_VBUSERRORHI,

	UPM6720_IRQIDX_MAX,
};

enum upm6720_notify {
	UPM6720_NOTIFY_IBUSUCPF = 0,
	UPM6720_NOTIFY_VBUSOVPALM,
	UPM6720_NOTIFY_VBATOVPALM,
	UPM6720_NOTIFY_IBUSOCP,
	UPM6720_NOTIFY_VBUSOVP,
	UPM6720_NOTIFY_IBATOCP,
	UPM6720_NOTIFY_VBATOVP,
	UPM6720_NOTIFY_VOUTOVP,
	UPM6720_NOTIFY_VDROVP,
	UPM6720_NOTIFY_MAX,
};

enum upm6720_statflag_idx {
	UPM6720_SF_STAT1 = 0,
	UPM6720_SF_STAT2,
	UPM6720_SF_STAT3,
	UPM6720_SF_STAT4,
	UPM6720_SF_STAT5,
	UPM6720_SF_FLAG1,
	UPM6720_SF_FLAG2,
	UPM6720_SF_FLAG3,
	UPM6720_SF_FLAG4,
	UPM6720_SF_FLAG5,
	UPM6720_SF_MAX,
};

enum upm6720_type {
	UPM6720_TYPE_STANDALONE = 0,
	UPM6720_TYPE_SLAVE,
	UPM6720_TYPE_MASTER,
	UPM6720_TYPE_MAX,
};

static const char *upm6720_type_name[UPM6720_TYPE_MAX] = {
	"standalone", "slave", "master",
};

static const u32 upm6720_chgdev_notify_map[UPM6720_NOTIFY_MAX] = {
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

static const u8 upm6720_reg_sf[UPM6720_SF_MAX] = {
	UPM6720_REG_STAT1,
	UPM6720_REG_STAT2,
	UPM6720_REG_STAT3,
	UPM6720_REG_STAT4,
	UPM6720_REG_STAT5,
	UPM6720_REG_FLAG1,
	UPM6720_REG_FLAG2,
	UPM6720_REG_FLAG3,
	UPM6720_REG_FLAG4,
	UPM6720_REG_FLAG5,
};

struct upm6720_reg_defval {
	u8 reg;
	u8 value;
	u8 mask;
};

static const struct upm6720_reg_defval upm6720_init_chip_check_reg[] = {
	{
		.reg = UPM6720_REG_VBATOVP,
		.value = 0x5A,
		.mask = UPM6720_VBATOVP_MASK,
	},
	{
		.reg = UPM6720_REG_IBATOCP,
		.value = 0x47,
		.mask = UPM6720_IBATOCP_MASK,
	},
	{
		.reg = UPM6720_REG_CTRL3,
		.value = 0x83,
		.mask = UPM6720_WDTMR_MASK,
	},
};

struct upm6720_desc {
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

static const struct upm6720_desc upm6720_desc_defval = {
	.chg_name = "divider_charger",
	.rm_name = "upm6720",
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
	.pmid2out_uvp = 100, /* -100mV */
	.pmid2out_ovp = 300, /* 300mV */
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

struct upm6720_chip {
	struct device *dev;
	struct i2c_client *client;
	struct mutex io_lock;
	struct mutex adc_lock;
	struct mutex stat_lock;
	struct mutex notify_lock;
	struct charger_device *chg_dev;
	struct charger_properties chg_prop;
	struct upm6720_desc *desc;
	struct gpio_desc *irq_gpio;
	struct task_struct *notify_task;
	int irq;
	int notify;
	u8 revision;
	u32 flag;
	u32 stat;
	u32 hm_cnt;
	enum upm6720_type type;
	bool wdt_en;
	bool force_adc_en;
	bool stop_thread;
	wait_queue_head_t wq;

#ifdef CONFIG_RT_REGMAP
	struct rt_regmap_device *rm_dev;
	struct rt_regmap_properties *rm_prop;
#endif /* CONFIG_RT_REGMAP */
};

enum upm6720_adc_channel {
	UPM6720_ADC_IBUS = 0,
	UPM6720_ADC_VBUS,
	UPM6720_ADC_VAC1,
	UPM6720_ADC_VAC2,
	UPM6720_ADC_VOUT,
	UPM6720_ADC_VBAT,
	UPM6720_ADC_IBAT,
	UPM6720_ADC_TSBUS,
	UPM6720_ADC_TSBAT,
	UPM6720_ADC_TDIE,
	UPM6720_ADC_MAX,
	UPM6720_ADC_NOTSUPP = UPM6720_ADC_MAX,
};

static const u8 upm6720_adc_reg[UPM6720_ADC_MAX] = {
	UPM6720_REG_IBUS_ADC1,
	UPM6720_REG_VBUS_ADC1,
	UPM6720_REG_VAC1_ADC1,
	UPM6720_REG_VAC2_ADC1,
	UPM6720_REG_VOUT_ADC1,
	UPM6720_REG_VBAT_ADC1,
	UPM6720_REG_IBAT_ADC1,
	UPM6720_REG_TSBUS_ADC1,
	UPM6720_REG_TSBAT_ADC1,
	UPM6720_REG_TDIE_ADC1,
};

static const char *upm6720_adc_name[UPM6720_ADC_MAX] = {
	"Ibus", "Vbus", "VAC1", "VAC2", "Vout", "Vbat", "Ibat", "TSBus", "TSBat", "TDie",
};

static const u32 upm6720_adc_accuracy_tbl[UPM6720_ADC_MAX] = {
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

static int upm6720_read_device(void *client, u32 addr, int len, void *dst)
{
	struct i2c_client *i2c = (struct i2c_client *)client;

	return i2c_smbus_read_i2c_block_data(i2c, addr, len, dst);
}

static int upm6720_write_device(void *client, u32 addr, int len, const void *src)
{
	struct i2c_client *i2c = (struct i2c_client *)client;

	return i2c_smbus_write_i2c_block_data(i2c, addr, len, src);
}

#ifdef CONFIG_RT_REGMAP
RT_REG_DECL(UPM6720_REG_VBATOVP, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBATOVP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBATOCP, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBATOCP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CONFIG1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CTRL1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBUSOVP, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBUSOVP_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBUSOCP, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CONFIG3, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_TEMP_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TDIE_ALM, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TSBUS_FLT, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TSBAT_FLT, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VAC_CTRL, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_CTRL2, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CTRL3, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CTRL4, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_CTRL5, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_STAT1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_STAT2, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_STAT3, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_STAT4, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_STAT5, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_FLAG1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_FLAG2, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_FLAG3, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_FLAG4, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_FLAG5, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_MASK1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_MASK2, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_MASK3, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_MASK4, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_MASK5, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_PART_INFO, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_ADC_CTRL, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_ADC_FN_DISABLE, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_IBUS_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBUS_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VAC1_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VAC1_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VAC2_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VAC2_ADC0, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_VOUT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VOUT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBAT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_VBAT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBAT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_IBAT_ADC0, 1, RT_VOLATILE, {});

RT_REG_DECL(UPM6720_REG_TSBUS_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TSBUS_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TSBAT_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TSBAT_ADC0, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TDIE_ADC1, 1, RT_VOLATILE, {});
RT_REG_DECL(UPM6720_REG_TDIE_ADC0, 1, RT_VOLATILE, {});

//RT_REG_DECL(UPM6720_REG_CTRL6, 1, RT_VOLATILE, {});
//RT_REG_DECL(UPM6720_REG_PMID2OUT_OVP_UVP, 1, RT_VOLATILE, {});


static const rt_register_map_t upm6720_regmap[] = {
	RT_REG(UPM6720_REG_VBATOVP),
	RT_REG(UPM6720_REG_VBATOVP_ALM),
	RT_REG(UPM6720_REG_IBATOCP),
	RT_REG(UPM6720_REG_IBATOCP_ALM),
	RT_REG(UPM6720_REG_CONFIG1),
	RT_REG(UPM6720_REG_CTRL1),
	RT_REG(UPM6720_REG_VBUSOVP),
	RT_REG(UPM6720_REG_VBUSOVP_ALM),
	RT_REG(UPM6720_REG_IBUSOCP),
	RT_REG(UPM6720_REG_CONFIG3),

	RT_REG(UPM6720_REG_TEMP_CTRL),
	RT_REG(UPM6720_REG_TDIE_ALM),
	RT_REG(UPM6720_REG_TSBUS_FLT),
	RT_REG(UPM6720_REG_TSBAT_FLT),
	RT_REG(UPM6720_REG_VAC_CTRL),

	RT_REG(UPM6720_REG_CTRL2),
	RT_REG(UPM6720_REG_CTRL3),
	RT_REG(UPM6720_REG_CTRL4),
	RT_REG(UPM6720_REG_CTRL5),

	RT_REG(UPM6720_REG_STAT1),
	RT_REG(UPM6720_REG_STAT2),
	RT_REG(UPM6720_REG_STAT3),
	RT_REG(UPM6720_REG_STAT4),
	RT_REG(UPM6720_REG_STAT5),
	RT_REG(UPM6720_REG_FLAG1),
	RT_REG(UPM6720_REG_FLAG2),
	RT_REG(UPM6720_REG_FLAG3),
	RT_REG(UPM6720_REG_FLAG4),
	RT_REG(UPM6720_REG_FLAG5),
	RT_REG(UPM6720_REG_MASK1),
	RT_REG(UPM6720_REG_MASK2),
	RT_REG(UPM6720_REG_MASK3),
	RT_REG(UPM6720_REG_MASK4),
	RT_REG(UPM6720_REG_MASK5),

	RT_REG(UPM6720_REG_PART_INFO),
	RT_REG(UPM6720_REG_ADC_CTRL),
	RT_REG(UPM6720_REG_ADC_FN_DISABLE),

	RT_REG(UPM6720_REG_IBUS_ADC1),
	RT_REG(UPM6720_REG_IBUS_ADC0),
	RT_REG(UPM6720_REG_VBUS_ADC1),
	RT_REG(UPM6720_REG_VBUS_ADC0),
	RT_REG(UPM6720_REG_VAC1_ADC1),
	RT_REG(UPM6720_REG_VAC1_ADC0),
	RT_REG(UPM6720_REG_VAC2_ADC1),
	RT_REG(UPM6720_REG_VAC2_ADC0),

	RT_REG(UPM6720_REG_VOUT_ADC1),
	RT_REG(UPM6720_REG_VOUT_ADC0),
	RT_REG(UPM6720_REG_VBAT_ADC1),
	RT_REG(UPM6720_REG_VBAT_ADC0),
	RT_REG(UPM6720_REG_IBAT_ADC1),
	RT_REG(UPM6720_REG_IBAT_ADC0),

	RT_REG(UPM6720_REG_TSBUS_ADC1),
	RT_REG(UPM6720_REG_TSBUS_ADC0),
	RT_REG(UPM6720_REG_TSBAT_ADC1),
	RT_REG(UPM6720_REG_TSBAT_ADC0),
	RT_REG(UPM6720_REG_TDIE_ADC1),
	RT_REG(UPM6720_REG_TDIE_ADC0),

	//RT_REG(UPM6720_REG_CTRL6),
	//RT_REG(UPM6720_REG_PMID2OUT_OVP_UVP),
};

static struct rt_regmap_fops upm6720_rm_fops = {
	.read_device = upm6720_read_device,
	.write_device = upm6720_write_device,
};

static int upm6720_register_regmap(struct upm6720_chip *chip)
{
	struct i2c_client *client = chip->client;
	struct rt_regmap_properties *prop = NULL;

	upm_dbg(chip->dev, "\n");

	prop = devm_kzalloc(&client->dev, sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = chip->desc->rm_name;
	prop->aliases = chip->desc->rm_name;
	prop->register_num = ARRAY_SIZE(upm6720_regmap);
	prop->rm = upm6720_regmap;
	prop->rt_regmap_mode = RT_SINGLE_BYTE | RT_CACHE_DISABLE |
			       RT_IO_PASS_THROUGH;
	prop->io_log_en = 0;

	chip->rm_prop = prop;
	chip->rm_dev = rt_regmap_device_register_ex(chip->rm_prop,
						    &upm6720_rm_fops, chip->dev,
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
static inline int __upm6720_i2c_write8(struct upm6720_chip *chip, u8 reg, u8 data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_write(chip->rm_dev, reg, 1, &data);
#else
		ret = upm6720_write_device(chip->client, reg, 1, &data);
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
	upm_dbg(chip->dev, "I2CW[0x%02X] = 0x%02X\n", reg, data);
	return 0;
}

static inline int __upm6720_i2c_read8(struct upm6720_chip *chip, u8 reg, u8 *data)
{
	int ret, retry = 0;

	do {
#ifdef CONFIG_RT_REGMAP
		ret = rt_regmap_block_read(chip->rm_dev, reg, 1, data);
#else
		ret = upm6720_read_device(chip->client, reg, 1, data);
#endif /* CONFIG_RT_REGMAP */
		retry++;
		if (ret < 0)
			usleep_range(10, 15);
	} while (ret < 0 && retry < I2C_ACCESS_MAX_RETRY);

	if (ret < 0) {
		dev_err(chip->dev, "%s I2CR[0x%02X] fail\n", __func__, reg);
		return ret;
	}
	upm_dbg(chip->dev, "I2CR[0x%02X] = 0x%02X\n", reg, *data);
	return 0;
}

static int upm6720_i2c_read8(struct upm6720_chip *chip, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __upm6720_i2c_read8(chip, reg, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static inline int __upm6720_i2c_write_block(struct upm6720_chip *chip, u8 reg,
					   u32 len, const u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_write(chip->rm_dev, reg, len, data);
#else
	ret = upm6720_write_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

static inline int __upm6720_i2c_read_block(struct upm6720_chip *chip, u8 reg,
					  u32 len, u8 *data)
{
	int ret;

#ifdef CONFIG_RT_REGMAP
	ret = rt_regmap_block_read(chip->rm_dev, reg, len, data);
#else
	ret = upm6720_read_device(chip->client, reg, len, data);
#endif /* CONFIG_RT_REGMAP */

	return ret;
}

static int upm6720_i2c_read_block(struct upm6720_chip *chip, u8 reg, u32 len,
				 u8 *data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = __upm6720_i2c_read_block(chip, reg, len, data);
	mutex_unlock(&chip->io_lock);

	return ret;
}

static int upm6720_i2c_test_bit(struct upm6720_chip *chip, u8 reg, u8 shft,
			       bool *one)
{
	int ret;
	u8 data;

	ret = upm6720_i2c_read8(chip, reg, &data);
	if (ret < 0) {
		*one = false;
		return ret;
	}
	*one = (data & BIT(shft)) ? true : false;
	return 0;
}

static int upm6720_i2c_update_bits(struct upm6720_chip *chip, u8 reg, u8 data,
				  u8 mask)
{
	int ret;
	u8 _data;

	mutex_lock(&chip->io_lock);
	ret = __upm6720_i2c_read8(chip, reg, &_data);
	if (ret < 0)
		goto out;
	_data &= ~mask;
	_data |= (data & mask);
	ret = __upm6720_i2c_write8(chip, reg, _data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static inline int upm6720_set_bits(struct upm6720_chip *chip, u8 reg, u8 mask)
{
	return upm6720_i2c_update_bits(chip, reg, mask, mask);
}

static inline int upm6720_clr_bits(struct upm6720_chip *chip, u8 reg, u8 mask)
{
	return upm6720_i2c_update_bits(chip, reg, 0x00, mask);
}

static inline u8 upm6720_val_toreg(u32 min, u32 max, u32 step, u32 target,
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

static inline u8 upm6720_val_toreg_via_tbl(const u32 *tbl, int tbl_size,
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

static u8 upm6720_vbatovp_toreg(u32 uV)
{
	return upm6720_val_toreg(3491000, 4759000, 9985, uV, false);
}

static u8 upm6720_vbatovp_alm_toreg(u32 uV)
{
	return upm6720_val_toreg(3500000, 4770000, 10000, uV, false);
}

static u8 upm6720_ibatocp_toreg(u32 uA)
{
	return upm6720_val_toreg(2000000, 8800000, 100000, uA, true);
}

static u8 upm6720_ibatocp_alm_toreg(u32 uA)
{
	return upm6720_val_toreg(0, 12700000, 100000, uA, true);
}

/*
static u8 upm6720_ibatucp_toreg(u32 uA)
{
	return upm6720_val_toreg(0, 6350000, 50000, uA, false);
}
*/

static u8 upm6720_vbusovp_toreg(u32 uV)
{
	return upm6720_val_toreg(7000000, 12750000, 50000, uV, false);
}

static u8 upm6720_vbusovp_alm_toreg(u32 uV)
{
	return upm6720_val_toreg(7000000, 13350000, 50000, uV, false);
}

static u8 upm6720_ibusocp_toreg(u32 uA)
{
	return upm6720_val_toreg(1075000, 4575000, 250000, uA, true);
}

static u8 upm6720_ibusocp_alm_toreg(u32 uA)
{
	return upm6720_val_toreg(1000000, 8750000, 250000, uA, true);
}

static u8 upm6720_tdie_alm_toreg(u32 temp)
{
	return upm6720_val_toreg(250, 1500, 5, temp, true);
}

/*
static u8 upm6720_pmid2out_uvp_toreg(u32 mV)
{
	return upm6720_val_toreg(50, 225, 25, mV, false);
}

static u8 upm6720_pmid2out_ovp_toreg(u32 mV)
{
	return upm6720_val_toreg(150, 500, 50, mV, false);
}
*/
static u8 upm6720_tsbat_flt_toreg(u32 percent)
{
	return upm6720_val_toreg(0, 498041, 1953, percent, true);
}

static const u32 upm6720_wdt[] = {
	500000, 1000000, 5000000, 30000000,
};

static u8 upm6720_wdt_toreg(u32 uS)
{
	return upm6720_val_toreg_via_tbl(upm6720_wdt, ARRAY_SIZE(upm6720_wdt), uS);
}

static const u32 upm6720_vacovp[] = {
	6500000, 10500000, 12000000, 14000000, 16000000, 18000000,
};

static u8 upm6720_vacovp_toreg(u32 uV)
{
	return upm6720_val_toreg_via_tbl(upm6720_vacovp, ARRAY_SIZE(upm6720_vacovp), uV);
}

static const u32 upm6720_fsw_set[] = {
	187, 250, 300, 375, 500, 750,
};

static u8 upm6720_fsw_set_toreg(u32 khZ)
{
	return upm6720_val_toreg_via_tbl(upm6720_fsw_set, ARRAY_SIZE(upm6720_fsw_set), khZ);
}

static const u32 upm6720_ss_timeout[] = {
	6250, 12500, 25000, 50000, 100000, 400000, 1500000, 10000000,
};

static u8 upm6720_ss_timeout_toreg(u32 uS)
{
	return upm6720_val_toreg_via_tbl(upm6720_ss_timeout,
								ARRAY_SIZE(upm6720_ss_timeout), uS);
}

static const u32 upm6720_ibusucpf_dg[] = {
	10, 5000, 50000, 150000,
};

static u8 upm6720_ibusucpf_deglitch_toreg(u32 uS)
{
	return upm6720_val_toreg_via_tbl(upm6720_ibusucpf_dg,
								ARRAY_SIZE(upm6720_ibusucpf_dg), uS);
}

static const u32 upm6720_voutovp[] = {
	4700, 4800, 4900, 5000,
};

static u8 upm6720_voutovp_toreg(u32 mV)
{
	return upm6720_val_toreg_via_tbl(upm6720_voutovp,
								ARRAY_SIZE(upm6720_voutovp), mV);
}

static int __upm6720_update_status(struct upm6720_chip *chip);
static int __upm6720_init_chip(struct upm6720_chip *chip);

/* Must be called while holding a lock */
static int upm6720_enable_wdt(struct upm6720_chip *chip, bool en)
{
	int ret;

	if (chip->wdt_en == en)
		return 0;
	ret = (en ? upm6720_clr_bits : upm6720_set_bits)
		(chip, UPM6720_REG_CTRL3, UPM6720_WDTEN_MASK);
	if (ret < 0)
		return ret;
	chip->wdt_en = en;
	return 0;
}

static const int upm6720_adc_m[] = 
    {9972, 1002, 1000, 1000, 1003, 1017, 999, 986, 976, 5079};

static const int upm6720_adc_l[] = 
    {10, 1, 1, 1, 1, 1, 1, 10000, 10000, 10000};

static int __upm6720_get_adc(struct upm6720_chip *chip,
			    enum upm6720_adc_channel chan, int *val)
{
	int ret;
	u8 data[2];

	ret = upm6720_set_bits(chip, UPM6720_REG_ADC_CTRL, UPM6720_ADCEN_MASK);
	if (ret < 0)
		goto out;

	usleep_range(60000, 70000);
	ret = upm6720_i2c_read_block(chip, upm6720_adc_reg[chan], 2, data);
	if (ret < 0)
		goto out_dis;
	switch (chan) {
	case UPM6720_ADC_IBUS:
	case UPM6720_ADC_IBAT:
		//+liyiying, mod
		/*
		*val = (data[1] | (data[0] << 8)) *
                upm6720_adc_m[chan] / upm6720_adc_l[chan] * 1000;
		*/
		*val = (data[1] | (data[0] << 8)) *
                upm6720_adc_m[chan] / upm6720_adc_l[chan];
		//-liyiying, mod
		break;
	case UPM6720_ADC_VBUS:
	case UPM6720_ADC_VAC1:
	case UPM6720_ADC_VAC2:
	case UPM6720_ADC_VOUT:
	case UPM6720_ADC_VBAT:
		*val = (data[1] | (data[0] << 8)) *
                upm6720_adc_m[chan] / upm6720_adc_l[chan];
		break;
	case UPM6720_ADC_TSBUS:
	case UPM6720_ADC_TSBAT:
	case UPM6720_ADC_TDIE:
		*val = (data[1] | (data[0] << 8)) *
                upm6720_adc_m[chan] / upm6720_adc_l[chan];
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret < 0)
		dev_err(chip->dev, "%s %s fail(%d)\n", __func__,
			upm6720_adc_name[chan], ret);
	else
		upm_dbg(chip->dev, "%s %d\n",
			 upm6720_adc_name[chan], *val);
out_dis:
	if (!chip->force_adc_en)
		ret = upm6720_clr_bits(chip, UPM6720_REG_ADC_CTRL,
				      UPM6720_ADCEN_MASK);
out:
	return ret;
}

static int upm6720_enable_chg(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u32 err_check = BIT(UPM6720_IRQIDX_VBUSOVP) |
			BIT(UPM6720_IRQIDX_VACOVP1) |
			BIT(UPM6720_IRQIDX_VACOVP2) |
			BIT(UPM6720_IRQIDX_VBUSERRORHI) |
			BIT(UPM6720_IRQIDX_VOUTOVP);
	u32 stat_check = BIT(UPM6720_IRQIDX_VAC1INSERT) |
			 BIT(UPM6720_IRQIDX_VOUTPRESENT);

	upm_dbg(chip->dev, "%d\n", en);
	mutex_lock(&chip->adc_lock);
	chip->force_adc_en = en;
	if (!en) {
		ret = upm6720_clr_bits(chip, UPM6720_REG_CTRL2,
				      UPM6720_CHGEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = upm6720_clr_bits(chip, UPM6720_REG_ADC_CTRL,
				      UPM6720_ADCEN_MASK);
		if (ret < 0)
			goto out_unlock;
		ret = upm6720_enable_wdt(chip, false);
		goto out_unlock;
	}
	/* Enable ADC to check status before enable charging */
	ret = upm6720_set_bits(chip, UPM6720_REG_ADC_CTRL, UPM6720_ADCEN_MASK);
	if (ret < 0)
		goto out_unlock;
	mutex_unlock(&chip->adc_lock);
	usleep_range(60000, 70000);

	mutex_lock(&chip->stat_lock);
	__upm6720_update_status(chip);
	if ((chip->stat & err_check) ||
	    ((chip->stat & stat_check) != stat_check)) {
		upm_dbg(chip->dev, "error(0x%08X,0x%08X,0x%08X)\n",
			chip->stat, err_check, stat_check);
		ret = -EINVAL;
		mutex_unlock(&chip->stat_lock);
		goto out;
	}
	mutex_unlock(&chip->stat_lock);
	if (!chip->desc->wd_timeout_dis) {
		ret = upm6720_enable_wdt(chip, true);
		if (ret < 0)
			goto out;
	}

	//Enable Chg_config Bit
	ret = upm6720_set_bits(chip, UPM6720_REG_CTRL1, UPM6720_CHG_CONFIG_MASK);
	if (ret < 0)
		goto out_unlock;

	usleep_range(1000, 2000);
	ret = upm6720_set_bits(chip, UPM6720_REG_CTRL2, UPM6720_CHGEN_MASK);
	goto out;
out_unlock:
	mutex_unlock(&chip->adc_lock);
out:
	return ret;
}

static int upm6720_is_chg_enabled(struct charger_device *chg_dev, bool *en)
{
	int ret;
	bool chg_en;
	bool conv_active;
	struct upm6720_chip *chip = charger_get_data(chg_dev);

	//check charge chg_en bit
	ret = upm6720_i2c_test_bit(chip, UPM6720_REG_CTRL2, UPM6720_CHGEN_SHFT,
				  &chg_en);
	if (ret < 0)
		return ret;

	//check charge Converter Active Status
	ret = upm6720_i2c_test_bit(chip, UPM6720_REG_STAT5, UPM6720_CONVACTIVE_SHFT,
				  &conv_active);
	if (ret < 0)
		return ret;

	*en = chg_en & conv_active;
	upm_dbg(chip->dev, "%d\n",  *en);
	return 0;
}

static inline enum upm6720_adc_channel to_upm6720_adc(enum adc_channel chan)
{
	switch (chan) {
	case ADC_CHANNEL_VBUS:
		return UPM6720_ADC_VBUS;
	case ADC_CHANNEL_VBAT:
		return UPM6720_ADC_VBAT;
	case ADC_CHANNEL_IBUS:
		return UPM6720_ADC_IBUS;
	case ADC_CHANNEL_IBAT:
		return UPM6720_ADC_IBAT;
	case ADC_CHANNEL_TEMP_JC:
		return UPM6720_ADC_TDIE;
	case ADC_CHANNEL_VOUT:
		return UPM6720_ADC_VOUT;
	default:
		break;
	}
	return UPM6720_ADC_NOTSUPP;
}

static int upm6720_get_adc(struct charger_device *chg_dev, enum adc_channel chan,
			  int *min, int *max)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	enum upm6720_adc_channel _chan = to_upm6720_adc(chan);

	if (_chan == UPM6720_ADC_NOTSUPP)
		return -EINVAL;
	mutex_lock(&chip->adc_lock);
	ret = __upm6720_get_adc(chip, _chan, max);
	if (ret < 0)
		goto out;
	if (min != max)
		*min = *max;
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int upm6720_get_adc_accuracy(struct charger_device *chg_dev,
				   enum adc_channel chan, int *min, int *max)
{
	enum upm6720_adc_channel _chan = to_upm6720_adc(chan);

	if (_chan == UPM6720_ADC_NOTSUPP)
		return -EINVAL;
	*min = *max = upm6720_adc_accuracy_tbl[_chan];
	return 0;
}

static int upm6720_set_vbusovp(struct charger_device *chg_dev, u32 uV)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_vbusovp_toreg(uV);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_VBUSOVP, reg,
				      UPM6720_VBUSOVP_MASK);
}

static int upm6720_set_ibusocp(struct charger_device *chg_dev, u32 uA)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_ibusocp_toreg(uA);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uA, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_IBUSOCP, reg,
				      UPM6720_IBUSOCP_MASK);
}

static int upm6720_set_vbatovp(struct charger_device *chg_dev, u32 uV)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_vbatovp_toreg(uV);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_VBATOVP, reg,
				      UPM6720_VBATOVP_MASK);
}

static int upm6720_set_vbatovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_vbatovp_alm_toreg(uV);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_VBATOVP_ALM, reg,
				      UPM6720_VBATOVP_ALM_MASK);
}

static int upm6720_reset_vbatovp_alarm(struct charger_device *chg_dev)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 data;

	upm_dbg(chip->dev, "\n");
	mutex_lock(&chip->io_lock);
	ret = __upm6720_i2c_read8(chip, UPM6720_REG_VBATOVP_ALM, &data);
	if (ret < 0)
		goto out;
	data |= UPM6720_VBATOVP_ALMDIS_MASK;
	ret = __upm6720_i2c_write8(chip, UPM6720_REG_VBATOVP_ALM, data);
	if (ret < 0)
		goto out;
	data &= ~UPM6720_VBATOVP_ALMDIS_MASK;
	ret = __upm6720_i2c_write8(chip, UPM6720_REG_VBATOVP_ALM, data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int upm6720_set_vbusovp_alarm(struct charger_device *chg_dev, u32 uV)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_vbusovp_alm_toreg(uV);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uV, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_VBUSOVP_ALM, reg,
				      UPM6720_VBUSOVP_ALM_MASK);
}

static int upm6720_is_vbuslowerr(struct charger_device *chg_dev, bool *err)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);

	mutex_lock(&chip->adc_lock);
	ret = upm6720_set_bits(chip, UPM6720_REG_ADC_CTRL, UPM6720_ADCEN_MASK);
	if (ret < 0)
		goto out;
	usleep_range(60000, 70000);
	ret = upm6720_i2c_test_bit(chip, UPM6720_REG_STAT2,
				  UPM6720_VBUSUCP_FLAG_SHFT, err);

	if (!chip->force_adc_en)
		upm6720_clr_bits(chip, UPM6720_REG_ADC_CTRL, UPM6720_ADCEN_MASK);
out:
	mutex_unlock(&chip->adc_lock);
	return ret;
}

static int upm6720_reset_vbusovp_alarm(struct charger_device *chg_dev)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 data;

	upm_dbg(chip->dev, "\n");
	mutex_lock(&chip->io_lock);
	ret = __upm6720_i2c_read8(chip, UPM6720_REG_VBUSOVP_ALM, &data);
	if (ret < 0)
		goto out;
	data |= UPM6720_VBUSOVP_ALMDIS_MASK;
	ret = __upm6720_i2c_write8(chip, UPM6720_REG_VBUSOVP_ALM, data);
	if (ret < 0)
		goto out;
	data &= ~UPM6720_VBUSOVP_ALMDIS_MASK;
	ret = __upm6720_i2c_write8(chip, UPM6720_REG_VBUSOVP_ALM, data);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int upm6720_set_ibatocp(struct charger_device *chg_dev, u32 uA)
{
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	u8 reg = upm6720_ibatocp_toreg(uA);

	upm_dbg(chip->dev, "%d(0x%02X)\n", uA, reg);
	return upm6720_i2c_update_bits(chip, UPM6720_REG_IBATOCP, reg,
				      UPM6720_IBATOCP_MASK);
}

static int upm6720_init_chip(struct charger_device *chg_dev)
{
	int i, ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);
	const struct upm6720_reg_defval *reg_defval;
	u8 val;

	for (i = 0; i < ARRAY_SIZE(upm6720_init_chip_check_reg); i++) {
		reg_defval = &upm6720_init_chip_check_reg[i];
		ret = upm6720_i2c_read8(chip, reg_defval->reg, &val);
		if (ret < 0)
			return ret;
		if ((val & reg_defval->mask) == reg_defval->value) {
			upm_dbg(chip->dev, "chip reset happened, reinit\n");
			return __upm6720_init_chip(chip);
		}
	}
	return 0;
}

static int upm6720_dump_reg(struct upm6720_chip *chip)
{
	int i, ret;
	u8 val;

	for (i = 0; i <= UPM6720_REG_MAX; i++) {
		ret = upm6720_i2c_read8(chip, i, &val);
		dev_info(chip->dev, "upm6720_reg[0x%02x] = 0x%02x\n",  i, val);
	}

    return ret;
}

static int upm6720_typec_mux_otg_enable(struct charger_device *chg_dev, bool en)
{
	int ret;
	struct upm6720_chip *chip = charger_get_data(chg_dev);

	dev_info(chip->dev, "%s: %d\n", __func__, en);

	if (en) {
		//1-- enable OTG mode: EN_OTG = 1
		ret = upm6720_set_bits(chip, UPM6720_REG_CTRL2, UPM6720_OTGEN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable otg mode fail !\n");
			return ret;
		}

		//2-- enable ACDRVS: DIS_ACDRV_BOTH = 0
		ret = upm6720_clr_bits(chip, UPM6720_REG_CTRL2, UPM6720_DIS_ADCRV_BOTH_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable acdrv_both fail !\n");
			return ret;
		}

		//3-- enable ACDRV1: ACDRV1_STAT = 1
		ret = upm6720_set_bits(chip, UPM6720_REG_CTRL2, UPM6720_ACDRV1_STAT_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable acdrv1_stat fail !\n");
			return ret;
		}
	} else {
		//1-- enable BUS_PD: BUS_PD_EN = 1
		ret = upm6720_set_bits(chip, UPM6720_REG_VBUSOVP, UPM6720_BUS_PD_EN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable bus_pd_en fail !\n");
			return ret;
		}

		//2-- enable VAC1_PD: VAC1_PD_EN = 1
		ret = upm6720_set_bits(chip, UPM6720_REG_VAC_CTRL, UPM6720_VAC1_PD_EN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable vac1_pd_en fail !\n");
			return ret;
		}

		//3-- disable ACDRV1: ACDRV1_STAT = 0
		ret = upm6720_clr_bits(chip, UPM6720_REG_CTRL2, UPM6720_ACDRV1_STAT_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "enable acdrv1_stat fail !\n");
			return ret;
		}

		//4-- disable OTG mode: ENOTG = 0
		ret = upm6720_clr_bits(chip, UPM6720_REG_CTRL2, UPM6720_OTGEN_MASK);
		if (ret < 0) {
			dev_err(chip->dev, "disable otg mode fail !\n");
			return ret;
		}
	}

	return 0;
}

static inline void upm6720_set_notify(struct upm6720_chip *chip,
				     enum upm6720_notify notify)
{
	mutex_lock(&chip->notify_lock);
	chip->notify |= BIT(notify);
	mutex_unlock(&chip->notify_lock);
}

//UPM6720_SF_FLAG1 IRQ
 static int upm6720_vbatovp_irq_handler(struct upm6720_chip *chip)
 {
	 dev_info(chip->dev, "%s\n", __func__);
	 upm6720_set_notify(chip, UPM6720_NOTIFY_VBATOVP);
	 return 0;
 }

static int upm6720_vbatovpalm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_VBATOVPALM);
	return 0;
}

static int upm6720_voutovp_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_VOUTOVP);
	return 0;
}

static int upm6720_ibatocp_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_IBATOCP);
	return 0;
}

static int upm6720_ibatocpalm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_ibatucpalm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_vbusovp_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_VBUSOVP);
	return 0;
}

static int upm6720_vbusovpalm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_VBUSOVPALM);
	return 0;
}

//UPM6720_SF_FLAG2 IRQ
static int upm6720_ibusocp_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_IBUSOCP);
	return 0;
}

static int upm6720_ibusocpalm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_ibusucpf_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	upm6720_set_notify(chip, UPM6720_NOTIFY_IBUSUCPF);
	return 0;
}

static int upm6720_ibusrcpf_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//UPM6720_SF_FLAG3 IRQ
static int upm6720_vacovp1_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_vacovp2_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_voutpresent_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_vac1insert_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_vac2insert_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_vbuspresent_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_acrb1config_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_acrb2config_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}


//UPM6720_SF_FLAG4 IRQ
static int upm6720_adcdone_irq_handler(struct upm6720_chip *chip)
{
	upm_dbg(chip->dev, "\n");
	return 0;
}

static int upm6720_sstimeout_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_tsbustsbat_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_tsbusflt_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_tsbatflt_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_tshut_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_tdiealm_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int upm6720_wdtimeout_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

//UPM6720_SF_FLAG5 IRQ
static int upm6720_vbuserrorhi_irq_handler(struct upm6720_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}


struct irq_map_desc {
	const char *name;
	int (*hdlr)(struct upm6720_chip *chip);
	u8 flag_idx;
	u8 stat_idx;
	u8 flag_mask;
	u8 stat_mask;
	u32 irq_idx;
	bool stat_only;
};

#define UPM6720_IRQ_DESC(_name, _flag_i, _stat_i, _flag_s, _stat_s, _irq_idx, \
			_stat_only) \
	{.name = #_name, .hdlr = upm6720_##_name##_irq_handler, \
	 .flag_idx = _flag_i, .stat_idx = _stat_i, \
	 .flag_mask = (1 << _flag_s), .stat_mask = (1 << _stat_s), \
	 .irq_idx = _irq_idx, .stat_only = _stat_only}


static const struct irq_map_desc upm6720_irq_map_tbl[UPM6720_IRQIDX_MAX] = {		
	UPM6720_IRQ_DESC(vbatovp, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 7, 7,
			    UPM6720_IRQIDX_VBATOVP, false),
	UPM6720_IRQ_DESC(vbatovpalm, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 6, 6,
			    UPM6720_IRQIDX_VBATOVPALM, false),
	UPM6720_IRQ_DESC(voutovp, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 5, 5,
			    UPM6720_IRQIDX_VOUTOVP, false),
	UPM6720_IRQ_DESC(ibatocp, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 4, 4,
			    UPM6720_IRQIDX_IBATOCP, false),
	UPM6720_IRQ_DESC(ibatocpalm, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 3, 3,
			    UPM6720_IRQIDX_IBATOCPALM, false),
	UPM6720_IRQ_DESC(ibatucpalm, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 2, 2,
			    UPM6720_IRQIDX_IBATUCPALM, false),
	UPM6720_IRQ_DESC(vbusovp, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 1, 1,
			    UPM6720_IRQIDX_VBUSOVP, false),
	UPM6720_IRQ_DESC(vbusovpalm, UPM6720_SF_FLAG1, UPM6720_SF_STAT1, 0, 0,
			    UPM6720_IRQIDX_VBUSOVPALM, false),

	UPM6720_IRQ_DESC(ibusocp, UPM6720_SF_FLAG2, UPM6720_SF_STAT2, 7, 7,
			    UPM6720_IRQIDX_IBUSOCP, false),
	UPM6720_IRQ_DESC(ibusocpalm, UPM6720_SF_FLAG2, UPM6720_SF_STAT2, 6, 6,
			    UPM6720_IRQIDX_IBUSOCPALM, false),
	UPM6720_IRQ_DESC(ibusucpf, UPM6720_SF_FLAG2, UPM6720_SF_STAT2, 5, 5,
				UPM6720_IRQIDX_IBUSUCPF, false),
	UPM6720_IRQ_DESC(ibusrcpf, UPM6720_SF_FLAG2, UPM6720_SF_STAT2, 4, 4,
			    UPM6720_IRQIDX_IBUSRCPF, false),

	UPM6720_IRQ_DESC(vacovp1, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 7, 7,
			    UPM6720_IRQIDX_VACOVP1, false),
	UPM6720_IRQ_DESC(vacovp2, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 6, 6,
			    UPM6720_IRQIDX_VACOVP2, false),
	UPM6720_IRQ_DESC(voutpresent, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 5, 5,
			    UPM6720_IRQIDX_VOUTPRESENT, false),
	UPM6720_IRQ_DESC(vac1insert, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 4, 4,
			    UPM6720_IRQIDX_VAC1INSERT, false),
	UPM6720_IRQ_DESC(vac2insert, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 3, 3,
			    UPM6720_IRQIDX_VAC2INSERT, false),    
	UPM6720_IRQ_DESC(vbuspresent, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 2, 2,
			    UPM6720_IRQIDX_VBUSPRESENT, false),
	UPM6720_IRQ_DESC(acrb1config, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 1, 1,
			    UPM6720_IRQIDX_ACRB1CONFIG, false),
	UPM6720_IRQ_DESC(acrb2config, UPM6720_SF_FLAG3, UPM6720_SF_STAT3, 0, 0,
			    UPM6720_IRQIDX_ACRB2CONFIG, false),
	
	UPM6720_IRQ_DESC(adcdone, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 7, 7,
			    UPM6720_IRQIDX_ADCDONE, false),
	UPM6720_IRQ_DESC(sstimeout, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 6, 6,
			    UPM6720_IRQIDX_SSTIMEOUT, false),
	UPM6720_IRQ_DESC(tsbustsbat, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 5, 5,
			    UPM6720_IRQIDX_TSBUSTSBAT, false),
	UPM6720_IRQ_DESC(tsbusflt, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 4, 4,
			    UPM6720_IRQIDX_TSBUSFLT, false),
	UPM6720_IRQ_DESC(tsbatflt, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 3, 3,
			    UPM6720_IRQIDX_TSBATFLT, false),
	UPM6720_IRQ_DESC(tshut, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 2, 2,
			    UPM6720_IRQIDX_TSHUT, false),
	UPM6720_IRQ_DESC(tdiealm, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 1, 1,
			    UPM6720_IRQIDX_TDIEALM, false),
	UPM6720_IRQ_DESC(wdtimeout, UPM6720_SF_FLAG4, UPM6720_SF_STAT4, 0, 0,
			    UPM6720_IRQIDX_WDTIMEOUT, false),

	UPM6720_IRQ_DESC(vbuserrorhi, UPM6720_SF_FLAG5, UPM6720_SF_STAT5, 4, 4,
			    UPM6720_IRQIDX_VBUSERRORHI, false),
};

static int __upm6720_update_status(struct upm6720_chip *chip)
{
	int i;
	u8 sf[UPM6720_SF_MAX] = {0};
	const struct irq_map_desc *desc;

	for (i = 0; i < UPM6720_SF_MAX; i++)
		upm6720_i2c_read8(chip, upm6720_reg_sf[i], &sf[i]);

	for (i = 0; i < ARRAY_SIZE(upm6720_irq_map_tbl); i++) {
		desc = &upm6720_irq_map_tbl[i];
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

static int upm6720_notify_task_threadfn(void *data)
{
	int i;
	struct upm6720_chip *chip = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(chip->wq, chip->notify != 0 ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto out;
		pm_stay_awake(chip->dev);
		mutex_lock(&chip->notify_lock);
		upm6720_dump_reg(chip);
		for (i = 0; i < UPM6720_NOTIFY_MAX; i++) {
			if (chip->notify & BIT(i)) {
				chip->notify &= ~BIT(i);
				mutex_unlock(&chip->notify_lock);
				charger_dev_notify(chip->chg_dev,
						   upm6720_chgdev_notify_map[i]);
				mutex_lock(&chip->notify_lock);
			}
		}
		mutex_unlock(&chip->notify_lock);
		pm_relax(chip->dev);
	}
out:
	return 0;
}

static irqreturn_t upm6720_irq_handler(int irq, void *data)
{
	int i;
	struct upm6720_chip *chip = data;
	const struct irq_map_desc *desc;

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->stat_lock);
	__upm6720_update_status(chip);
	for (i = 0; i < ARRAY_SIZE(upm6720_irq_map_tbl); i++) {
		desc = &upm6720_irq_map_tbl[i];
		if ((chip->flag & (1 << desc->irq_idx)) && desc->hdlr)
			desc->hdlr(chip);
	}
	chip->flag = 0;
	wake_up_interruptible(&chip->wq);
	mutex_unlock(&chip->stat_lock);
	pm_relax(chip->dev);
	return IRQ_HANDLED;
}

static const struct charger_ops upm6720_chg_ops = {
	.enable = upm6720_enable_chg,
	.is_enabled = upm6720_is_chg_enabled,
	.get_adc = upm6720_get_adc,
	.set_vbusovp = upm6720_set_vbusovp,
	.set_ibusocp = upm6720_set_ibusocp,
	.set_vbatovp = upm6720_set_vbatovp,
	.set_ibatocp = upm6720_set_ibatocp,
	.init_chip = upm6720_init_chip,
	.set_vbatovp_alarm = upm6720_set_vbatovp_alarm,
	.reset_vbatovp_alarm = upm6720_reset_vbatovp_alarm,
	.set_vbusovp_alarm = upm6720_set_vbusovp_alarm,
	.reset_vbusovp_alarm = upm6720_reset_vbusovp_alarm,
	.is_vbuslowerr = upm6720_is_vbuslowerr,
	.get_adc_accuracy = upm6720_get_adc_accuracy,
	.typec_mux_otg_enable = upm6720_typec_mux_otg_enable,
};

static int upm6720_register_chgdev(struct upm6720_chip *chip)
{
	chip->chg_prop.alias_name = chip->desc->chg_name;
	chip->chg_dev = charger_device_register(chip->desc->chg_name, chip->dev,
						chip, &upm6720_chg_ops,
						&chip->chg_prop);
	return chip->chg_dev ? 0 : -EINVAL;
}

static int upm6720_clearall_irq(struct upm6720_chip *chip)
{
	int i, ret;
	u8 data;

	for (i = 0; i < UPM6720_SF_MAX; i++) {
		ret = upm6720_i2c_read8(chip, upm6720_reg_sf[i], &data);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int upm6720_init_irq(struct upm6720_chip *chip)
{
	int ret = 0, len = 0;
	char *name = NULL;

	upm_dbg(chip->dev, "\n");
	ret = upm6720_clearall_irq(chip);
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
	upm_dbg(chip->dev, "irq = %d\n", chip->irq);

	/* Request threaded IRQ */
	len = strlen(chip->desc->chg_name);
	name = devm_kzalloc(chip->dev, len + 5, GFP_KERNEL);
	snprintf(name, len + 5, "%s_irq", chip->desc->chg_name);
	ret = devm_request_threaded_irq(chip->dev, chip->irq, NULL,
		upm6720_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, name,
		chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s request thread irq fail(%d)\n", __func__,
			ret);
		return ret;
	}
	device_init_wakeup(chip->dev, true);
	return 0;
}

#define UPM6720_DT_VALPROP(name, reg, shft, mask, func, base) \
	{#name, offsetof(struct upm6720_desc, name), reg, shft, mask, func, base}

struct upm6720_dtprop {
	const char *name;
	size_t offset;
	u8 reg;
	u8 shft;
	u8 mask;
	u8 (*toreg)(u32 val);
	u8 base;
};

static inline void upm6720_parse_dt_u32(struct device_node *np, void *desc,
				       const struct upm6720_dtprop *props,
				       int prop_cnt)
{
	int i;

	for (i = 0; i < prop_cnt; i++) {
		if (unlikely(!props[i].name))
			continue;
		of_property_read_u32(np, props[i].name, desc + props[i].offset);
	}
}

static inline void upm6720_parse_dt_bool(struct device_node *np, void *desc,
					const struct upm6720_dtprop *props,
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

static inline int upm6720_apply_dt(struct upm6720_chip *chip, void *desc,
				  const struct upm6720_dtprop *props,
				  int prop_cnt)
{
	int i, ret;
	u32 val;

	for (i = 0; i < prop_cnt; i++) {
		val = *(u32 *)(desc + props[i].offset);
		if (props[i].toreg)
			val = props[i].toreg(val);
		val += props[i].base;
		ret = upm6720_i2c_update_bits(chip, props[i].reg,
					     val << props[i].shft,
					     props[i].mask);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct upm6720_dtprop upm6720_dtprops_u32[] = {
	//Reg00
	UPM6720_DT_VALPROP(vbatovp, UPM6720_REG_VBATOVP, 0, 0x7f,
			  upm6720_vbatovp_toreg, 0),
	//Reg01
	UPM6720_DT_VALPROP(vbatovp_alm, UPM6720_REG_VBATOVP_ALM, 0, 0x7f,
			  upm6720_vbatovp_alm_toreg, 0),
	//Reg02
	UPM6720_DT_VALPROP(ibatocp, UPM6720_REG_IBATOCP, 0, 0x7f,
			  upm6720_ibatocp_toreg, 0),
	//Reg03
	UPM6720_DT_VALPROP(ibatocp_alm, UPM6720_REG_IBATOCP_ALM, 0, 0x7f,
			  upm6720_ibatocp_alm_toreg, 0),
	//Reg06
	UPM6720_DT_VALPROP(vbusovp, UPM6720_REG_VBUSOVP, 0, 0x7f,
			  upm6720_vbusovp_toreg, 0),
	//Reg07
	UPM6720_DT_VALPROP(vbusovp_alm, UPM6720_REG_VBUSOVP_ALM, 0, 0x7f,
			  upm6720_vbusovp_alm_toreg, 0),
	//Reg08
	UPM6720_DT_VALPROP(ibusocp, UPM6720_REG_IBUSOCP, 0, 0x1f,
			  upm6720_ibusocp_toreg, 0),
	//Reg09
	UPM6720_DT_VALPROP(ibusocp_alm, UPM6720_REG_IBUSOCP_ALM, 0, 0x1f,
			  upm6720_ibusocp_alm_toreg, 0),
	//Reg0B
	UPM6720_DT_VALPROP(tdie_alm, UPM6720_REG_TDIE_ALM, 0, 0xff,
			  upm6720_tdie_alm_toreg, 0),
	//Reg0C
	UPM6720_DT_VALPROP(tsbus_flt, UPM6720_REG_TSBUS_FLT, 0, 0xff,
			  upm6720_tsbat_flt_toreg, 0),
	//Reg0D
	UPM6720_DT_VALPROP(tsbat_flt, UPM6720_REG_TSBAT_FLT, 0, 0xff,
			  upm6720_tsbat_flt_toreg, 0),
	//Reg0E
	UPM6720_DT_VALPROP(vac1ovp, UPM6720_REG_VAC_CTRL, 5, 0xe0,
			  upm6720_vacovp_toreg, 0),
	UPM6720_DT_VALPROP(vac2ovp, UPM6720_REG_VAC_CTRL, 2, 0x1c,
			  upm6720_vacovp_toreg, 0),
	//Reg10
	UPM6720_DT_VALPROP(fsw_set, UPM6720_REG_CTRL3, 5, 0xe0,
			  upm6720_fsw_set_toreg, 0),
	UPM6720_DT_VALPROP(wd_timeout, UPM6720_REG_CTRL3, 3, 0x18,
			  upm6720_wdt_toreg, 0),
	//Reg11
	UPM6720_DT_VALPROP(ibat_rsense, UPM6720_REG_CTRL4, 7, 0x80, NULL, 0),
	UPM6720_DT_VALPROP(ss_timeout, UPM6720_REG_CTRL4, 4, 0x70,
			  upm6720_ss_timeout_toreg, 0),
	UPM6720_DT_VALPROP(ibusucpf_deglitch, UPM6720_REG_CTRL4, 2, 0x0c,
			  upm6720_ibusucpf_deglitch_toreg, 0),
	//Reg12
	UPM6720_DT_VALPROP(vout_ovp, UPM6720_REG_CTRL5, 5, 0x60,
			  upm6720_voutovp_toreg, 0),
	UPM6720_DT_VALPROP(freq_shift, UPM6720_REG_CTRL5, 3, 0x18, NULL, 0),
};

static const struct upm6720_dtprop upm6720_dtprops_bool[] = {
	//Reg00
	UPM6720_DT_VALPROP(vbatovp_dis, UPM6720_REG_VBATOVP, 7, 0x80, NULL, 0),
	//Reg01
	UPM6720_DT_VALPROP(vbatovp_alm_dis, UPM6720_REG_VBATOVP_ALM, 7, 0x80, NULL, 0),
	//Reg02
	UPM6720_DT_VALPROP(ibatocp_dis, UPM6720_REG_IBATOCP, 7, 0x80, NULL, 0),
	//Reg03
	UPM6720_DT_VALPROP(ibatocp_alm_dis, UPM6720_REG_IBATOCP_ALM, 7, 0x80, NULL, 0),
	//Reg05
	UPM6720_DT_VALPROP(ibusucp_dis, UPM6720_REG_CTRL1, 7, 0x80, NULL, 0),
	UPM6720_DT_VALPROP(vbus_errhi_dis, UPM6720_REG_CTRL1, 2, 0x04, NULL, 0),
	//Reg06
	UPM6720_DT_VALPROP(vbus_pd_en, UPM6720_REG_VBUSOVP, 7, 0x80, NULL, 0),
	//Reg07
	UPM6720_DT_VALPROP(vbusovp_alm_dis, UPM6720_REG_VBUSOVP_ALM, 7, 0x80, NULL, 0),
	//Reg08	  
	UPM6720_DT_VALPROP(ibusocp_dis, UPM6720_REG_IBUSOCP, 7, 0x80, NULL, 0),
	//Reg09	  
	UPM6720_DT_VALPROP(ibusocp_alm_dis, UPM6720_REG_IBUSOCP_ALM, 7, 0x80, NULL, 0),
	//Reg0A
	UPM6720_DT_VALPROP(tshut_dis, UPM6720_REG_TEMP_CTRL, 7, 0x80, NULL, 0),
	UPM6720_DT_VALPROP(tshut_alm_dis, UPM6720_REG_TEMP_CTRL, 4, 0x10, NULL, 0),
	UPM6720_DT_VALPROP(tsbus_flt_dis, UPM6720_REG_TEMP_CTRL, 3, 0x08, NULL, 0),
	UPM6720_DT_VALPROP(tsbat_flt_dis, UPM6720_REG_TEMP_CTRL, 2, 0x04, NULL, 0),
	//Reg0E
	UPM6720_DT_VALPROP(vac1_pd_en, UPM6720_REG_VAC_CTRL, 1, 0x02, NULL, 0),
	UPM6720_DT_VALPROP(vac2_pd_en, UPM6720_REG_VAC_CTRL, 0, 0x01, NULL, 0),
	//Reg10
	UPM6720_DT_VALPROP(wd_timeout_dis, UPM6720_REG_CTRL3, 2, 0x04, NULL, 0),
	//Reg12
	UPM6720_DT_VALPROP(voutovp_dis, UPM6720_REG_CTRL5, 7, 0x80, NULL, 0),
	//Reg23
	UPM6720_DT_VALPROP(ibusadc_dis, UPM6720_REG_ADC_CTRL, 1, 0x02, NULL, 0),
	UPM6720_DT_VALPROP(vbusadc_dis, UPM6720_REG_ADC_CTRL, 0, 0x01, NULL, 0),
	//Reg24
	UPM6720_DT_VALPROP(vacadc1_dis, UPM6720_REG_ADC_FN_DISABLE, 7, 0x80, NULL, 0),
	UPM6720_DT_VALPROP(vacadc2_dis, UPM6720_REG_ADC_FN_DISABLE, 6, 0x40, NULL, 0),
	UPM6720_DT_VALPROP(voutadc_dis, UPM6720_REG_ADC_FN_DISABLE, 5, 0x20, NULL, 0),
	UPM6720_DT_VALPROP(vbatadc_dis, UPM6720_REG_ADC_FN_DISABLE, 4, 0x10, NULL, 0),
	UPM6720_DT_VALPROP(ibatadc_dis, UPM6720_REG_ADC_FN_DISABLE, 3, 0x08, NULL, 0),
	UPM6720_DT_VALPROP(tsbusadc_dis, UPM6720_REG_ADC_FN_DISABLE, 2, 0x04, NULL, 0),
	UPM6720_DT_VALPROP(tsbatadc_dis, UPM6720_REG_ADC_FN_DISABLE, 1, 0x02, NULL, 0),
	UPM6720_DT_VALPROP(tdieadc_dis, UPM6720_REG_ADC_FN_DISABLE, 0, 0x01, NULL, 0),
};

static int upm6720_parse_dt(struct upm6720_chip *chip)
{
	struct upm6720_desc *desc;
	struct device_node *np = chip->dev->of_node;
	struct device_node *child_np;

	if (!np)
		return -ENODEV;

	chip->irq_gpio = devm_gpiod_get(chip->dev, "upm6720,intr", GPIOD_IN);
	if (IS_ERR(chip->irq_gpio))
		return PTR_ERR(chip->irq_gpio);

	desc = devm_kzalloc(chip->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &upm6720_desc_defval, sizeof(*desc));
	if (of_property_read_string(np, "rm_name", &desc->rm_name) < 0)
		upm_dbg(chip->dev, "no rm name\n");
	if (of_property_read_u8(np, "rm_slave_addr", &desc->rm_slave_addr) < 0)
		upm_dbg(chip->dev, "no regmap slave addr\n");
	child_np = of_get_child_by_name(np, upm6720_type_name[chip->type]);
	if (!child_np) {
		upm_dbg(chip->dev, "no node(%s) found\n",
			upm6720_type_name[chip->type]);
		return -ENODEV;
	}
	if (of_property_read_string(child_np, "chg_name", &desc->chg_name) < 0)
		upm_dbg(chip->dev, "no chg name\n");
	upm6720_parse_dt_u32(child_np, (void *)desc, upm6720_dtprops_u32,
			    ARRAY_SIZE(upm6720_dtprops_u32));
	upm6720_parse_dt_bool(child_np, (void *)desc, upm6720_dtprops_bool,
			     ARRAY_SIZE(upm6720_dtprops_bool));

	chip->desc = desc;
	return 0;
}

static int upm6720_reset_register(struct upm6720_chip *chip)
{
	int ret;

	ret = upm6720_set_bits(chip, UPM6720_REG_CTRL2, UPM6720_RST_MASK);
	upm_dbg(chip->dev, "ret(%d)\n", ret);
	usleep_range(5, 10);
	return ret;
}

static int __upm6720_init_chip(struct upm6720_chip *chip)
{
	int ret;

	upm_dbg(chip->dev, "\n");
	ret = upm6720_reset_register(chip);
	if (ret < 0)
		return ret;
	ret = upm6720_apply_dt(chip, (void *)chip->desc, upm6720_dtprops_u32,
			      ARRAY_SIZE(upm6720_dtprops_u32));
	if (ret < 0)
		return ret;
	ret = upm6720_apply_dt(chip, (void *)chip->desc, upm6720_dtprops_bool,
			      ARRAY_SIZE(upm6720_dtprops_bool));
	if (ret < 0)
		return ret;

	if (dbg_log_en == true) {
		upm6720_dump_reg(chip);
	}
	chip->wdt_en = !chip->desc->wd_timeout_dis;
	return chip->wdt_en ? upm6720_enable_wdt(chip, false) : 0;
}

static int upm6720_check_devinfo(struct i2c_client *client, u8 *chip_rev,
				enum upm6720_type *type)
{
	int ret;

	dev_info(&client->dev, "%s rev\n", __func__);
	ret = i2c_smbus_read_byte_data(client, UPM6720_REG_PART_INFO);
	if (ret < 0) {
		dev_err(&client->dev, "%s 1 ret=%d\n", __func__, ret);
		return ret;
	} else {
		dev_info(&client->dev, "%s 1 ret=%d\n", __func__, ret);
	}

	if ((ret & UPM6720_DEVID_MASK) != UPM6720_DEVID) {
		dev_info(&client->dev, "%s 2 ret=%d\n", __func__, ret);
		//return -ENODEV;
	}

	*chip_rev = (ret & UPM6720_DEVREV_MASK) >> UPM6720_DEVREV_SHFT;

	ret = i2c_smbus_read_byte_data(client, UPM6720_REG_CTRL5);
	if (ret < 0) {
		dev_err(&client->dev, "%s 3 ret=%d\n", __func__, ret);
		return ret;
	}
	*type = (ret & UPM6720_MS_MASK) >> UPM6720_MS_SHFT;
	if (*type < 0)
		return -EINVAL;
	dev_info(&client->dev, "%s rev(0x%02X), type(%s)\n", __func__,
		 *chip_rev, upm6720_type_name[*type]);

	return 0;
}

static int upm6720_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int ret;
	struct upm6720_chip *chip;
	u8 chip_rev;
	enum upm6720_type type;

	dev_info(&client->dev, "%s(%s)\n", __func__, UPM6720_DRV_VERSION);

	ret = upm6720_check_devinfo(client, &chip_rev, &type);
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

	ret = upm6720_parse_dt(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);
		goto err;
	}

#ifdef CONFIG_RT_REGMAP
	ret = upm6720_register_regmap(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg regmap fail(%d)\n", __func__, ret);
		goto err;
	}
#endif /* CONFIG_RT_REGMAP */

	ret = __upm6720_init_chip(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s init chip fail(%d)\n", __func__, ret);
		goto err_unreg_regmap;
	}

	ret = upm6720_register_chgdev(chip);
	if (ret < 0) {
		dev_err(chip->dev, "%s reg chgdev fail(%d)\n", __func__, ret);
		goto err_unreg_regmap;
	}

	chip->notify_task = kthread_run(upm6720_notify_task_threadfn, chip,
					"notify_thread");
	if (IS_ERR(chip->notify_task)) {
		dev_err(chip->dev, "%s run notify thread fail(%d)\n", __func__,
			ret);
		ret = PTR_ERR(chip->notify_task);
		goto err_unreg_chgdev;
	}

	ret = upm6720_init_irq(chip);
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

static void upm6720_i2c_shutdown(struct i2c_client *client)
{
	struct upm6720_chip *chip = i2c_get_clientdata(client);

	dev_info(&client->dev, "%s\n", __func__);
	if (chip)
		upm6720_reset_register(chip);
}

static void upm6720_i2c_remove(struct i2c_client *client)
{
	struct upm6720_chip *chip = i2c_get_clientdata(client);

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

static int __maybe_unused upm6720_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct upm6720_chip *chip = i2c_get_clientdata(i2c);

	upm_dbg(dev, "\n");
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	disable_irq(chip->irq);
	return 0;
}

static int __maybe_unused upm6720_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct upm6720_chip *chip = i2c_get_clientdata(i2c);

	upm_dbg(dev, "\n");
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);
	enable_irq(chip->irq);
	return 0;
}

static SIMPLE_DEV_PM_OPS(upm6720_pm_ops, upm6720_i2c_suspend, upm6720_i2c_resume);

static const struct of_device_id upm6720_of_id[] = {
	{ .compatible = "unisemi,upm6720" },
	{},
};
MODULE_DEVICE_TABLE(of, upm6720_of_id);

static const struct i2c_device_id upm6720_i2c_id[] = {
	{ "upm6720", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, upm6720_i2c_id);

static struct i2c_driver upm6720_i2c_driver = {
	.driver = {
		.name = "upm6720",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(upm6720_of_id),
		.pm = &upm6720_pm_ops,
	},
	.probe = upm6720_i2c_probe,
	.shutdown = upm6720_i2c_shutdown,
	.remove = upm6720_i2c_remove,
	.id_table = upm6720_i2c_id,
};
module_i2c_driver(upm6720_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Unisemi UPM6720 ChargePump Driver");
MODULE_AUTHOR("Allan ouyang<yangpingao@wingtech.com>");
MODULE_VERSION(UPM6720_DRV_VERSION);
