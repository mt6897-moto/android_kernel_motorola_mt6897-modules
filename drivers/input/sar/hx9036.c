/*************************************************************
*                                                            *
*  Driver for NanJingTianYiHeXin HX9036 Cap Sensor           *
*                                                            *
*************************************************************/
/*
                       客制化配置说明
dts参考(仅供参考!!!)：
hx9036: hx9036@1b {
	compatible = "tyhx,hx9036";
	reg = <0x1b>;
	tyhx,irq-gpio = <&pio 16 0x0>;
	tyhx,irq-flags = <2>; //1:rising 2:falling
	tyhx,channel-flag = <0xFF>; //used flag
	status = "okay";
};

填充上下电函数：static void hx9036_power_on(uint8_t on) 如果是常供电，可不用配置此项

根据实际硬件电路形式，FAE协助确认通道配置函数中的cs和channel映射关系：static void hx9036_ch_cfg(void)

根据实际需要，在dtsi文件的对应节点中和dts解析函数static int hx9036_parse_dt(struct device *dev)中添加你需要的其他属性信息，
如其他gpio和regulator等，参考dts配置中的中断gpio号"tyhx,irq-gpio"和通道数配置"tyhx,channel-flag"是必要的。
更多的gpio配置，请放在 static int hx9036_gpio_init(void) 和 static void hx9036_gpio_deinit(void) 中

*/

#define HX9036_DRIVER_VER "003" //Change-Id

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/notifier.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/hardware_info.h>

#include "hx9036.h"

static struct i2c_client *hx9036_i2c_client = NULL;
static struct hx9036_platform_data hx9036_pdata;
static uint8_t ch_enable_status = 0x00;
static uint8_t hx9036_polling_enable = 0;
static int hx9036_polling_period_ms = 0;
static volatile uint8_t hx9036_irq_from_suspend_flag = 0;
static volatile uint8_t hx9036_irq_en_flag = 1;

static int32_t data_raw[HX9036_CH_NUM] = {0};
static int32_t data_diff[HX9036_CH_NUM] = {0};
static int32_t data_lp[HX9036_CH_NUM] = {0};
static int32_t data_bl[HX9036_CH_NUM] = {0};
static uint16_t data_offset_dac[HX9036_CH_NUM] = {0};
static uint8_t hx9036_data_accuracy = 16;
static int32_t data_offset_csn[HX9036_CH_NUM] = {0};
static uint8_t hx9036_ch_near_state[HX9036_CH_NUM] = {0};

//64个可选的接近阈值设置
static uint16_t hx9036_threshold_near_select[] = {//size:64
    128, 144, 160, 176, 192, 208, 224, 240, 256, 320,
    384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792,
    2048, 2560, 3072, 3584, 4096, 5120, 6144, 7168, 8192, 12288,
    16384, 24576, 120, 112, 104, 96, 88, 80, 72, 64,
    60, 56, 52, 48, 44, 40, 38, 36, 34, 32,
    30, 28, 26, 24, 22, 20, 18, 16, 14, 13,
    12, 10, 8, 32767
};

//hx9036没有远离阈值配置寄存器，芯片基于当前接近阈值自动配置远离阈值，故此处的thr_far配置是无效的。
static struct hx9036_near_far_threshold hx9036_ch_thres1[HX9036_CH_NUM] = {
    {.thr_near = 32767, .thr_far = 0}, //ch0
    {.thr_near = 144, .thr_far = 0},
    {.thr_near = 32767, .thr_far = 0},
    {.thr_near = 144, .thr_far = 0},
    {.thr_near = 32767, .thr_far = 0},
    {.thr_near = 320, .thr_far = 0},
    {.thr_near = 32767, .thr_far = 0},
    {.thr_near = 144, .thr_far = 0},
};

static DEFINE_MUTEX(hx9036_i2c_rw_mutex);
static DEFINE_MUTEX(hx9036_ch_en_mutex);
static DEFINE_MUTEX(hx9036_cali_mutex);

#ifdef CONFIG_PM_WAKELOCKS
static struct wakeup_source *hx9031as_wake_lock = NULL;
#else
static struct wake_lock hx9031as_wake_lock;
#endif
//=============================================={ TYHX i2c wrap begin
//从指定起始寄存器开始，连续写入count个值
static int linux_common_i2c_write(struct i2c_client *client, uint8_t *reg_addr, uint8_t *txbuf, int count)
{
    int ret = -1;
    int ii = 0;
    uint8_t buf[HX9036_MAX_XFER_SIZE + 1] = {0};
    struct i2c_msg msg[1];

    if(count > HX9036_MAX_XFER_SIZE) {
        count = HX9036_MAX_XFER_SIZE;
        PRINT_ERR("block write over size!!!\n");
    }
    buf[0] = *reg_addr;
    memcpy(buf + 1, txbuf, count);

    msg[0].addr = client->addr;
    msg[0].flags = 0;//write
    msg[0].len = count + 1;
    msg[0].buf = buf;

    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ARRAY_SIZE(msg) != ret) {
        PRINT_ERR("linux_common_i2c_write failed. ret=%d\n", ret);
        ret = -1;
        for(ii = 0; ii < msg[0].len; ii++) {
            PRINT_ERR("msg[0].addr=0x%04X, msg[0].flags=0x%04X, msg[0].len=0x%04X, msg[0].buf[%02d]=0x%02X\n",
                      msg[0].addr,
                      msg[0].flags,
                      msg[0].len,
                      ii,
                      msg[0].buf[ii]);
        }
    } else {
        ret = 0;
    }

    return ret;
}

//从指定起始寄存器开始，连续读取count个值
static int linux_common_i2c_read(struct i2c_client *client, uint8_t *reg_addr, uint8_t *rxbuf, int count)
{
    int ret = -1;
    int ii = 0;
    struct i2c_msg msg[2];

    if(count > HX9036_MAX_XFER_SIZE) {
        count = HX9036_MAX_XFER_SIZE;
        PRINT_ERR("block read over size!!!\n");
    }

    msg[0].addr = client->addr;
    msg[0].flags = 0;//write
    msg[0].len = 1;
    msg[0].buf = reg_addr;

    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;//read
    msg[1].len = count;
    msg[1].buf = rxbuf;

    ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
    if (ARRAY_SIZE(msg) != ret) {
        PRINT_ERR("linux_common_i2c_read failed. ret=%d\n", ret);
        ret = -1;
        PRINT_ERR("msg[0].addr=0x%04X, msg[0].flags=0x%04X, msg[0].len=0x%04X, msg[0].buf[0]=0x%02X\n",
                  msg[0].addr,
                  msg[0].flags,
                  msg[0].len,
                  msg[0].buf[0]);
        if(msg[1].len >= 1) {
            for(ii = 0; ii < msg[1].len; ii++) {
                PRINT_ERR("msg[1].addr=0x%04X, msg[1].flags=0x%04X, msg[1].len=0x%04X, msg[1].buf[%02d]=0x%02X\n",
                          msg[1].addr,
                          msg[1].flags,
                          msg[1].len,
                          ii,
                          msg[1].buf[ii]);
            }
        }
    } else {
        ret = 0;
    }

    return ret;
}

//return 0 for success, return -1 for errors.
static int hx9036_read(uint16_t page_addr, uint8_t *rxbuf, int count)
{
    int ret = -1;
    uint8_t reg_page = HX9036_REG_PAGE;
    uint8_t page_id = (uint8_t)(page_addr >> 8);
    uint8_t addr = (uint8_t)page_addr;

    mutex_lock(&hx9036_i2c_rw_mutex);

    ret = linux_common_i2c_write(hx9036_i2c_client, &reg_page, &page_id, 1);
    if(0 != ret) {
        PRINT_ERR("linux_common_i2c_write failed\n");
        goto exit;
    }

    ret = linux_common_i2c_read(hx9036_i2c_client, &addr, rxbuf, count);
    if(0 != ret) {
        PRINT_ERR("linux_common_i2c_read failed\n");
        goto exit;
    }

exit:
    mutex_unlock(&hx9036_i2c_rw_mutex);
    return ret;
}

//return 0 for success, return -1 for errors.
static int hx9036_write(uint16_t page_addr, uint8_t *txbuf, int count)
{
    int ret = -1;
    uint8_t reg_page = HX9036_REG_PAGE;
    uint8_t page_id = (uint8_t)(page_addr >> 8);
    uint8_t addr = (uint8_t)page_addr;

    mutex_lock(&hx9036_i2c_rw_mutex);

    ret = linux_common_i2c_write(hx9036_i2c_client, &reg_page, &page_id, 1);
    if(0 != ret) {
        PRINT_ERR("linux_common_i2c_write failed\n");
        goto exit;
    }

    ret = linux_common_i2c_write(hx9036_i2c_client, &addr, txbuf, count);
    if(0 != ret) {
        PRINT_ERR("linux_common_i2c_write failed\n");
        goto exit;
    }

exit:
    mutex_unlock(&hx9036_i2c_rw_mutex);
    return ret;
}
//==============================================} TYHX i2c wrap end

//=============================================={ TYHX common code begin
static void hx9036_data_lock(uint8_t lock_flag)
{
    int ret = -1;
    uint8_t buf[1] = {0};

    if(HX9036_DATA_LOCK == lock_flag) {
        buf[0] = 0x01;
        ret = hx9036_write(PAGE0_F1_RD_LOCK_CFG, buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }
    } else if(HX9036_DATA_UNLOCK == lock_flag) {
        buf[0] = 0x00;
        ret = hx9036_write(PAGE0_F1_RD_LOCK_CFG, buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }
    } else {
        PRINT_ERR("ERROR!!! hx9036_data_lock wrong para. now do data unlock!\n");
        buf[0] = 0x00;
        ret = hx9036_write(PAGE0_F1_RD_LOCK_CFG, buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }
    }
}

static int hx9036_id_check(void)
{
    int ret = -1;
    int ii = 0;
    uint8_t buf[2] = {0};

    for(ii = 0; ii < HX9036_ID_CHECK_COUNT; ii++) {
        ret = hx9036_read(PAGE0_01_DEVICE_ID, buf, 2);
        if(ret < 0) {
            PRINT_ERR("i2c read error\n");
            continue;
        }
        hx9036_pdata.device_id = buf[0];
        hx9036_pdata.version_id = buf[1];
        if((HX9036_CHIP_ID == hx9036_pdata.device_id))
            break;
        else
            continue;
    }

    if(HX9036_CHIP_ID == hx9036_pdata.device_id) {
        PRINT_INF("success! device_id=0x%02X version_id=0x%02X (HX9036)\n", hx9036_pdata.device_id, hx9036_pdata.version_id);
        return 0;
    } else {
        PRINT_ERR("failed! device_id=0x%02X version_id=0x%02X (UNKNOW_CHIP_ID)\n", hx9036_pdata.device_id, hx9036_pdata.version_id);
        return -1;
    }
}

static void hx9036_ch_cfg(void)
{
    int ret = -1;
    int ii = 0;
    uint16_t ch_cfg = 0;
    uint8_t cfg[HX9036_CH_NUM * 2] = {0};

    uint8_t CS0 = 0;
    uint8_t CS1 = 2;
    uint8_t CS2 = 4;
    uint8_t CS3 = 6;//当CS3连接到主控的中断引脚时，如果将CS3配置到任意CHx上均会导致中断引脚被拉低！
    uint8_t CS4 = 8;
    uint8_t CS5 = 10;
    uint8_t CS6 = 12;
    uint8_t CS7 = 14;
    uint8_t NA = 16;
    uint8_t CH0_POS = NA;
    uint8_t CH0_NEG = NA;
    uint8_t CH1_POS = NA;
    uint8_t CH1_NEG = NA;
    uint8_t CH2_POS = NA;
    uint8_t CH2_NEG = NA;
    uint8_t CH3_POS = NA;
    uint8_t CH3_NEG = NA;
    uint8_t CH4_POS = NA;
    uint8_t CH4_NEG = NA;
    uint8_t CH5_POS = NA;
    uint8_t CH5_NEG = NA;
    uint8_t CH6_POS = NA;
    uint8_t CH6_NEG = NA;
    uint8_t CH7_POS = NA;
    uint8_t CH7_NEG = NA;

    ENTER;
    if(HX9035_ON_BOARD == HX9036_CHIP_SELECT) {
        CS0 = 0;
        CS1 = 2;
        CS2 = 4;
        CS3 = 6;
        CS4 = 8;
        CS5 = 10;
        CS6 = 12;
        CS7 = 14;
        NA = 16;
        PRINT_INF("HX9035_ON_BOARD\n");
    } else if(HX9036_ON_BOARD == HX9036_CHIP_SELECT) {
        CS0 = 0;
        CS1 = 2;
        CS2 = 4;
        CS3 = 6;
        CS4 = 8;
        CS5 = 10;
        CS6 = 12;
        CS7 = 14;
        NA = 16;
        PRINT_INF("HX9036_ON_BOARD\n");
    }

    //TODO:通道客制化配置开始 =================================================
    //每个通道的正负极都可以连接到任何一个CS，按需配置。未使用的通道应配置为NA，不可删除！
    CH0_POS = CS1;
    CH0_NEG = NA;
    CH1_POS = CS0;
    CH1_NEG = CS1;
    CH2_POS = CS1;
    CH2_NEG = NA;
    CH3_POS = CS2;//当CS3连接到主控的中断引脚时，如果将CS3配置到任意CHx上均会导致中断引脚被拉低！
    CH3_NEG = NA;
    CH4_POS = CS6;
    CH4_NEG = NA;
    CH5_POS = CS5;
    CH5_NEG = CS6;
    CH6_POS = CS7;
    CH6_NEG = NA;
    CH7_POS = CS4;
    CH7_NEG = CS7;
    //通道客制化配置结束 ======================================================

    ch_cfg = (uint16_t)((0x03 << CH0_POS) + (0x02 << CH0_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH1_POS) + (0x02 << CH1_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH2_POS) + (0x02 << CH2_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH3_POS) + (0x02 << CH3_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH4_POS) + (0x02 << CH4_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH5_POS) + (0x02 << CH5_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH6_POS) + (0x02 << CH6_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ch_cfg = (uint16_t)((0x03 << CH7_POS) + (0x02 << CH7_NEG));
    cfg[ii++] = (uint8_t)(ch_cfg);
    cfg[ii++] = (uint8_t)(ch_cfg >> 8);

    ret = hx9036_write(PAGE0_68_CH0_CFG_0, cfg, HX9036_CH_NUM * 2);
    if(0 != ret) {
        PRINT_ERR("hx9036_write failed\n");
    }
}

static void hx9036_reg_init(void)
{
    int ii = 0;
    int ret = -1;

    while(ii < ARRAY_SIZE(hx9036_reg_init_list)) {
        ret = hx9036_write(hx9036_reg_init_list[ii].addr, &hx9036_reg_init_list[ii].val, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }
        ii++;
    }
}

static void hx9036_read_offset_dac(void)
{
    int ret = -1;
    int ii = 0;
    uint8_t bytes_per_channel = 0;
    uint8_t bytes_all_channels = 0;
    uint8_t rx_buf[HX9036_CH_NUM * CH_DATA_BYTES_MAX] = {0};
    uint32_t data = 0;

    hx9036_data_lock(HX9036_DATA_LOCK);
    bytes_per_channel = CH_DATA_2BYTES;
    bytes_all_channels = HX9036_CH_NUM * bytes_per_channel;
    ret = hx9036_read(PAGE0_58_OFFSET_DAC0_0, rx_buf, bytes_all_channels);
    if(0 == ret) {
        for(ii = 0; ii < HX9036_CH_NUM; ii++) {
            data = ((rx_buf[ii * bytes_per_channel + 1] << 8) | (rx_buf[ii * bytes_per_channel]));
            data = data & 0x1FFF;//13位
            data_offset_dac[ii] = data;
        }
    }
    hx9036_data_lock(HX9036_DATA_UNLOCK);

    PRINT_DBG("OFFSET,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_offset_dac[0], data_offset_dac[1], data_offset_dac[2], data_offset_dac[3],
              data_offset_dac[4], data_offset_dac[5], data_offset_dac[6], data_offset_dac[7]);
}

static void hx9036_manual_offset_calibration(void)//手动校准所有通道
{
    int ret = -1;
    uint8_t buf[1] = {0};

    mutex_lock(&hx9036_cali_mutex);
    buf[0] = 0xFF;
    ret = hx9036_write(PAGE0_57_OFFSET_CALI_CTRL1, buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_write failed\n");
    }
    PRINT_INF("chs will calibrate in next convert cycle (ODR=%dms)\n", HX9036_ODR_MS);
    mutex_unlock(&hx9036_cali_mutex);
}

static void hx9036_get_prox_state(void)
{
    int ret = -1;
    uint8_t ii = 0;
    uint8_t buf[1] = {0};

    hx9036_pdata.prox_state_reg = 0;
    ret = hx9036_read(PAGE0_A8_PROX_STATUS, buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }
    hx9036_pdata.prox_state_reg = buf[0];

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        hx9036_ch_near_state[ii] = ((hx9036_pdata.prox_state_reg >> ii) & 0x01);
    }

    PRINT_INF("hx9036_ch_near_state[0~7]:%d%d%d%d%d%d%d%d hx9036_pdata.prox_state_reg=0x%02X\n",
              hx9036_ch_near_state[0],
              hx9036_ch_near_state[1],
              hx9036_ch_near_state[2],
              hx9036_ch_near_state[3],
              hx9036_ch_near_state[4],
              hx9036_ch_near_state[5],
              hx9036_ch_near_state[6],
              hx9036_ch_near_state[7],
              hx9036_pdata.prox_state_reg);
}

static void hx9036_capcoe_init(void)
{
    int ret = -1;
    uint8_t efuse_rst = 0x02;
    uint8_t capcoe = 0;
    uint8_t buf[4] = {0};

    ret = hx9036_read(PAGE1_07_ANALOG_MEM0_WRDATA_7_0, buf, 4);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }
    PRINT_INF("REG:0x0107~010A(0x%02X 0x%02X 0x%02X 0x%02X)\n", buf[0], buf[1], buf[2], buf[3]);

    capcoe = buf[3] & 0x3F;
    if(capcoe < 22 || capcoe > 42 ) {
        PRINT_INF("REG CHANGE!");
        ret = hx9036_write(PAGE0_3D_GLOBAL_CFG0, &efuse_rst, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }

        buf[3] = 0xA0;
        ret = hx9036_write(PAGE1_07_ANALOG_MEM0_WRDATA_7_0, buf, 4);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
        }

        ret = hx9036_read(PAGE1_07_ANALOG_MEM0_WRDATA_7_0, buf, 4);
        if(0 != ret) {
            PRINT_ERR("hx9036_read failed\n");
        }
        PRINT_INF("REG:0x0107~010A(0x%02X 0x%02X 0x%02X 0x%02X)\n", buf[0], buf[1], buf[2], buf[3]);
    }
}

static int16_t hx9036_get_thres1_near(uint8_t ch)
{
    int ret = 0;
    uint8_t buf[1] = {0};

    ret = hx9036_read(PAGE0_C9_PROX_HIGH_THRES1_CFG0 + ch, buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }

    hx9036_ch_thres1[ch].thr_near = hx9036_threshold_near_select[buf[0]];
    PRINT_INF("get: hx9036_ch_thres1[%d].thr_near=%d\n", ch, hx9036_ch_thres1[ch].thr_near);
    return hx9036_ch_thres1[ch].thr_near;
}

static int16_t hx9036_get_thres1_far(uint8_t ch)
{
    int ret = 0;
    uint8_t buf[1] = {0};

    //芯片会基于当前配置的接近阈值自动设置一个远离阈值，远离阈值可以是接近阈值的1/2,3/4,7/8,1/1倍，四选一可配
    //PAGE1_E1是当前配置的该系数
    ret = hx9036_read(PAGE1_E1_PROX_LOW_THRES1_CFG0 + ch, buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }

    hx9036_get_thres1_near(ch);//从寄存器更新当前的接近阈值设置
    hx9036_ch_thres1[ch].thr_far =  hx9036_ch_thres1[ch].thr_near * HX9036_THR_FAR_COE_1_2;
    PRINT_INF("get & update: hx9036_ch_thres1[%d].thr_far=%d\n", ch, hx9036_ch_thres1[ch].thr_far);
    return hx9036_ch_thres1[ch].thr_far;
}

//芯片支持三级接近阈值配置，当前只用第一级
static int16_t hx9036_set_thres1_near(uint8_t ch, uint16_t val)
{
    int ret = -1;
    int ii = 0;
    uint8_t buf[1] = {0};

    for(ii = 0; ii < ARRAY_SIZE(hx9036_threshold_near_select); ii++) {
        if(hx9036_threshold_near_select[ii] == val) {
            buf[0] = ii;
            ret = hx9036_write(PAGE0_C9_PROX_HIGH_THRES1_CFG0 + ch, buf, 1);//6位！！！
            if(0 != ret) {
                PRINT_ERR("hx9036_write failed\n");
            }
            hx9036_ch_thres1[ch].thr_near = hx9036_threshold_near_select[ii];
            PRINT_INF("set: hx9036_ch_thres1[%d].thr_near=%d\n", ch, hx9036_ch_thres1[ch].thr_near);
            return hx9036_ch_thres1[ch].thr_near;
        }
    }

    PRINT_ERR("no element match %d\n", val);
    hx9036_get_thres1_near(ch);//从寄存器更新当前的接近阈值设置
    return hx9036_ch_thres1[ch].thr_near;
}

//static int16_t hx9036_set_thres1_far(uint8_t ch, uint16_t val)
//该芯片不要这个函数！！
//因为芯片会基于当前配置的接近阈值自动设置一个远离阈值，
//远离阈值可以是接近阈值的1/2,3/4,7/8,1/1倍，四选一可配

static void hx9036_data_select(void)
{
    int ret = -1;
    int ii = 0;
    uint8_t buf[2] = {0};

    //0x9a	0xff	7:0 lp_diff_sel 0：lp_data 1:diff_data
    //0x9b	0xff	7:0 raw_bl_sel	0: raw_data 1:bl_data
    ret = hx9036_read(PAGE0_9A_DSP_CONFIG_CTRL7, buf, 2);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {//ch0~sh7
        hx9036_pdata.sel_diff[ii] = buf[0] & (0x01 << ii);
        hx9036_pdata.sel_lp[ii] = !hx9036_pdata.sel_diff[ii];
        hx9036_pdata.sel_bl[ii] = buf[1] & (0x01 << ii);
        hx9036_pdata.sel_raw[ii] = !hx9036_pdata.sel_bl[ii];
    }
}

static void hx9036_sample(void)
{
    int ret = -1;
    int ii = 0;
    uint8_t bytes_per_channel = 0;
    uint8_t bytes_all_channels = 0;
    uint8_t rx_buf[HX9036_CH_NUM * CH_DATA_BYTES_MAX] = {0};
    int32_t data = 0;

    hx9036_data_lock(HX9036_DATA_LOCK);
    hx9036_data_select();
    //====================================================================================================
    bytes_per_channel = CH_DATA_3BYTES;
    bytes_all_channels = HX9036_CH_NUM * bytes_per_channel;
    ret = hx9036_read(PAGE0_0D_RAW_BL_CH0_0, rx_buf, bytes_all_channels);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        if(16 == hx9036_data_accuracy) {
            data = ((rx_buf[ii * bytes_per_channel + 2] << 8) | (rx_buf[ii * bytes_per_channel + 1]));
            data = (data > 0x7FFF) ? (data - (0xFFFF + 1)) : data;
        } else {
            data = ((rx_buf[ii * bytes_per_channel + 2] << 16) | (rx_buf[ii * bytes_per_channel + 1] << 8) | (rx_buf[ii * bytes_per_channel]));
            data = (data > 0x7FFFFF) ? (data - (0xFFFFFF + 1)) : data;
        }
        data_raw[ii] = 0;
        data_bl[ii] = 0;
        if(true == hx9036_pdata.sel_raw[ii]) {
            data_raw[ii] = data;
        }
        if(true == hx9036_pdata.sel_bl[ii]) {
            data_bl[ii] = data;
        }
    }
    //====================================================================================================
    bytes_per_channel = CH_DATA_3BYTES;
    bytes_all_channels = HX9036_CH_NUM * bytes_per_channel;
    ret = hx9036_read(PAGE0_25_LP_DIFF_CH0_0, rx_buf, bytes_all_channels);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        if(16 == hx9036_data_accuracy) {
            data = ((rx_buf[ii * bytes_per_channel + 2] << 8) | (rx_buf[ii * bytes_per_channel + 1]));
            data = (data > 0x7FFF) ? (data - (0xFFFF + 1)) : data;
        } else {
            data = ((rx_buf[ii * bytes_per_channel + 2] << 16) | (rx_buf[ii * bytes_per_channel + 1] << 8) | (rx_buf[ii * bytes_per_channel]));
            data = (data > 0x7FFFFF) ? (data - (0xFFFFFF + 1)) : data;
        }
        data_lp[ii] = 0;
        data_diff[ii] = 0;
        if(true == hx9036_pdata.sel_lp[ii]) {
            data_lp[ii] = data;
        }
        if(true == hx9036_pdata.sel_diff[ii]) {
            data_diff[ii] = data;
        }
    }
    //====================================================================================================
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        if(true == hx9036_pdata.sel_lp[ii] && true == hx9036_pdata.sel_bl[ii]) {
            data_diff[ii] = data_lp[ii] - data_bl[ii];
        }
    }
    //====================================================================================================
    bytes_per_channel = CH_DATA_2BYTES;
    bytes_all_channels = HX9036_CH_NUM * bytes_per_channel;
    ret = hx9036_read(PAGE0_58_OFFSET_DAC0_0, rx_buf, bytes_all_channels);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        data = ((rx_buf[ii * bytes_per_channel + 1] << 8) | (rx_buf[ii * bytes_per_channel]));
        data = data & 0x1FFF;//13位
        data_offset_dac[ii] = data;
    }
    //====================================================================================================
    hx9036_data_lock(HX9036_DATA_UNLOCK);

    PRINT_DBG("DIFF  ,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_diff[0], data_diff[1], data_diff[2], data_diff[3],
              data_diff[4], data_diff[5], data_diff[6], data_diff[7]);
    PRINT_DBG("RAW   ,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_raw[0], data_raw[1], data_raw[2], data_raw[3],
              data_raw[4], data_raw[5], data_raw[6], data_raw[7]);
    PRINT_DBG("OFFSET,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_offset_dac[0], data_offset_dac[1], data_offset_dac[2], data_offset_dac[3],
              data_offset_dac[4], data_offset_dac[5], data_offset_dac[6], data_offset_dac[7]);
    PRINT_DBG("BL    ,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_bl[0], data_bl[1], data_bl[2], data_bl[3],
              data_bl[4], data_bl[5], data_bl[6], data_bl[7]);
    PRINT_DBG("LP    ,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
              data_lp[0], data_lp[1], data_lp[2], data_lp[3],
              data_lp[4], data_lp[5], data_lp[6], data_lp[7]);
}

#if HX9036_TEST_CHS_EN //通道测试时用全开全关策略
static int hx9036_ch_en(uint8_t ch_id, uint8_t en)
{
    int ret = -1;
    uint8_t tx_buf[1] = {0};

    en = !!en;
    if(ch_id >= HX9036_CH_NUM) {
        PRINT_ERR("channel index over range!!! ch_enable_status=0x%02X (ch_id=%d, en=%d)\n",
                  ch_enable_status, ch_id, en);
        return -1;
    }

    if(1 == en) {
        if(0 == ch_enable_status) {
            hx9036_pdata.prox_state_reg = 0;
            tx_buf[0] = hx9036_pdata.channel_used_flag;
            ret = hx9036_write(PAGE0_03_CH_NUM_CFG, tx_buf, 1);
            if(0 != ret) {
                PRINT_ERR("hx9036_write failed\n");
                return -1;
            }
        }
        ch_enable_status |= (1 << ch_id);
        PRINT_INF("ch_enable_status=0x%02X (ch_%d enabled)\n", ch_enable_status, ch_id);
    } else {
        ch_enable_status &= ~(1 << ch_id);
        if(0 == ch_enable_status) {
            tx_buf[0] = 0x00;
            ret = hx9036_write(PAGE0_03_CH_NUM_CFG, tx_buf, 1);
            if(0 != ret) {
                PRINT_ERR("hx9036_write failed\n");
                return -1;
            }
        }
        PRINT_INF("ch_enable_status=0x%02X (ch_%d disabled)\n", ch_enable_status, ch_id);
    }
    return 0;
}

#else

static int hx9036_ch_en(uint8_t ch_id, uint8_t en)
{
    int ret = -1;
    uint8_t rx_buf[1] = {0};
    uint8_t tx_buf[1] = {0};

    en = !!en;
    if(ch_id >= HX9036_CH_NUM) {
        PRINT_ERR("channel index over range!!! ch_enable_status=0x%02X (ch_id=%d, en=%d)\n",
                  ch_enable_status, ch_id, en);
        return -1;
    }

    ret = hx9036_read(PAGE0_03_CH_NUM_CFG, rx_buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
        return -1;
    }
    ch_enable_status = rx_buf[0];

    if(1 == en) {
        if(0 == ch_enable_status) { //开启第一个ch
            hx9036_pdata.prox_state_reg = 0;
        }
        ch_enable_status |= (1 << ch_id);
        tx_buf[0] = ch_enable_status;
        ret = hx9036_write(PAGE0_03_CH_NUM_CFG, tx_buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
            return -1;
        }
        PRINT_INF("ch_enable_status=0x%02X (ch_%d enabled)\n", ch_enable_status, ch_id);
        TYHX_DELAY_US(10 * 1000);
    } else {
        ch_enable_status &= ~(1 << ch_id);
        tx_buf[0] = ch_enable_status;
        ret = hx9036_write(PAGE0_03_CH_NUM_CFG, tx_buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_write failed\n");
            return -1;
        }
        PRINT_INF("ch_enable_status=0x%02X (ch_%d disabled)\n", ch_enable_status, ch_id);
    }
    return 0;
}
#endif
//==============================================} TYHX common code end

static int hx9036_ch_en_hal(uint8_t ch_id, uint8_t enable)//yasin: for upper layer
{
    int ret = -1;

    mutex_lock(&hx9036_ch_en_mutex);
    if (1 == enable) {
        PRINT_INF("enable ch_%d(name:%s)\n", ch_id, hx9036_pdata.chs_info[ch_id].name);
        ret = hx9036_ch_en(ch_id, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_ch_en failed\n");
            mutex_unlock(&hx9036_ch_en_mutex);
            return -1;
        }
        hx9036_pdata.chs_info[ch_id].state = IDLE;
        hx9036_pdata.chs_info[ch_id].enabled = true;

#if HX9036_REPORT_EVKEY
        input_event(hx9036_pdata.input_dev_key, EV_KEY, hx9036_pdata.chs_info[ch_id].keycode, IDLE);
        input_sync(hx9036_pdata.input_dev_key);
#else
        input_report_abs(hx9036_pdata.chs_info[ch_id].input_dev_abs, ABS_DISTANCE, IDLE);
        input_sync(hx9036_pdata.chs_info[ch_id].input_dev_abs);
#endif
    } else if (0 == enable) {
        PRINT_INF("disable ch_%d(name:%s)\n", ch_id, hx9036_pdata.chs_info[ch_id].name);
        ret = hx9036_ch_en(ch_id, 0);
        if(0 != ret) {
            PRINT_ERR("hx9036_ch_en failed\n");
            mutex_unlock(&hx9036_ch_en_mutex);
            return -1;
        }
        hx9036_pdata.chs_info[ch_id].state = IDLE;
        hx9036_pdata.chs_info[ch_id].enabled = false;

#if HX9036_REPORT_EVKEY
        input_event(hx9036_pdata.input_dev_key, EV_KEY, hx9036_pdata.chs_info[ch_id].keycode, -1);
        input_sync(hx9036_pdata.input_dev_key);
#else
        input_report_abs(hx9036_pdata.chs_info[ch_id].input_dev_abs, ABS_DISTANCE, -1);
        input_sync(hx9036_pdata.chs_info[ch_id].input_dev_abs);
#endif
    } else {
        PRINT_ERR("unknown enable symbol\n");
    }

    mutex_unlock(&hx9036_ch_en_mutex);

    return 0;
}

static void hx9036_disable_irq(unsigned int irq)
{
    if(0 == irq) {
        PRINT_ERR("invalid irq number!\n");
        return;
    }

    if(1 == hx9036_irq_en_flag) {
        disable_irq_nosync(hx9036_pdata.irq);
        hx9036_irq_en_flag = 0;
        PRINT_DBG("irq_%d is disabled!\n", irq);
    } else {
        PRINT_ERR("irq_%d is disabled already!\n", irq);
    }
}

static void hx9036_enable_irq(unsigned int irq)
{
    if(0 == irq) {
        PRINT_ERR("invalid irq number!\n");
        return;
    }

    if(0 == hx9036_irq_en_flag) {
        enable_irq(hx9036_pdata.irq);
        hx9036_irq_en_flag = 1;
        PRINT_DBG("irq_%d is enabled!\n", irq);
    } else {
        PRINT_ERR("irq_%d is enabled already!\n", irq);
    }
}

#if HX9036_REPORT_EVKEY
static void hx9036_input_report_key(void)
{
    int ii = 0;
    uint8_t touch_state = 0;

    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        if (false == hx9036_pdata.chs_info[ii].enabled) { //enabled表示该通道已经被开启。
            PRINT_DBG("ch_%d(name:%s) is disabled, nothing report\n", ii, hx9036_pdata.chs_info[ii].name);
            continue;
        }
        if (false == hx9036_pdata.chs_info[ii].used) { //used表示通道有效，但此刻该通道并不一定enable
            PRINT_DBG("ch_%d(name:%s) is unused, nothing report\n", ii, hx9036_pdata.chs_info[ii].name);
            continue;
        }

        touch_state = hx9036_ch_near_state[ii];

        if (PROXACTIVE == touch_state) {
            if (hx9036_pdata.chs_info[ii].state == PROXACTIVE)
                PRINT_DBG("%s already PROXACTIVE, nothing report\n", hx9036_pdata.chs_info[ii].name);
            else {
                input_event(hx9036_pdata.input_dev_key, EV_KEY, hx9036_pdata.chs_info[ii].keycode, PROXACTIVE);
                hx9036_pdata.chs_info[ii].state = PROXACTIVE;
                PRINT_DBG("%s report PROXACTIVE\n", hx9036_pdata.chs_info[ii].name);
            }
        } else if (IDLE == touch_state) {
            if (hx9036_pdata.chs_info[ii].state == IDLE)
                PRINT_DBG("%s already released, nothing report\n", hx9036_pdata.chs_info[ii].name);
            else {
                input_event(hx9036_pdata.input_dev_key, EV_KEY, hx9036_pdata.chs_info[ii].keycode, IDLE);
                hx9036_pdata.chs_info[ii].state = IDLE;
                PRINT_DBG("%s report released\n", hx9036_pdata.chs_info[ii].name);
            }
        } else {
            PRINT_ERR("unknow touch state! touch_state=%d\n", touch_state);
        }
    }
    input_sync(hx9036_pdata.input_dev_key);
}

#else

static void hx9036_input_report_abs(void)
{
    int ii = 0;
    uint8_t touch_state = 0;

    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        if (false == hx9036_pdata.chs_info[ii].enabled) { //enabled表示该通道已经被开启。
            PRINT_DBG("ch_%d(name:%s) is disabled, nothing report\n", ii, hx9036_pdata.chs_info[ii].name);
            continue;
        }
        if (false == hx9036_pdata.chs_info[ii].used) { //used表示通道有效，但此刻该通道并不一定enable
            PRINT_DBG("ch_%d(name:%s) is unused, nothing report\n", ii, hx9036_pdata.chs_info[ii].name);
            continue;
        }

        touch_state = hx9036_ch_near_state[ii];
        //+PERIDOT-36,liushaoqing.wt,ADD,2024/03/13, add sar power reduction control switch
        #if POWER_ENABLE
            if (hx9036_pdata.power_state) {
                if (hx9036_pdata.chs_info[ii].state == IDLE) {
                    PRINT_DBG("%s [power_enable]: already released, nothing report\n", hx9036_pdata.chs_info[ii].name);
                } else {
                    __pm_wakeup_event(hx9031as_wake_lock, 1000);
                    PRINT_DBG("[power_enable]:%s report far\n", hx9036_pdata.chs_info[ii].name);

                    input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, IDLE);
                    input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);
                    hx9036_pdata.chs_info[ii].state = IDLE;
                }
                continue;
            }
        #endif 
        //-PERIDOT-36,liushaoqing.wt,ADD,2024/03/13, add sar power reduction control switch

        if (PROXACTIVE == touch_state) {
            if (hx9036_pdata.chs_info[ii].state == PROXACTIVE)
                PRINT_DBG("%s already PROXACTIVE, nothing report\n", hx9036_pdata.chs_info[ii].name);
            else {
                input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, PROXACTIVE);
                input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);
                hx9036_pdata.chs_info[ii].state = PROXACTIVE;
                PRINT_DBG("%s report PROXACTIVE\n", hx9036_pdata.chs_info[ii].name);
            }
        } else if (IDLE == touch_state) {
            if (hx9036_pdata.chs_info[ii].state == IDLE)
                PRINT_DBG("%s already released, nothing report\n", hx9036_pdata.chs_info[ii].name);
            else {
                input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, IDLE);
                input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);
                hx9036_pdata.chs_info[ii].state = IDLE;
                PRINT_DBG("%s report released\n", hx9036_pdata.chs_info[ii].name);
            }
        } else {
            PRINT_ERR("unknow touch state! touch_state=%d\n", touch_state);
        }
    }
}
#endif

// 必要DTS属性：
// "tyhx,irq-gpio"     必要！中断对应的gpio number
// "tyhx,channel-flag"  必要！每个channel对应一个input设备，每个bit对应一个channel（channel）。如0xF代表开启0，1，2，3通道
static int hx9036_parse_dt(struct device *dev)
{
    int ret = -1;
    struct device_node *dt_node = dev->of_node;

    if (NULL == dt_node) {
        PRINT_ERR("No DTS node\n");
        return -ENODEV;
    }

    hx9036_pdata.irq_gpio = of_get_named_gpio_flags(dt_node, "tyhx,irq-gpio", 0, NULL);
    if(hx9036_pdata.irq_gpio < 0) {
        PRINT_ERR("failed to get irq_gpio from DT\n");
        return -1;
    }

    PRINT_INF("hx9036_pdata.irq_gpio=%d\n", hx9036_pdata.irq_gpio);

    hx9036_pdata.channel_used_flag = 0xFF;
    ret = of_property_read_u32(dt_node, "tyhx,channel-flag", &hx9036_pdata.channel_used_flag);//客户配置：有效通道标志位：hx9036最大传入0xFF(表示8通道)
    if(ret < 0) {
        PRINT_ERR("\"tyhx,channel-flag\" is not set in DT\n");
        return -1;
    }
    if(hx9036_pdata.channel_used_flag > ((1 << HX9036_CH_NUM) - 1)) {
        PRINT_ERR("the max value of channel_used_flag is 0x%X\n", ((1 << HX9036_CH_NUM) - 1));
        return -1;
    }
    PRINT_INF("hx9036_pdata.channel_used_flag=0x%X\n", hx9036_pdata.channel_used_flag);

    return 0;
}

static int hx9036_gpio_init(void)
{
    int ret = -1;
    if (gpio_is_valid(hx9036_pdata.irq_gpio)) {
        ret = gpio_request(hx9036_pdata.irq_gpio, "hx9036_irq_gpio");
        if (ret < 0) {
            PRINT_ERR("gpio_request failed. ret=%d\n", ret);
            return ret;
        }
        ret = gpio_direction_input(hx9036_pdata.irq_gpio);
        if (ret < 0) {
            PRINT_ERR("gpio_direction_input failed. ret=%d\n", ret);
            gpio_free(hx9036_pdata.irq_gpio);
            return ret;
        }
    } else {
        PRINT_ERR("Invalid gpio num\n");
        return -1;
    }

    return 0;
}

static void hx9036_gpio_deinit(void)
{
    ENTER;
    gpio_free(hx9036_pdata.irq_gpio);
}

static void hx9036_power_on(uint8_t on)
{
    if(on) {
        //TODO: 用户自行填充
        PRINT_INF("power on\n");
    } else {
        //TODO: 用户自行填充
        PRINT_INF("power off\n");
    }
}

static void hx9036_polling_work_func(struct work_struct *work)
{
    ENTER;
    mutex_lock(&hx9036_ch_en_mutex);
    hx9036_sample();
    hx9036_get_prox_state();

#if HX9036_REPORT_EVKEY
    hx9036_input_report_key();
#else
    hx9036_input_report_abs();
#endif

    if(1 == hx9036_polling_enable)
        schedule_delayed_work(&hx9036_pdata.polling_work, msecs_to_jiffies(hx9036_polling_period_ms));
    mutex_unlock(&hx9036_ch_en_mutex);
    return;
}

static irqreturn_t hx9036_irq_handler(int irq, void *pvoid)
{
    ENTER;
    mutex_lock(&hx9036_ch_en_mutex);
    if(1 == hx9036_irq_from_suspend_flag) {
        hx9036_irq_from_suspend_flag = 0;
        PRINT_INF("delay 50ms for waiting the i2c controller enter working mode\n");
        msleep(50);//如果从suspend被中断唤醒，该延时确保i2c控制器也从休眠唤醒并进入工作状态
    }
    hx9036_sample();
    hx9036_get_prox_state();

#if HX9036_REPORT_EVKEY
    hx9036_input_report_key();
#else
    hx9036_input_report_abs();
#endif

    mutex_unlock(&hx9036_ch_en_mutex);
    return IRQ_HANDLED;
}

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^sysfs for test begin
static ssize_t hx9036_raw_data_show(struct class *class, struct class_attribute *attr, char *buf)
{
    char *p = buf;
    int ii = 0;

    ENTER;
    hx9036_sample();
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        p += snprintf(p, PAGE_SIZE, "ch[%d]: DIFF=%-8d, RAW=%-8d, OFFSET=%-8d, BL=%-8d, LP=%-8d\n",
                      ii, data_diff[ii], data_raw[ii], data_offset_dac[ii], data_bl[ii], data_lp[ii]);
    }
    return (p - buf);
}

static ssize_t hx9036_register_write_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    unsigned int reg_address = 0;
    unsigned int val = 0;
    uint16_t addr = 0;
    uint8_t tx_buf[1] = {0};

    ENTER;
    if (sscanf(buf, "%x,%x", &reg_address, &val) != 2) {
        PRINT_ERR("please input two HEX numbers: aaaa,bb (aaaa: reg_address(16bit), bb: value_to_be_set(8bit)\n");
        return -EINVAL;
    }

    addr = (uint16_t)reg_address;
    tx_buf[0] = (uint8_t)val;

    ret = hx9036_write(addr, tx_buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_write failed\n");
    }

    PRINT_INF("WRITE:Reg0x%04X=0x%02X\n", addr, tx_buf[0]);
    return count;
}

static ssize_t hx9036_register_read_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    unsigned int reg_address = 0;
    uint16_t addr = 0;
    uint8_t rx_buf[1] = {0};

    ENTER;
    if (sscanf(buf, "%x", &reg_address) != 1) {
        PRINT_ERR("please input a HEX number of 16bit for reg_addr\n");
        return -EINVAL;
    }
    addr = (uint16_t)reg_address;

    ret = hx9036_read(addr, rx_buf, 1);
    if(0 != ret) {
        PRINT_ERR("hx9036_read failed\n");
    }

    PRINT_INF("READ:Reg0x%04X=0x%02X\n", addr, rx_buf[0]);
    return count;
}

static ssize_t hx9036_channel_en_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int ch_id = 0;
    int en = 0;

    ENTER;
    if (sscanf(buf, "%d,%d", &ch_id, &en) != 2) {
        PRINT_ERR("please input two DEC numbers: ch_id,en (ch_id: channel number, en: 1=enable, 0=disable)\n");
        return -EINVAL;
    }

    if((ch_id >= HX9036_CH_NUM) || (ch_id < 0)) {
        PRINT_ERR("channel number out of range, the effective number is 0~%d\n", HX9036_CH_NUM - 1);
        return -EINVAL;
    }

    if ((hx9036_pdata.channel_used_flag >> ch_id) & 0x01) {
        hx9036_ch_en_hal(ch_id, (en > 0) ? 1 : 0);
    } else {
        PRINT_ERR("ch_%d is unused, you can not enable or disable an unused channel\n", ch_id);
    }

    return count;
}

static ssize_t hx9036_channel_en_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int ii = 0;
    char *p = buf;

    ENTER;
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        if ((hx9036_pdata.channel_used_flag >> ii) & 0x1) {
            PRINT_INF("hx9036_pdata.chs_info[%d].enabled=%d\n", ii, hx9036_pdata.chs_info[ii].enabled);
            p += snprintf(p, PAGE_SIZE, "hx9036_pdata.chs_info[%d].enabled=%d\n", ii, hx9036_pdata.chs_info[ii].enabled);
        }
    }

    return (p - buf);
}

static ssize_t hx9036_manual_offset_calibration_show(struct class *class, struct class_attribute *attr, char *buf)
{
    hx9036_read_offset_dac();
    return sprintf(buf, "OFFSET,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d,%-8d\n",
                   data_offset_dac[0], data_offset_dac[1], data_offset_dac[2], data_offset_dac[3],
                   data_offset_dac[4], data_offset_dac[5], data_offset_dac[6], data_offset_dac[7]);
}

static ssize_t hx9036_manual_offset_calibration_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned long val;

    ENTER;
    if (kstrtoul(buf, 10, &val)) {
        PRINT_ERR("Invalid Argument\n");
        return -EINVAL;
    }

    if (0 != val)
        hx9036_manual_offset_calibration();
    else
        PRINT_ERR("echo 1 to calibrate\n");
    return count;
}

static ssize_t hx9036_prox_state_show(struct class *class, struct class_attribute *attr, char *buf)
{
    hx9036_get_prox_state();
    return sprintf(buf, "hx9036_ch_near_state[0~7]:%d-%d-%d-%d-%d-%d-%d-%d\n",
                   hx9036_ch_near_state[0],
                   hx9036_ch_near_state[1],
                   hx9036_ch_near_state[2],
                   hx9036_ch_near_state[3],
                   hx9036_ch_near_state[4],
                   hx9036_ch_near_state[5],
                   hx9036_ch_near_state[6],
                   hx9036_ch_near_state[7]);
}

static ssize_t hx9036_polling_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int value = 0;
    int ret = -1;

    ENTER;
    ret = kstrtoint(buf, 10, &value);
    if (0 != ret) {
        PRINT_ERR("kstrtoint failed\n");
        goto exit;
    }

    if (value >= 10) {
        hx9036_polling_period_ms = value;
        if(1 == hx9036_polling_enable) {
            PRINT_INF("polling is already enabled!, no need to do enable again!, just update the polling period\n");
            goto exit;
        }

        hx9036_polling_enable = 1;
        hx9036_disable_irq(hx9036_pdata.irq);

        PRINT_INF("polling started! period=%dms\n", hx9036_polling_period_ms);
        schedule_delayed_work(&hx9036_pdata.polling_work, msecs_to_jiffies(hx9036_polling_period_ms));
    } else {
        if(0 == hx9036_polling_enable) {
            PRINT_INF("polling is already disabled!, no need to do again!\n");
            goto exit;
        }
        hx9036_polling_period_ms = 0;
        hx9036_polling_enable = 0;
        PRINT_INF("polling stoped!\n");

        cancel_delayed_work(&hx9036_pdata.polling_work);//停止polling对应的工作队列，并重新开启中断模式
        hx9036_enable_irq(hx9036_pdata.irq);
    }

exit:
    return count;
}

static ssize_t hx9036_polling_show(struct class *class, struct class_attribute *attr, char *buf)
{
    PRINT_INF("hx9036_polling_enable=%d hx9036_polling_period_ms=%d\n", hx9036_polling_enable, hx9036_polling_period_ms);
    return sprintf(buf, "hx9036_polling_enable=%d hx9036_polling_period_ms=%d\n", hx9036_polling_enable, hx9036_polling_period_ms);
}

static ssize_t hx9036_loglevel_show(struct class *class, struct class_attribute *attr, char *buf)
{
    PRINT_INF("tyhx_log_level=%d\n", tyhx_log_level);
    return sprintf(buf, "tyhx_log_level=%d\n", tyhx_log_level);
}

static ssize_t hx9036_loglevel_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    int value = 0;

    ret = kstrtoint(buf, 10, &value);
    if (0 != ret) {
        PRINT_ERR("kstrtoint failed\n");
        return count;
    }

    tyhx_log_level = value;
    PRINT_INF("set tyhx_log_level=%d\n", tyhx_log_level);
    return count;
}

static ssize_t hx9036_accuracy_show(struct class *class, struct class_attribute *attr, char *buf)
{
    PRINT_INF("hx9036_data_accuracy=%d\n", hx9036_data_accuracy);
    return sprintf(buf, "hx9036_data_accuracy=%d\n", hx9036_data_accuracy);
}

static ssize_t hx9036_accuracy_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    int value = 0;

    ret = kstrtoint(buf, 10, &value);
    if (0 != ret) {
        PRINT_ERR("kstrtoint failed\n");
        return count;
    }

    hx9036_data_accuracy = (24 == value) ? 24 : 16;
    PRINT_INF("set hx9036_data_accuracy=%d\n", hx9036_data_accuracy);
    return count;
}

static ssize_t hx9036_threshold1_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    unsigned int ch_id = 0;
    unsigned int thr_near = 0;

    ENTER;
    if (sscanf(buf, "%d,%d", &ch_id, &thr_near) != 2) {
        PRINT_ERR("please input 2 numbers in DEC: ch_id,thr_near(eg: 0,512)\n");
        return -EINVAL;
    }

    if((ch_id >= HX9036_CH_NUM) || (ch_id < 0)) {
        PRINT_ERR("channel number out of range, the effective number is 0~%d\n", HX9036_CH_NUM - 1);
        return -EINVAL;
    }

    PRINT_INF("set threshold1: ch_id=%d, thr_near=%d\n", ch_id, thr_near);
    hx9036_set_thres1_near(ch_id, thr_near);

    return count;
}

static ssize_t hx9036_threshold1_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int ii = 0;
    char *p = buf;

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        hx9036_get_thres1_near(ii);
        hx9036_get_thres1_far(ii);
        PRINT_INF("ch_%d threshold1: near=%d, far=%d\n", ii, hx9036_ch_thres1[ii].thr_near, hx9036_ch_thres1[ii].thr_far);
        p += snprintf(p, PAGE_SIZE, "ch_%d threshold1: near=%d, far=%d\n", ii, hx9036_ch_thres1[ii].thr_near, hx9036_ch_thres1[ii].thr_far);
    }

    return (p - buf);
}

static ssize_t hx9036_dump_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int ret = -1;
    int ii = 0;
    char *p = buf;
    uint8_t rx_buf[1] = {0};

    for(ii = 0; ii < ARRAY_SIZE(hx9036_reg_init_list); ii++) {
        ret = hx9036_read(hx9036_reg_init_list[ii].addr, rx_buf, 1);
        if(0 != ret) {
            PRINT_ERR("hx9036_read failed\n");
        }
        PRINT_INF("0x%04X=0x%02X\n", hx9036_reg_init_list[ii].addr, rx_buf[0]);
        p += snprintf(p, PAGE_SIZE, "0x%04X=0x%02X\n", hx9036_reg_init_list[ii].addr, rx_buf[0]);
    }

    p += snprintf(p, PAGE_SIZE, "driver version:%s\n", HX9036_DRIVER_VER);
    return (p - buf);
}

static ssize_t hx9036_offset_dac_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int ii = 0;
    char *p = buf;

    hx9036_read_offset_dac();

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        PRINT_INF("data_offset_dac[%d]=%dpF\n", ii, data_offset_dac[ii] * 26 / 1000);
        p += snprintf(p, PAGE_SIZE, "ch[%d]=%dpF ", ii, data_offset_dac[ii] * 26 / 1000);
    }
    p += snprintf(p, PAGE_SIZE, "\n");

    return (p - buf);
}

static ssize_t hx9036_offset_csn_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int ii = 0;
    char *p = buf;

    hx9036_read_offset_dac();

    data_offset_csn[0] = data_offset_dac[0] + data_offset_dac[3];
    data_offset_csn[1] = data_offset_dac[3];
    data_offset_csn[2] = 0;
    data_offset_csn[3] = 0;
    data_offset_csn[4] = data_offset_dac[2] + data_offset_dac[5];
    data_offset_csn[5] = data_offset_dac[4];
    data_offset_csn[6] = data_offset_dac[1] + data_offset_dac[4];
    data_offset_csn[7] = data_offset_dac[5];

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        PRINT_INF("data_offset_csn[%d]=%dpF\n", ii, data_offset_csn[ii] * 26 / 1000);
        p += snprintf(p, PAGE_SIZE, "data_offset_csn[%d]=%dpF\n", ii, data_offset_csn[ii] * 26 / 1000);
    }
    p += snprintf(p, PAGE_SIZE, "\n");

    return (p - buf);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
static struct class_attribute class_attr_raw_data = __ATTR(raw_data, 0664, hx9036_raw_data_show, NULL);
static struct class_attribute class_attr_reg_write = __ATTR(reg_write,  0664, NULL, hx9036_register_write_store);
static struct class_attribute class_attr_reg_read = __ATTR(reg_read, 0664, NULL, hx9036_register_read_store);
static struct class_attribute class_attr_channel_en = __ATTR(channel_en, 0664, hx9036_channel_en_show, hx9036_channel_en_store);
static struct class_attribute class_attr_calibrate = __ATTR(calibrate, 0664, hx9036_manual_offset_calibration_show, hx9036_manual_offset_calibration_store);
static struct class_attribute class_attr_prox_state = __ATTR(prox_state, 0664, hx9036_prox_state_show, NULL);
static struct class_attribute class_attr_polling_period = __ATTR(polling_period, 0664, hx9036_polling_show, hx9036_polling_store);
static struct class_attribute class_attr_loglevel = __ATTR(loglevel, 0664, hx9036_loglevel_show, hx9036_loglevel_store);
static struct class_attribute class_attr_accuracy = __ATTR(accuracy, 0664, hx9036_accuracy_show, hx9036_accuracy_store);
static struct class_attribute class_attr_dump = __ATTR(dump, 0664, hx9036_dump_show, NULL);
static struct class_attribute class_attr_offset_dac = __ATTR(offset_dac, 0664, hx9036_offset_dac_show, NULL);
static struct class_attribute class_attr_offset_csn = __ATTR(offset_csn, 0664, hx9036_offset_csn_show, NULL);
static struct class_attribute class_attr_threshold1 = __ATTR(threshold1, 0664, hx9036_threshold1_show, hx9036_threshold1_store);

static struct attribute *hx9036_class_attrs[] = {
    &class_attr_raw_data.attr,
    &class_attr_reg_write.attr,
    &class_attr_reg_read.attr,
    &class_attr_channel_en.attr,
    &class_attr_calibrate.attr,
    &class_attr_prox_state.attr,
    &class_attr_polling_period.attr,
    &class_attr_loglevel.attr,
    &class_attr_accuracy.attr,
    &class_attr_dump.attr,
    &class_attr_offset_dac.attr,
    &class_attr_offset_csn.attr,
    &class_attr_threshold1.attr,
    NULL,
};
ATTRIBUTE_GROUPS(hx9036_class);
#else
static struct class_attribute hx9036_class_attributes[] = {
    __ATTR(raw_data, 0664, hx9036_raw_data_show, NULL),
    __ATTR(reg_write,  0664, NULL, hx9036_register_write_store),
    __ATTR(reg_read, 0664, NULL, hx9036_register_read_store),
    __ATTR(channel_en, 0664, hx9036_channel_en_show, hx9036_channel_en_store),
    __ATTR(calibrate, 0664, hx9036_manual_offset_calibration_show, hx9036_manual_offset_calibration_store),
    __ATTR(prox_state, 0664, hx9036_prox_state_show, NULL),
    __ATTR(polling_period, 0664, hx9036_polling_show, hx9036_polling_store),
    __ATTR(loglevel, 0664, hx9036_loglevel_show, hx9036_loglevel_store),
    __ATTR(accuracy, 0664, hx9036_accuracy_show, hx9036_accuracy_store),
    __ATTR(dump, 0664, hx9036_dump_show, NULL),
    __ATTR(offset_dac, 0664, hx9036_offset_dac_show, NULL),
    __ATTR(offset_csn, 0664, hx9036_offset_csn_show, NULL),
    __ATTR(threshold1, 0664, hx9036_threshold1_show, hx9036_threshold1_store),
    __ATTR_NULL,
};
#endif

struct class hx9036_class = {
        .name = "hx9036",
        .owner = THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
        .class_groups = hx9036_class_groups,
#else
        .class_attrs = hx9036_class_attributes,
#endif
    };
/* +++for WT factory use,liushaoqing.wt,add,2 */
//+PERIDOT-36,liushaoqing.wt,ADD,2024/03/13, add sar power reduction control switch
#if POWER_ENABLE
static ssize_t power_enable_show(struct class *class,
                                 struct class_attribute *attr,
                                 char *buf)
{
    return sprintf(buf, "%d\n", hx9036_pdata.power_state);
}

static ssize_t power_enable_store(struct class *class,
                                  struct class_attribute *attr,
                                  const char *buf, size_t count)
{
    int ret = -1;
 
    ret = kstrtoint(buf, 10, &hx9036_pdata.power_state);
    if (0 != ret) {
        PRINT_ERR("hx9036: kstrtoint failed\n");
    }
    //PERIDOT-36,liushaoqing.wt,ADD,2024/03/26, add sar hx9036_ch_en_hal
    hx9036_ch_en_hal(1, 1);
    hx9036_ch_en_hal(3, 1);
    hx9036_ch_en_hal(5, 1);
    hx9036_ch_en_hal(7, 1);
    return count;
}

static CLASS_ATTR_RW(power_enable);

#endif

//-PERIDOT-36,liushaoqing.wt,ADD,2024/03/13, add sar power reduction control switch

static ssize_t ch1_cap_diff_dump_show(struct class *class,
        struct class_attribute *attr,
        char *buf)
{
ssize_t len = 0;
    uint32_t cap_reg = 0;
    ENTER;
 
    hx9036_sample();
 
    //CH1 background cap
    cap_reg = data_offset_dac[1];
    PRINT_ERR("CH1_background_cap=%d\n", cap_reg);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH1_background_cap=%d;", cap_reg);
 
    //CH1 refer channel cap
    cap_reg = data_offset_dac[0];
    PRINT_ERR("CH1_refer_channel_cap=%d\n", cap_reg);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH1_refer_channel_cap=%d;", cap_reg);
 
    //CH1 diff
    len += snprintf(buf+len, PAGE_SIZE-len, "CH1_diff=%d",data_diff[1]);

    return (len);//return numbers of parameters
 
} 
static CLASS_ATTR_RO(ch1_cap_diff_dump);
 
static ssize_t ch3_cap_diff_dump_show(struct class *class,
        struct class_attribute *attr,
        char *buf)
{
ssize_t len = 0;
    uint32_t cap_reg = 0;
    ENTER;
 
    hx9036_sample();
 
   //CH3 background cap
    cap_reg = data_offset_dac[3];
    PRINT_ERR("CH3_background_cap=%d\n", cap_reg);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH3_background_cap=%d;", cap_reg);
 
    //CH3 refer channel cap
    cap_reg = data_offset_dac[3];
    PRINT_ERR("CH3_refer_channel_cap=%d\n", 0);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH3_refer_channel_cap=%d;", cap_reg);
 
    //CH3 diff
    len += snprintf(buf+len, PAGE_SIZE-len, "CH3_diff=%d",data_diff[3]);
 
    return (len);//return numbers of parameters
 
} 
static CLASS_ATTR_RO(ch3_cap_diff_dump);

static ssize_t ch5_cap_diff_dump_show(struct class *class,
        struct class_attribute *attr,
        char *buf)
{
ssize_t len = 0;
    uint32_t cap_reg = 0;
    ENTER;
 
    hx9036_sample();

   //CH5 background cap
    cap_reg = data_offset_dac[5];
    PRINT_ERR("CH5_background_cap=%d\n", cap_reg);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH5_background_cap=%d;", cap_reg);
 
    //CH5 refer channel cap
    cap_reg = data_offset_dac[4];
    PRINT_ERR("CH5_refer_channel_cap=%d\n", 0);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH5_refer_channel_cap=%d;", cap_reg);
 
    //CH5 diff
    len += snprintf(buf+len, PAGE_SIZE-len, "CH5_diff=%d",data_diff[5]);

    return (len);//return numbers of parameters
 
} 
static CLASS_ATTR_RO(ch5_cap_diff_dump);

static ssize_t ch7_cap_diff_dump_show(struct class *class,
        struct class_attribute *attr,
        char *buf)
{
ssize_t len = 0;
    uint32_t cap_reg = 0;
    ENTER;
 
    hx9036_sample();

   //CH7 background cap
    cap_reg = data_offset_dac[7];
    PRINT_ERR("CH7_background_cap=%d\n", cap_reg);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH7_background_cap=%d;", cap_reg);
 
    //CH7 refer channel cap
    cap_reg = data_offset_dac[6];
    PRINT_ERR("CH7_refer_channel_cap=%d\n", 0);
len += snprintf(buf+len, PAGE_SIZE-len,
"CH7_refer_channel_cap=%d;", cap_reg);
 
    //CH7 diff
    len += snprintf(buf+len, PAGE_SIZE-len, "CH7_diff=%d",data_diff[7]);
 
    return (len);//return numbers of parameters
 
} 
static CLASS_ATTR_RO(ch7_cap_diff_dump);

static struct class capsense_class = {
    .name = "capsense",
    .owner = THIS_MODULE,
};
/* ---for WT factory use,liushaoqing.wt,add,2 */
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^sysfs for test end

static DEFINE_MUTEX(hx9031a_i2c_rw_mutex);
static DEFINE_MUTEX(hx9031a_ch_en_mutex);
static DEFINE_MUTEX(hx9031a_cali_mutex);


static int hx9031a_set_enable(struct sensors_classdev *sensors_cdev, unsigned int enable)
{
    int ret = -1;
    int ii = 0;
    PRINT_INF("hx9031a_set_enable\n");
    mutex_lock(&hx9031a_ch_en_mutex);
    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        if (strcmp(sensors_cdev->name, hx9036_pdata.chs_info[ii].name) == 0) {
            if (1 == enable) {
                PRINT_INF("enable ch_%d(name:%s)\n", ii, sensors_cdev->name);
                //+PERIDOT-36  liushaoqing.wt add 20240304 set enable sar channel 
                if(ii == 1){
                    ret = hx9036_ch_en(0, 1);
                } else if (ii == 5){
                    ret = hx9036_ch_en(4, 1);
                }else if (ii == 7){
                    ret = hx9036_ch_en(6, 1);
                }
                //-PERIDOT-36  liushaoqing.wt add 20240304 set enable sar channel
                ret = hx9036_ch_en(ii, 1);
                if(0 != ret) {
                    PRINT_ERR("hx9036_ch_en failed\n");
                    mutex_unlock(&hx9031a_ch_en_mutex);
                    return -1;
                }
                hx9036_pdata.chs_info[ii].state = IDLE;
                hx9036_pdata.chs_info[ii].enabled = true;
#ifdef CONFIG_PM_WAKELOCKS
                __pm_wakeup_event(hx9031as_wake_lock, 1000);
#else
                wake_lock_timeout(&hx9031as_wake_lock, HZ * 1);
#endif
                input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, IDLE);
                input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);
                //hx9031a_manual_offset_calibration(ii);//enable(en:0x24)的时候会自动校准，故此处不需要。
            } else if (0 == enable) {
                PRINT_INF("disable ch_%d(name:%s)\n", ii, sensors_cdev->name);
                //+PERIDOT-36  liushaoqing.wt add 20240304 set enable sar channel
                if(ii == 1){
                    ret = hx9036_ch_en(0, 0);
                } else if (ii == 5){
                    ret = hx9036_ch_en(4, 0);
                }else if (ii == 7){
                    ret = hx9036_ch_en(6, 0);
                }
                //-PERIDOT-36  liushaoqing.wt add 20240304 set enable sar channel
                ret = hx9036_ch_en(ii, 0);
                if(0 != ret) {
                    PRINT_ERR("hx9036_ch_en failed\n");
                    mutex_unlock(&hx9031a_ch_en_mutex);
                    return -1;
                }
                hx9036_pdata.chs_info[ii].state = IDLE;
                hx9036_pdata.chs_info[ii].enabled = false;
#ifdef CONFIG_PM_WAKELOCKS
                __pm_wakeup_event(hx9031as_wake_lock, 1000);
#else
                wake_lock_timeout(&hx9031as_wake_lock, HZ * 1);
#endif
                input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, IDLE);
                input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);
            } else {
                PRINT_ERR("unknown enable symbol\n");
            }
        }
    }
    mutex_unlock(&hx9031a_ch_en_mutex);

    return 0;
}
#if HX9036_REPORT_EVKEY
static int hx9036_input_init_key(struct i2c_client *client)
{
    int ii = 0;
    int ret = -1;

    hx9036_pdata.chs_info = devm_kzalloc(&client->dev,
                                         sizeof(struct hx9036_channel_info) * HX9036_CH_NUM,
                                         GFP_KERNEL);
    if (NULL == hx9036_pdata.chs_info) {
        PRINT_ERR("devm_kzalloc failed\n");
        ret = -ENOMEM;
        goto failed_devm_kzalloc;
    }

    hx9036_pdata.input_dev_key = input_allocate_device();
    if (NULL == hx9036_pdata.input_dev_key) {
        PRINT_ERR("input_allocate_device failed\n");
        ret = -ENOMEM;
        goto failed_input_allocate_device;
    }

    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        snprintf(hx9036_pdata.chs_info[ii].name,
                 sizeof(hx9036_pdata.chs_info[ii].name),
                 "hx9036_key_ch%d",
                 ii);
        PRINT_DBG("name of ch_%d:\"%s\"\n", ii, hx9036_pdata.chs_info[ii].name);
        hx9036_pdata.chs_info[ii].used = false;
        hx9036_pdata.chs_info[ii].enabled = false;
        hx9036_pdata.chs_info[ii].keycode = KEY_1 + ii;
        if ((hx9036_pdata.channel_used_flag >> ii) & 0x1) {
            hx9036_pdata.chs_info[ii].used = true;
            hx9036_pdata.chs_info[ii].state = IDLE;
            __set_bit(hx9036_pdata.chs_info[ii].keycode, hx9036_pdata.input_dev_key->keybit);
        }
    }

    hx9036_pdata.input_dev_key->name = HX9036_DRIVER_NAME;
    __set_bit(EV_KEY, hx9036_pdata.input_dev_key->evbit);
    ret = input_register_device(hx9036_pdata.input_dev_key);
    if(ret) {
        PRINT_ERR("input_register_device failed\n");
        goto failed_input_register_device;
    }

    PRINT_INF("input init success\n");
    return ret;

failed_input_register_device:
    input_free_device(hx9036_pdata.input_dev_key);
failed_input_allocate_device:
    devm_kfree(&client->dev, hx9036_pdata.chs_info);
failed_devm_kzalloc:
    PRINT_ERR("hx9036_input_init_key failed\n");
    return ret;
}

static void hx9036_input_deinit_key(struct i2c_client *client)
{
    ENTER;
    input_unregister_device(hx9036_pdata.input_dev_key);
    input_free_device(hx9036_pdata.input_dev_key);
    devm_kfree(&client->dev, hx9036_pdata.chs_info);
}

#else

static int hx9036_input_init_abs(struct i2c_client *client)
{
    int ii = 0;
    int jj = 0;
    int ret = -1;

    hx9036_pdata.chs_info = devm_kzalloc(&client->dev,
                                         sizeof(struct hx9036_channel_info) * HX9036_CH_NUM,
                                         GFP_KERNEL);
    if (NULL == hx9036_pdata.chs_info) {
        PRINT_ERR("devm_kzalloc failed\n");
        ret = -ENOMEM;
        goto failed_devm_kzalloc;
    }

    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        snprintf(hx9036_pdata.chs_info[ii].name,
                 sizeof(hx9036_pdata.chs_info[ii].name),
                 "hx9036_abs_ch%d",
                 ii);
        PRINT_DBG("name of ch_%d:\"%s\"\n", ii, hx9036_pdata.chs_info[ii].name);
        hx9036_pdata.chs_info[ii].used = true;
        hx9036_pdata.chs_info[ii].enabled = true;

        hx9036_pdata.chs_info[ii].input_dev_abs = input_allocate_device();
        if (NULL == hx9036_pdata.chs_info[ii].input_dev_abs) {
            PRINT_ERR("input_allocate_device failed, ii=%d\n", ii);
            ret = -ENOMEM;
            goto failed_input_allocate_device;
        }

        hx9036_pdata.chs_info[ii].input_dev_abs->name = hx9036_pdata.chs_info[ii].name;
        __set_bit(EV_ABS, hx9036_pdata.chs_info[ii].input_dev_abs->evbit);
        input_set_abs_params(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, -1, 100, 0, 0);

        ret = input_register_device(hx9036_pdata.chs_info[ii].input_dev_abs);
        if (ret) {
            PRINT_ERR("input_register_device failed, ii=%d\n", ii);
            goto failed_input_register_device;
        }

        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.sensors_enable = hx9031a_set_enable;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.sensors_poll_delay = NULL;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.name = hx9036_pdata.chs_info[ii].name;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.vendor = "HX9036E";
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.version = 1;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.type = SENSOR_TYPE_ABOV_CAPSENSE;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.max_range = "5";
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.resolution = "5.0";
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.sensor_power = "3";
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.min_delay = 0;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.fifo_reserved_event_count = 0;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.fifo_max_event_count = 0;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.delay_msec = 100;
        hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev.enabled = 0;
        hx9036_pdata.chs_info[ii].enabled = false;

        ret = sensors_classdev_register(&hx9036_pdata.chs_info[ii].input_dev_abs->dev, &hx9036_pdata.chs_info[ii].hx9031a_sensors_classdev);
            if (ret < 0) {
                PRINT_ERR("create %d cap sensor_class  file failed (%d)\n", ii, ret);
            }

        input_report_abs(hx9036_pdata.chs_info[ii].input_dev_abs, ABS_DISTANCE, -1);
        input_sync(hx9036_pdata.chs_info[ii].input_dev_abs);

        if ((hx9036_pdata.channel_used_flag >> ii) & 0x1) {
            hx9036_pdata.chs_info[ii].used = true;
            hx9036_pdata.chs_info[ii].state = IDLE;
        }
    }

    PRINT_INF("input init success\n");
    return ret;

failed_input_register_device:
    for(jj = ii - 1; jj >= 0; jj--) {
        input_unregister_device(hx9036_pdata.chs_info[jj].input_dev_abs);
    }
    ii++;
failed_input_allocate_device:
    for(jj = ii - 1; jj >= 0; jj--) {
        input_free_device(hx9036_pdata.chs_info[jj].input_dev_abs);
    }
    devm_kfree(&client->dev, hx9036_pdata.chs_info);
failed_devm_kzalloc:
    PRINT_ERR("hx9036_input_init_abs failed\n");
    return ret;
}

static void hx9036_input_deinit_abs(struct i2c_client *client)
{
    int ii = 0;

    ENTER;
    for (ii = 0; ii < HX9036_CH_NUM; ii++) {
        input_unregister_device(hx9036_pdata.chs_info[ii].input_dev_abs);
        input_free_device(hx9036_pdata.chs_info[ii].input_dev_abs);
    }
    devm_kfree(&client->dev, hx9036_pdata.chs_info);
}
#endif

static int hx9036_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ii = 0;
    int ret = 0;
    char firmware_ver[HARDWARE_MAX_ITEM_LONGTH];

    PRINT_INF("client->addr=0x%02X\n", client->addr);
    if (!i2c_check_functionality(to_i2c_adapter(client->dev.parent), I2C_FUNC_SMBUS_READ_WORD_DATA)) {
        PRINT_ERR("i2c_check_functionality failed\n");
        ret = -EIO;
        goto failed_i2c_check_functionality;
    }

    i2c_set_clientdata(client, &hx9036_pdata);
    hx9036_i2c_client = client;
    hx9036_pdata.pdev = &client->dev;
    client->dev.platform_data = &hx9036_pdata;

//{begin =============================================需要客户自行配置dts属性和实现上电相关内容
   
    ret = hx9036_parse_dt(&client->dev);//yasin: power, irq, regs
    if (ret) {
        PRINT_ERR("hx9036_parse_dt failed\n");
        ret = -ENODEV;
        goto failed_parse_dt;
    }

    ret = hx9036_gpio_init();
    if (ret) {
        PRINT_ERR("hx9036_gpio_init failed\n");
        ret = -1;
        goto failed_gpio_init;
    }

    hx9036_pdata.irq = gpio_to_irq(hx9036_pdata.irq_gpio);
    client->irq = hx9036_pdata.irq;
    PRINT_INF("hx9036_pdata.irq_gpio=%d hx9036_pdata.irq=%d\n", hx9036_pdata.irq_gpio, hx9036_pdata.irq);

    hx9036_power_on(1);
//}end =============================================================

    ret = hx9036_id_check();
    if(0 != ret) {
        PRINT_ERR("hx9036_id_check failed\n");
        goto failed_id_check;
    }
    hx9036_reg_init();
    hx9036_capcoe_init();
    hx9036_ch_cfg();
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        hx9036_set_thres1_near(ii, hx9036_ch_thres1[ii].thr_near);
    }

    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        hx9036_get_thres1_near(ii);
    }
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        hx9036_get_thres1_far(ii);
    }

    INIT_DELAYED_WORK(&hx9036_pdata.polling_work, hx9036_polling_work_func);

#if HX9036_REPORT_EVKEY
    ret = hx9036_input_init_key(client);
    if(0 != ret) {
        PRINT_ERR("hx9036_input_init_key failed\n");
        goto failed_input_init;
    }
#else
    ret = hx9036_input_init_abs(client);
    if(0 != ret) {
        PRINT_ERR("hx9036_input_init_abs failed\n");
        goto failed_input_init;
    }
#endif

    ret = class_register(&hx9036_class);//debug fs path:/sys/class/hx9036/*
    if (ret < 0) {
        PRINT_ERR("class_register failed\n");
        goto failed_class_register;
    }

    // for WT factory
    ret = class_register(&capsense_class);
    PRINT_INF("capsense_class has been registered\n");
    if (ret < 0) {
        PRINT_ERR("register capsense class failed \n");
        return ret;
    }
 
    ret = class_create_file(&capsense_class, &class_attr_ch1_cap_diff_dump);
    if (ret < 0) {
        PRINT_ERR("Create ch1_cap_diff_dump file failed \n");
        return ret;
    }

    ret = class_create_file(&capsense_class, &class_attr_ch3_cap_diff_dump);
    if (ret < 0) {
        PRINT_ERR("Create ch3_cap_diff_dump file failed \n");
        return ret;
    }

    ret = class_create_file(&capsense_class, &class_attr_ch5_cap_diff_dump);
    if (ret < 0) {
        PRINT_ERR("Create ch5_cap_diff_dump file failed \n");
        return ret;
    }

    ret = class_create_file(&capsense_class, &class_attr_ch7_cap_diff_dump);
    if (ret < 0) {
        PRINT_ERR("Create ch7_cap_diff_dump file failed \n");
        return ret;
    }

    ret = class_create_file(&capsense_class, &class_attr_power_enable);
    if (ret < 0) {
        PRINT_ERR("Create power_enable file failed \n");
        return ret;
    }
    //end WT factory
    ret = request_threaded_irq(hx9036_pdata.irq, NULL, hx9036_irq_handler,
                               IRQF_TRIGGER_FALLING | IRQF_ONESHOT | IRQF_NO_SUSPEND,
                               hx9036_pdata.pdev->driver->name, (&hx9036_pdata));
    if(ret < 0) {
        PRINT_ERR("request_irq failed irq=%d ret=%d\n", hx9036_pdata.irq, ret);
        goto failed_request_irq;
    }
    enable_irq_wake(hx9036_pdata.irq);//enable irq wakeup PM
    hx9036_irq_en_flag = 1;//irq is enabled after request by default

#if HX9036_TEST_CHS_EN
    PRINT_INF("enable all channels for test\n");
    for(ii = 0; ii < HX9036_CH_NUM; ii++) {
        if ((hx9036_pdata.channel_used_flag >> ii) & 0x01) {
            hx9036_ch_en_hal(ii, 1);
        }
    }
#endif
    snprintf(firmware_ver,HARDWARE_MAX_ITEM_LONGTH, "HX9036E");
    hardwareinfo_set_prop(HARDWARE_SAR,firmware_ver);

    PRINT_INF("probe success\n");
    return 0;

failed_request_irq:
    class_unregister(&hx9036_class);
failed_class_register:
#if HX9036_REPORT_EVKEY
    hx9036_input_deinit_key(client);
#else
    hx9036_input_deinit_abs(client);
#endif
failed_input_init:
    cancel_delayed_work_sync(&(hx9036_pdata.polling_work));
failed_id_check:
    hx9036_power_on(0);
    hx9036_gpio_deinit();
failed_gpio_init:
failed_parse_dt:
failed_i2c_check_functionality:
    PRINT_ERR("probe failed\n");
    return ret;

}

static void hx9036_remove(struct i2c_client *client)
{
    ENTER;
    free_irq(hx9036_pdata.irq, &hx9036_pdata);
    class_unregister(&hx9036_class);
#if HX9036_REPORT_EVKEY
    hx9036_input_deinit_key(client);
#else
    hx9036_input_deinit_abs(client);
#endif
    cancel_delayed_work_sync(&(hx9036_pdata.polling_work));
    hx9036_power_on(0);
    hx9036_gpio_deinit();
    //return 0;
}

static int hx9036_suspend(struct device *dev)
{
    ENTER;
    hx9036_irq_from_suspend_flag = 1;
    return 0;
}

static int hx9036_resume(struct device *dev)
{
    ENTER;
    hx9036_irq_from_suspend_flag = 0;
    return 0;
}

static struct i2c_device_id hx9036_i2c_id_table[] = {
    {HX9036_DRIVER_NAME, 0},
    {}
};

MODULE_DEVICE_TABLE(i2c, hx9036_i2c_id_table);
#ifdef CONFIG_OF
static struct of_device_id hx9036_of_match_table[] = {
    {.compatible = "tyhx,hx9036"},
    { },
};
#else
#define hx9036_of_match_table NULL
#endif

static const struct dev_pm_ops hx9036_pm_ops = {
    .suspend = hx9036_suspend,
    .resume = hx9036_resume,
};

static struct i2c_driver hx9036_i2c_driver = {
    .driver = {
        .owner = THIS_MODULE,
        .name = HX9036_DRIVER_NAME,
        .of_match_table = hx9036_of_match_table,
        .pm = &hx9036_pm_ops,
    },
    .id_table = hx9036_i2c_id_table,
    .probe = hx9036_probe,
    .remove = hx9036_remove,
};

static int __init hx9036_module_init(void)
{
    ENTER;
    PRINT_INF("driver version:%s\n", HX9036_DRIVER_VER);
    return i2c_add_driver(&hx9036_i2c_driver);
}

static void __exit hx9036_module_exit(void)
{
    ENTER;
    i2c_del_driver(&hx9036_i2c_driver);
}

module_init(hx9036_module_init);
module_exit(hx9036_module_exit);

MODULE_AUTHOR("Yasin Lee <yasin.lee.x@gmail.com><yasin.lee@tianyihexin.com>");
MODULE_DESCRIPTION("Driver for NanJingTianYiHeXin HX9036 Cap Sense");
MODULE_ALIAS("sar driver");
MODULE_LICENSE("GPL v2");
