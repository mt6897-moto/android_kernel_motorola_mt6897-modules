#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>

#define MAX_UEVENT_PROP_NUM		20

#define SYSFS_FIELD_RW(_name, _prop, _report)\
{\
	.attr = __ATTR(_name, 0644, lenovo_charge_sysfs_show, lenovo_charge_sysfs_store),\
	.prop = _prop,\
	.report_uevent = _report,\
}

#define SYSFS_FIELD_RO(_name, _prop, _report)\
{\
	.attr = __ATTR(_name, 0444, lenovo_charge_sysfs_show, NULL),\
	.prop = _prop,\
	.report_uevent = _report,\
}

enum sysfs_property {
	/* Nodes under power_supply/battery/ */
	POWER_SUPPLY_PROP_BATTERY_SOH = 0,
	//POWER_SUPPLY_PROP_BATTERY_CYCLE_COUNT,
	POWER_SUPPLY_PROP_BATTERY_PRODUCT_DATE,
	POWER_SUPPLY_PROP_BATTERY_ACTIVATE_DATE,
	POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING,
	POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU,

	/* Nodes under power_supply/usb/ */
};

struct sysfs_info {
	struct device_attribute attr;
	enum sysfs_property prop;
	bool report_uevent;
};

