#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/vmalloc.h>

#include "lenovo_charge_sysfs.h"
#include "mtk_charger.h"

static struct power_supply *battery_psy = NULL;
static struct power_supply *ext_psy = NULL;
static struct mtk_charger *info = NULL;
static struct power_supply *chg_psy = NULL;
static int batt_soh = 0, batt_product_date = 0;
static int batt_activate_date = 0, batt_recharge_setting = 0, batt_protection_setting_eu = 0;
static const char* batt_activate_date_str = "000000";
static bool lenovo_charger_sysfs_check_psy(void)
{
	if (!(battery_psy)) {
		battery_psy = power_supply_get_by_name("battery");
	}

	if (!(battery_psy)) {
		return false;
	} else {
		return true;
	}
}

static bool lenovo_charger_sysfs_check_mm8013_psy(void)
{
	if (!(ext_psy)) {
		ext_psy = power_supply_get_by_name("battery_mm8013");
	}

	if (!(ext_psy)) {
		return false;
	} else {
		return true;
	}
}

static bool lenovo_chager_sysfs_check_charger_info(void)
{
	chg_psy = power_supply_get_by_name("mtk-master-charger");

	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_err("%s: fail to get chg_psy\n", __func__);
		return false;
	} else {
		info = (struct mtk_charger *)power_supply_get_drvdata(chg_psy);
		if (IS_ERR_OR_NULL(chg_psy)) {
			pr_err("%s: fail to get charger drvdate\n", __func__);
			return false;
		}
	}

	return false;
}

static int lenovo_charge_sysfs_get_property(struct power_supply *psy, enum sysfs_property prop, union power_supply_propval *val)
{
	union power_supply_propval ext_soh_val = {0}, ext_product_date = {0};
	int ret = 0;
	char product_date_string[20];

	if (!lenovo_charger_sysfs_check_psy()) {
		dev_err(&psy->dev, "Failed to check psy\n");
		return -EINVAL;
	}

	switch (prop) {
		/* sys/power_supply/battery */
		case POWER_SUPPLY_PROP_BATTERY_SOH:
			if (ext_psy == NULL || IS_ERR(ext_psy)) {
				/* some log */	
			} else {
				ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_PRECHARGE_CURRENT, &ext_soh_val);
				if (ret < 0) {
					dev_info(&psy->dev, "[%s] power_supply_get_property fail  \n", __func__);
				} else {
					batt_soh = ext_soh_val.intval;
					dev_info(&psy->dev, "[%s] ext_battery_healthd is %d \n", __func__, ext_soh_val.intval);
				}
			}
			val->intval = batt_soh;
			dev_info(&psy->dev, "%s get soh:%d", __func__, val->intval);
			break;
			/*
		case POWER_SUPPLY_PROP_BATTERY_CYCLE_COUNT:
			val->intval = batt_cycle_count;
			dev_info(&psy->dev, "%s get cycle_count:%d", __func__, val->intval);
			break;
			*/
		case POWER_SUPPLY_PROP_BATTERY_PRODUCT_DATE:
			if (ext_psy == NULL || IS_ERR(ext_psy)) {
				/* some log */	
			} else {
				ret = power_supply_get_property(ext_psy, POWER_SUPPLY_PROP_MANUFACTURE_YEAR, &ext_product_date);
				if (ret < 0) {
					dev_info(&psy->dev, "[%s] power_supply_get_property fail  \n", __func__);
				} else {
					batt_product_date = ext_product_date.intval;
					dev_info(&psy->dev, "[%s] ext_battery_healthd is %d \n", __func__, ext_product_date.intval);
				}
			}
			sprintf(product_date_string, "%X", batt_product_date);
			val->strval = product_date_string;

			dev_info(&psy->dev, "%s get prodect_date:%s", __func__, val->strval);
			break;
		case POWER_SUPPLY_PROP_BATTERY_ACTIVATE_DATE:
			if (batt_activate_date > 0) {
				char tempPtr[20];
				snprintf(tempPtr, sizeof(tempPtr), "%d", batt_activate_date);
				val->strval = tempPtr;
			} else {
				val->strval = batt_activate_date_str;
			}
			dev_info(&psy->dev, "%s get active_date:%s, %s, %d\n", __func__,
				val->strval, batt_activate_date_str, batt_activate_date);
			break;
		case POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING:
			val->intval = batt_recharge_setting;
			dev_info(&psy->dev, "%s get recharge_setting:%d\n", __func__, val->intval);
			break;
		case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU:
			val->intval = batt_protection_setting_eu;
			dev_info(&psy->dev, "%s get protection_setting_eu:%d\n", __func__, val->intval);
			break;

		/* sys/power_supply/usb */

		default:
			return -EINVAL;
	}

	return 0;
}

static int lenovo_charge_sysfs_set_property(struct power_supply *psy, enum sysfs_property prop, const union power_supply_propval *val)
{
	if (!lenovo_charger_sysfs_check_psy()) {
		dev_err(&psy->dev, "Failed to check psy\n");
		return -EINVAL;
	}

	switch (prop) {
		/* sys/power_supply/battery */
		case POWER_SUPPLY_PROP_BATTERY_ACTIVATE_DATE:
			dev_info(&psy->dev, "%s %d\n", __func__, batt_activate_date);
			batt_activate_date = val->intval;
			dev_info(&psy->dev, "%s set active_date:%d %d\n", __func__, val->intval, batt_activate_date);
			break;
		case POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING:
			batt_recharge_setting = val->intval;
			if (val->intval > 0 && val->intval < 100) {
				if (IS_ERR_OR_NULL(info)) {
					if (lenovo_chager_sysfs_check_charger_info() != false) {
						info->batt_recharge_setting = val->intval;
					}
				} else {
					info->batt_recharge_setting = val->intval;
				}
			}
			dev_info(&psy->dev, "%s set recharge_setting:%d", __func__, val->intval);
			break;
		case POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU:
			batt_protection_setting_eu = val->intval;
			if (IS_ERR_OR_NULL(info)) {
				if (lenovo_chager_sysfs_check_charger_info() != false) {
					info->batt_protection_setting_eu = val->intval;
				}
			} else {
				info->batt_protection_setting_eu = val->intval;
			}
			dev_info(&psy->dev, "%s set protection_setting_eu:%d", __func__, val->intval);
			break;

		/* sys/power_supply/usb */

		default:
			return -EINVAL;
	}

	return 0;
}

static ssize_t lenovo_charge_sysfs_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct power_supply *psy = NULL;
	struct sysfs_info *sysfs_attr;
	union power_supply_propval pval = {0};
	int val;
	ssize_t ret;

	ret = kstrtos32(buf, 0, &val);
	if (ret < 0) {
		return ret;
	}
	pval.intval = val;

	psy = dev_get_drvdata(dev);
	if (!psy) {
		dev_info(&psy->dev, "invalid psy");
		return ret;
	}

	sysfs_attr = container_of(attr, struct sysfs_info, attr);
	lenovo_charge_sysfs_set_property(psy, sysfs_attr->prop, &pval);

	return count;
}

static ssize_t lenovo_charge_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct power_supply *psy = NULL;
	struct sysfs_info *sysfs_attr;
	union power_supply_propval pval = {0};
	ssize_t count = 0;

	psy = dev_get_drvdata(dev);
	if (!psy) {
		dev_info(&psy->dev, "invalid psy");
		return count;
	}

	sysfs_attr = container_of(attr, struct sysfs_info, attr);
	lenovo_charge_sysfs_get_property(psy, sysfs_attr->prop, &pval);

	if (sysfs_attr->prop == POWER_SUPPLY_PROP_BATTERY_ACTIVATE_DATE ||
		sysfs_attr->prop == POWER_SUPPLY_PROP_BATTERY_PRODUCT_DATE) {
		count = scnprintf(buf, PAGE_SIZE, "%s\n", pval.strval);
	} else {
		count = scnprintf(buf, PAGE_SIZE, "%d\n", pval.intval);
	}

	return count;
}

static struct sysfs_info lenovo_battery_sysfs_field_tbl[] = {
	SYSFS_FIELD_RO(soh, POWER_SUPPLY_PROP_BATTERY_SOH, false),
	//SYSFS_FIELD_RO(cycle_count, POWER_SUPPLY_PROP_BATTERY_CYCLE_COUNT, false),
	SYSFS_FIELD_RO(product_date, POWER_SUPPLY_PROP_BATTERY_PRODUCT_DATE, false),
	SYSFS_FIELD_RW(activate_date, POWER_SUPPLY_PROP_BATTERY_ACTIVATE_DATE, false),
	SYSFS_FIELD_RW(recharge_setting, POWER_SUPPLY_PROP_BATTERY_RECHARGE_SETTING, false),
	SYSFS_FIELD_RW(protection_setting_eu, POWER_SUPPLY_PROP_BATTERY_PROTECTION_SETTING_EU, false),
};

static struct attribute
	*lenovo_battery_sysfs_attrs[ARRAY_SIZE(lenovo_battery_sysfs_field_tbl) + 1];

static const struct attribute_group lenovo_battery_sysfs_attr_group = {
	.attrs = lenovo_battery_sysfs_attrs,
};

static void lenovo_charge_create_sysfs(struct power_supply *psy)
{
	int i = 0, length = 0, rc = 0;

	if (!(psy)) {
		dev_info(&psy->dev, "Invaild psy\n");
		return;
	}

	if (!strcmp(psy->desc->name, "battery")){
		length = ARRAY_SIZE(lenovo_battery_sysfs_field_tbl);
		for (i = 0; i < length; i++) {
			lenovo_battery_sysfs_attrs[i] = &lenovo_battery_sysfs_field_tbl[i].attr.attr;
		}

		lenovo_battery_sysfs_attrs[length] = NULL;
		rc = sysfs_create_group(&psy->dev.kobj, &lenovo_battery_sysfs_attr_group);
		if (rc) {
			dev_info(&psy->dev, "Failed to create battery_sysfs ret:%d\n", rc);
			return;
		} else {
			dev_info(&psy->dev, "Succeed to create battery_sysfs ret:%d\n", rc);
		}

		battery_psy = psy;
	} else {
		/* TO do */
	}

	return;
}

/*
static int lenovo_charge_sysfs_uevent(struct power_supply *psy)
{
	// To do

	return 0;
}
*/

static int __init lenovo_charge_sysfs_init(void)
{
	struct power_supply *battery_psy = NULL;

	battery_psy = power_supply_get_by_name("battery");
	if (battery_psy == NULL ||IS_ERR(battery_psy)) {
		return 0;
	}

	lenovo_charge_create_sysfs(battery_psy);

	lenovo_charger_sysfs_check_psy();

	lenovo_charger_sysfs_check_mm8013_psy();

	lenovo_chager_sysfs_check_charger_info();

	return 0;
}

static void __exit lenovo_charge_sysfs_exit(void)
{
	/* To do */
}

module_init(lenovo_charge_sysfs_init);
module_exit(lenovo_charge_sysfs_exit);

MODULE_AUTHOR("liyiying@wingtech.com");
MODULE_DESCRIPTION("Lenovo charge sysfs");
MODULE_LICENSE("GPL");
