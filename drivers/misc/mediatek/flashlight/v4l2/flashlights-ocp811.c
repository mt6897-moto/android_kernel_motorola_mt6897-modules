// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>

#include "flashlight-core.h"
#include "flashlight-dt.h"

/* define device tree */
/* TODO: modify temp device tree name */
#define OCP811_GPIO_DTNAME "mediatek,flashlights_ocp811_gpio"

/* TODO: define driver name */
#define OCP811_NAME "flashlights-ocp811"

/* define registers */
/* TODO: define register */

/* define mutex and work queue */
static DEFINE_MUTEX(ocp811_mutex);
static struct work_struct ocp811_work;

#define OCP811_TIMEOUT_EN 1
#define OCP811_TIMEOUT_MS 500

/* define pinctrl */
/* TODO: define pinctrl */
#define OCP811_PINCTRL_PIN_FLASH_TORCH     1
#define OCP811_PINCTRL_PIN_FLASH_EN        0

#define OCP811_PINCTRL_PINSTATE_LOW  0
#define OCP811_PINCTRL_PINSTATE_HIGH 1

#define OCP811_PINCTRL_STATE_FLASH_TORCH_HIGH "flash_torch_high"
#define OCP811_PINCTRL_STATE_FLASH_TORCH_LOW  "flash_torch_low"

#define OCP811_PINCTRL_STATE_FLASH_EN_HIGH    "flash_en_high"
#define OCP811_PINCTRL_STATE_FLASH_EN_LOW     "flash_en_low"


/******************************************************************************
 * ocp811 operations
 *****************************************************************************/
#define OCP811_CHANNEL_NUM 1
#define OCP811_CHANNEL_CH1 0

#define OCP811_NONE (-1)
#define OCP811_DISABLE 0
#define OCP811_ENABLE 1

#define OCP811_ENABLE_TORCH 0
#define OCP811_ENABLE_FLASH 1

#define OCP811_LEVEL_NUM 2
#define OCP811_LEVEL_TORCH 0

#define OCP811_LEVEL_FLASH OCP811_LEVEL_NUM

#define OCP811_WDT_TIMEOUT 1248 /* ms */
#define OCP811_HW_TIMEOUT 400 /* ms */

static const int ocp811_current[OCP811_LEVEL_NUM] = {
	 200,  1000
};


static struct pinctrl *ocp811_pinctrl;
static struct pinctrl_state *ocp811_flash_torch_high;
static struct pinctrl_state *ocp811_flash_torch_low;

static struct pinctrl_state *ocp811_flash_en_high;
static struct pinctrl_state *ocp811_flash_en_low;

/* define usage count */
static int use_count;
static int flash_level = 0;

/* platform data */
struct ocp811_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};

/******************************************************************************
 * Pinctrl configuration
 *****************************************************************************/
static int ocp811_pinctrl_init(struct platform_device *pdev)
{
	int ret = 0;

	/* get pinctrl */
	ocp811_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(ocp811_pinctrl)) {
		pr_info("Failed to get flashlight pinctrl.\n");
		ret = PTR_ERR(ocp811_pinctrl);
		return ret;
	}

	/* TODO: Flashlight pin initialization */
	ocp811_flash_torch_high = pinctrl_lookup_state(
			ocp811_pinctrl, OCP811_PINCTRL_STATE_FLASH_TORCH_HIGH);
	if (IS_ERR(ocp811_flash_torch_high)) {
		pr_info("Failed to init (%s)\n", OCP811_PINCTRL_STATE_FLASH_TORCH_HIGH);
		ret = PTR_ERR(ocp811_flash_torch_high);
	}
	ocp811_flash_torch_low = pinctrl_lookup_state(
			ocp811_pinctrl, OCP811_PINCTRL_STATE_FLASH_TORCH_LOW);
	if (IS_ERR(ocp811_flash_torch_low)) {
		pr_info("Failed to init (%s)\n", OCP811_PINCTRL_STATE_FLASH_TORCH_LOW);
		ret = PTR_ERR(ocp811_flash_torch_low);
	}

	ocp811_flash_en_high = pinctrl_lookup_state(
			ocp811_pinctrl, OCP811_PINCTRL_STATE_FLASH_EN_HIGH);
	if (IS_ERR(ocp811_flash_en_high)) {
		pr_info("Failed to init (%s)\n", OCP811_PINCTRL_STATE_FLASH_EN_HIGH);
		ret = PTR_ERR(ocp811_flash_en_high);
	}
	ocp811_flash_en_low = pinctrl_lookup_state(
			ocp811_pinctrl, OCP811_PINCTRL_STATE_FLASH_EN_LOW);
	if (IS_ERR(ocp811_flash_en_low)) {
		pr_info("Failed to init (%s)\n", OCP811_PINCTRL_STATE_FLASH_EN_LOW);
		ret = PTR_ERR(ocp811_flash_en_low);
	}
	return ret;
}

static int ocp811_pinctrl_set(int pin, int state)
{
	int ret = 0;

	if (IS_ERR(ocp811_pinctrl)) {
		pr_info("pinctrl is not available\n");
		return -1;
	}

	switch (pin) {
	case OCP811_PINCTRL_PIN_FLASH_TORCH:
		if (state == OCP811_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(ocp811_flash_torch_low))
			pinctrl_select_state(ocp811_pinctrl, ocp811_flash_torch_low);
		else if (state == OCP811_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(ocp811_flash_torch_high))
			pinctrl_select_state(ocp811_pinctrl, ocp811_flash_torch_high);
		else
			pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	case OCP811_PINCTRL_PIN_FLASH_EN:
		if (state == OCP811_PINCTRL_PINSTATE_LOW &&
				!IS_ERR(ocp811_flash_en_low))
			pinctrl_select_state(ocp811_pinctrl, ocp811_flash_en_low);
		else if (state == OCP811_PINCTRL_PINSTATE_HIGH &&
				!IS_ERR(ocp811_flash_en_high))
			pinctrl_select_state(ocp811_pinctrl, ocp811_flash_en_high);
		else
			pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	default:
		pr_info("set err, pin(%d) state(%d)\n", pin, state);
		break;
	}
	pr_info("pin(%d) state(%d)\n", pin, state);

	return ret;
}

/* flashlight disable function */
static int ocp811_disable(void)
{
	/* TODO: wrap disable function */
	pr_err("ocp811_disable flash_level %d\n", flash_level);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_LOW);
	return 0;
}

/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static struct hrtimer ocp811_timer;
static unsigned int ocp811_timeout_ms;

static void ocp811_work_disable(struct work_struct *data)
{
	pr_err("work queue callback ocp811 timeout ms\n");
	ocp811_disable();
}

static enum hrtimer_restart ocp811_timer_func(struct hrtimer *timer)
{
	schedule_work(&ocp811_work);
	return HRTIMER_NORESTART;
}

/******************************************************************************
 * dummy operations
 *****************************************************************************/
/* flashlight enable function */
static int ocp811_enable(void)
{
#if OCP811_TIMEOUT_EN
	ktime_t ktime;
	unsigned int s;
	unsigned int ns;
#endif

	/* TODO: wrap enable function */
	pr_err("ocp811_enable flash_level %d ocp811_timeout_ms %d\n", flash_level,ocp811_timeout_ms);
	if(flash_level)
	{
#if OCP811_TIMEOUT_EN
		s = OCP811_TIMEOUT_MS / 1000;
		ns = OCP811_TIMEOUT_MS % 1000 * 1000000;
		ktime = ktime_set(s, ns);
		hrtimer_start(&ocp811_timer, ktime,
				HRTIMER_MODE_REL);
#endif
		ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_HIGH);
		ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_HIGH);
	}else{
		ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
		ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_HIGH);
	}
	return 0;
}

/* set flashlight level */
static int ocp811_set_level(int level)
{
	/* TODO: wrap set level function */
	flash_level = level;
	return 0;
}

/* flashlight init */
static int ocp811_init(void)
{
	/* TODO: wrap init function */
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_LOW);
	return 0;
}

/* flashlight uninit */
static int ocp811_uninit(void)
{
	/* TODO: wrap uninit function */
	pr_info("ocp811_uninit\n");
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_LOW);
	return 0;
}

static int ocp811_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= OCP811_LEVEL_NUM)
		level = OCP811_LEVEL_NUM - 1;

	return level;
}

/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int ocp811_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_info("FLASH_IOC_SET_TIME_OUT_TIME_MS(channel %d): %d\n",
				channel, (int)fl_arg->arg);
		ocp811_timeout_ms = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_info("FLASH_IOC_SET_DUTY(channel %d): %d\n",
				channel, (int)fl_arg->arg);
		ocp811_set_level(fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_info("FLASH_IOC_SET_ONOFF(channel %d): %d \n",
				channel, (int)fl_arg->arg);

		if (fl_arg->arg == 1) {
			ocp811_enable();
		} else {
			ocp811_disable();
#if OCP811_TIMEOUT_EN
			hrtimer_cancel(&ocp811_timer);
#endif
		}
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		pr_info("FLASH_IOC_GET_DUTY_NUMBER(%d)\n", channel);
		fl_arg->arg = OCP811_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		pr_info("FLASH_IOC_GET_MAX_TORCH_DUTY(%d)\n", channel);
		fl_arg->arg = OCP811_LEVEL_TORCH;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = ocp811_verify_level(fl_arg->arg);
		pr_info("FLASH_IOC_GET_DUTY_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		fl_arg->arg = ocp811_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		pr_info("FLASH_IOC_GET_HW_TIMEOUT(%d)\n", channel);
		fl_arg->arg = OCP811_HW_TIMEOUT;
		break;
	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int ocp811_open(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int ocp811_release(void)
{
	/* Move to set driver for saving power */
	return 0;
}

static int ocp811_set_driver(int set)
{
	int ret = 0;

	/* set chip and usage count */
	mutex_lock(&ocp811_mutex);
	if (set) {
		if (!use_count)
			ret = ocp811_init();
		use_count++;
		pr_info("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = ocp811_uninit();
		if (use_count < 0)
			use_count = 0;
		pr_info("Unset driver: %d\n", use_count);
	}
	mutex_unlock(&ocp811_mutex);

	return ret;
}

static ssize_t ocp811_strobe_store(struct flashlight_arg arg)
{
	ocp811_set_driver(1);
	ocp811_set_level(arg.level);
	ocp811_timeout_ms = 0;
	ocp811_enable();
	msleep(arg.dur);
	ocp811_disable();
	ocp811_set_driver(0);

	return 0;
}

static struct flashlight_operations ocp811_ops = {
	ocp811_open,
	ocp811_release,
	ocp811_ioctl,
	ocp811_strobe_store,
	ocp811_set_driver
};


/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
static int ocp811_chip_init(void)
{
	/* NOTE: Chip initialication move to "set driver" for power saving.
	 * ocp811_init();
	 */

	return 0;
}

static int ocp811_parse_dt(struct device *dev,
		struct ocp811_platform_data *pdata)
{
	struct device_node *np, *cnp;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node || !pdata)
		return -ENODEV;

	np = dev->of_node;

	pdata->channel_num = of_get_child_count(np);
	if (!pdata->channel_num) {
		pr_info("Parse no dt, node.\n");
		return 0;
	}
	pr_info("Channel number(%d).\n", pdata->channel_num);

	if (of_property_read_u32(np, "decouple", &decouple))
		pr_info("Parse no dt, decouple.\n");

	pdata->dev_id = devm_kzalloc(dev,
			pdata->channel_num *
			sizeof(struct flashlight_device_id),
			GFP_KERNEL);
	if (!pdata->dev_id)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type", &pdata->dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp, "ct", &pdata->dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp, "part", &pdata->dev_id[i].part))
			goto err_node_put;
		snprintf(pdata->dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				OCP811_NAME);
		pdata->dev_id[i].channel = i;
		pdata->dev_id[i].decouple = decouple;

		pr_info("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				pdata->dev_id[i].type, pdata->dev_id[i].ct,
				pdata->dev_id[i].part, pdata->dev_id[i].name,
				pdata->dev_id[i].channel,
				pdata->dev_id[i].decouple);
		i++;
	}

	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}

static int ocp811_factory_enable(void)
{
	pr_info("ocp811_factory_enable start.\n");
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_HIGH);
	return 0;
}

static int ocp811_factory_disable(void)
{
	pr_info("ocp811_factory_disable start.\n");
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_TORCH, OCP811_PINCTRL_PINSTATE_LOW);
	ocp811_pinctrl_set(OCP811_PINCTRL_PIN_FLASH_EN, OCP811_PINCTRL_PINSTATE_LOW);
	return 0;
}

static ssize_t led_flash_show(struct device *dev, struct device_attribute *attr, char *buf){
    return sprintf(buf, "%d\n", 0);
}
static ssize_t led_flash_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size){
	unsigned long value;
	int err;
	err = kstrtoul(buf, 10, &value);
	if(err != 0){
		return err;
	}
	//pr_info("value:%d. \n", value);
	switch(value){
		case 0: //off
			err = ocp811_factory_disable();
			if(err < 0)
				pr_err("AAA - error1 - AAA\n");
			break;
		case 1: //on
			err = ocp811_factory_enable();
			if(err < 0)
				pr_err("AAA - error2 - AAA\n");
			break;
		default :
			pr_err("AAA - error3 - AAA\n");
			break;
	}
	return 1;
}
static DEVICE_ATTR(led_flash, 0664, led_flash_show, led_flash_store);


static int ocp811_probe(struct platform_device *pdev)
{
	struct ocp811_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int err, ret;
	int i;

	pr_info("Probe start.\n");

	/* init pinctrl */
	if (ocp811_pinctrl_init(pdev)) {
		pr_err("Failed to init pinctrl.\n");
		err = -EFAULT;
		goto err;
	}

	/* init platform data */
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			err = -ENOMEM;
			goto err;
		}
		pdev->dev.platform_data = pdata;
		err = ocp811_parse_dt(&pdev->dev, pdata);
		if (err)
			goto err;
	}

	/* init work queue */
	INIT_WORK(&ocp811_work, ocp811_work_disable);

	/* init timer */
	hrtimer_init(&ocp811_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ocp811_timer.function = ocp811_timer_func;
	ocp811_timeout_ms = OCP811_TIMEOUT_MS;

	/* init chip hw */
	ocp811_chip_init();

	/* clear usage count */
	use_count = 0;

	/* register flashlight device */
	if (pdata->channel_num) {
		for (i = 0; i < pdata->channel_num; i++)
			if (flashlight_dev_register_by_device_id(
						&pdata->dev_id[i],
						&ocp811_ops)) {
				err = -EFAULT;
				goto err;
			}
	} else {
		if (flashlight_dev_register(OCP811_NAME, &ocp811_ops)) {
			err = -EFAULT;
			goto err;
		}
	}
	//add file node
	ret = device_create_file(&pdev->dev, &dev_attr_led_flash);
	if(ret < 0) {
		pr_err("=== create led_flash_node file failed === \n");
	}

	pr_info("Probe done.\n");

	return 0;
err:
	return err;
}

static int ocp811_remove(struct platform_device *pdev)
{
	struct ocp811_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i;

	pr_info("Remove start.\n");

	pdev->dev.platform_data = NULL;

	/* unregister flashlight device */
	if (pdata && pdata->channel_num)
		for (i = 0; i < pdata->channel_num; i++)
			flashlight_dev_unregister_by_device_id(
					&pdata->dev_id[i]);
	else
		flashlight_dev_unregister(OCP811_NAME);

	/* flush work queue */
	flush_work(&ocp811_work);

	pr_info("Remove done.\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id ocp811_gpio_of_match[] = {
	{.compatible = OCP811_GPIO_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, ocp811_gpio_of_match);
#else
static struct platform_device ocp811_gpio_platform_device[] = {
	{
		.name = OCP811_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, ocp811_gpio_platform_device);
#endif

static struct platform_driver ocp811_platform_driver = {
	.probe = ocp811_probe,
	.remove = ocp811_remove,
	.driver = {
		.name = OCP811_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ocp811_gpio_of_match,
#endif
	},
};

static int __init flashlight_ocp811_init(void)
{
	int ret;

	pr_info("Init start.\n");

#ifndef CONFIG_OF
	ret = platform_device_register(&ocp811_gpio_platform_device);
	if (ret) {
		pr_info("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&ocp811_platform_driver);
	if (ret) {
		pr_info("Failed to register platform driver\n");
		return ret;
	}

	pr_info("Init done.\n");

	return 0;
}

static void __exit flashlight_ocp811_exit(void)
{
	pr_info("Exit start.\n");

	platform_driver_unregister(&ocp811_platform_driver);

	pr_info("Exit done.\n");
}

module_init(flashlight_ocp811_init);
module_exit(flashlight_ocp811_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Wang <Simon-TCH.Wang@mediatek.com>");
MODULE_DESCRIPTION("MTK Flashlight DUMMY GPIO Driver");

