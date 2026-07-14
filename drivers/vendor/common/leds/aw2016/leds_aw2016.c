/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include "leds_aw2016.h"

/* add zte boardtest interface start */
#include <vendor/common/zte_misc.h>
static uint8_t is_breath_led_exist = 0;

int is_breath_led_exist_get(char *val, const void *arg)
{
	return snprintf(val, PAGE_SIZE, "%d", is_breath_led_exist);
}

static struct zte_misc_ops is_breath_led_exist_node = {
	.node_name = "is_breath_led_exist",
	.set = NULL,
	.get = is_breath_led_exist_get,
	.free = NULL,
	.arg = NULL,
};
/* zte boardtest interface end */

static int aw2016_write(struct aw2016_led *led, u8 reg, u8 val)
{
    int ret = -EINVAL, retry_times = 0;

    do {
        ret = i2c_smbus_write_byte_data(led->client, reg, val);
        retry_times ++;
        if(retry_times == 5)
            break;
    } while (ret < 0);

    if (ret < 0) {
        AW_LOG("%s reg 0x%x write 0x%x failed!\n", __func__, reg, val);
        return ret;
    }

#ifdef DEBUG
    AW_LOG("%s reg 0x%x write 0x%x succeed!\n", __func__, reg, val);
#endif
    return 0;
}

static int aw2016_read(struct aw2016_led *led, u8 reg, u8 *val)
{
    int ret = -EINVAL, retry_times = 0;

    do {
        ret = i2c_smbus_read_byte_data(led->client, reg);
        retry_times ++;
        if(retry_times == 5)
            break;
    } while (ret < 0);

    if (ret < 0) {
        AW_LOG("%s reg 0x%x read failed!\n", __func__, reg);
        return ret;
    }

    *val = ret;
#ifdef DEBUG
    AW_LOG("%s reg 0x%x read 0x%x!\n", __func__, reg, *val);
#endif
    return 0;
}

static void aw2016_get_color_current(struct aw2016_led *led, uint32_t rgb_data)
{
    int red_color, green_color, blue_color;
    int red, green, blue;

    red_color = (rgb_data >> 16) & 0xFF;
    green_color = (rgb_data >> 8) & 0xFF;
    blue_color = (rgb_data >> 0) & 0xFF;

    red = red_color * LED_CURRENT_MAX / LED_COLOR_MAX;
    green = green_color * LED_CURRENT_MAX / LED_COLOR_MAX;
    blue = blue_color * LED_CURRENT_MAX / LED_COLOR_MAX;

    if ((red == 0) && (red_color != 0))
        red += 1;
    if ((green == 0) && (green_color != 0))
        green += 1;
    if ((blue == 0) && (blue_color != 0))
        blue += 1;

    led->pdata->red_current = red;
    led->pdata->green_current = green;
    led->pdata->blue_current = blue;

#ifdef DEBUG
    AW_LOG("%s rgb_data=0x%06x red/green/blue curent = %d %d %d\n",
            __func__, rgb_data,
            led->pdata->red_current, led->pdata->green_current, led->pdata->blue_current);
#endif
}

static void aw2016_set_led_sync_cfg(struct aw2016_led *led, u8 mode_mask)
{
    aw2016_write(led, AW2016_REG_LCFG1,
                (AW2016_LED_SYNC_MODE_MASK | mode_mask | led->pdata->red_current));
    aw2016_write(led, AW2016_REG_LCFG2,
                (mode_mask | led->pdata->green_current));
    aw2016_write(led, AW2016_REG_LCFG3,
                (mode_mask | led->pdata->blue_current));
}

static uint8_t aw2016_get_timings_cfg(uint16_t ms, int flag)
{
    int i, start;
    int len = sizeof(timings_cfg_group) / sizeof(struct timings_cfg);

    if (flag == 1)  // for T1&T3(rise&fall time)
        start = 0;
    else  // for T0&T2&T4(delay&on&off time)
        start = 1;

    for (i = start; i < len; i++) {
        if (ms <= timings_cfg_group[i].ms)
            return timings_cfg_group[i].reg_val;
    }

    return timings_cfg_group[len - 1].reg_val;
}

static void aw2016_get_timings(struct aw2016_led *led,
            uint32_t rgb_data, uint32_t on_ms, uint32_t off_ms)
{
    led->pdata->rise_time_ms = (on_ms >> 16) & 0xFFFF;
    led->pdata->hold_time_ms = on_ms & 0xFFFF;
    led->pdata->fall_time_ms = (off_ms >> 16) & 0xFFFF;
    led->pdata->off_time_ms = off_ms & 0xFFFF;
    led->pdata->repeat_times = (rgb_data >> 24) & 0xFF;
    if (led->pdata->repeat_times > PATTERN_REPEAT_TIMES_MAX)
        led->pdata->repeat_times = PATTERN_REPEAT_TIMES_MAX;

#ifdef DEBUG
    AW_LOG("%s rgb_data=0x%06x on/off ms = %d %d rise/hold/fall/off time ms = %d %d %d %d repeat_times=%d\n",
        __func__, rgb_data, on_ms, off_ms,
        led->pdata->rise_time_ms, led->pdata->hold_time_ms,
        led->pdata->fall_time_ms, led->pdata->off_time_ms, led->pdata->repeat_times);
#endif
}

static void aw2016_set_timings(struct aw2016_led *led)
{
    aw2016_write(led, AW2016_REG_LED1T0,
            ((aw2016_get_timings_cfg(led->pdata->rise_time_ms, 1) << 4) |
            aw2016_get_timings_cfg(led->pdata->hold_time_ms, 0)));
    aw2016_write(led, AW2016_REG_LED1T1,
            ((aw2016_get_timings_cfg(led->pdata->fall_time_ms, 1) << 4) |
            aw2016_get_timings_cfg(led->pdata->off_time_ms, 0)));
    aw2016_write(led, AW2016_REG_LED1T2,
                ((aw2016_get_timings_cfg(led->pdata->delay_time_ms, 1) << 4) |
                led->pdata->repeat_times));
}

static void aw2106_led_init(struct aw2016_led *led)
{
    u8 val;

    /* enable aw2016 if disabled */
    aw2016_read(led, AW2016_REG_GCR1, &val);
    if (!(val & AW2016_CHIP_ENABLE_MASK)) {
        aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_ENABLE_MASK);
        mdelay(2);
    }

    aw2016_write(led, AW2016_REG_GCR2, led->pdata->imax);
    aw2016_write(led, AW2016_REG_PWM1, led->pdata->brightness);

#ifdef DEBUG
    AW_LOG("%s done.\n", __func__);
#endif
}

static void aw2016_soft_reset(struct aw2016_led *led)
{
    aw2016_write(led, AW2016_REG_RESET, AW2016_CHIP_RESET_MASK);
    mdelay(5);
}

static void aw2016_brightness_work(struct work_struct *work)
{
    struct aw2016_led *led = container_of(work, struct aw2016_led,
                    brightness_work);
    u8 val;

    mutex_lock(&led->pdata->led->lock);

    /* enable aw2016 if disabled */
    aw2016_read(led, AW2016_REG_GCR1, &val);
    if (!(val & AW2016_CHIP_ENABLE_MASK)) {
        aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_ENABLE_MASK);
        mdelay(2);
    }

    if (led->cdev.brightness > 0) {
        if (led->cdev.brightness > led->cdev.max_brightness)
            led->cdev.brightness = led->cdev.max_brightness;
        aw2016_write(led, AW2016_REG_GCR2, led->pdata->imax);
        aw2016_write(led, AW2016_REG_LCFG1 + led->id,
                    (AW2016_LED_MANUAL_MODE_MASK | led->pdata->led_current));
        aw2016_write(led, AW2016_REG_PWM1 + led->id, led->cdev.brightness);
        aw2016_read(led, AW2016_REG_LEDEN, &val);
        aw2016_write(led, AW2016_REG_LEDEN, val | (1 << led->id));
    } else {
        aw2016_read(led, AW2016_REG_LEDEN, &val);
        aw2016_write(led, AW2016_REG_LEDEN, val & (~(1 << led->id)));
    }

    /*
     * If value in AW2016_REG_LEDEN is 0, it means the RGB leds are
     * all off. So we need to power it off.
     */
    aw2016_read(led, AW2016_REG_LEDEN, &val);
    if (val == 0) {
        aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_DISABLE_MASK);
        mutex_unlock(&led->pdata->led->lock);
        return;
    }

    mutex_unlock(&led->pdata->led->lock);
}

static void aw2016_blink_work(struct work_struct *work)
{
    struct aw2016_led *led = container_of(work, struct aw2016_led,
                    blink_work);
    u8 val;

    mutex_lock(&led->pdata->led->lock);

    /* enable aw2016 if disabled */
    aw2016_read(led, AW2016_REG_GCR1, &val);
    if (!(val & AW2016_CHIP_ENABLE_MASK)) {
        aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_ENABLE_MASK);
        mdelay(2);
    }

    led->cdev.brightness = led->blinking ? led->cdev.max_brightness : 0;

    if (led->blinking > 0) {
        aw2016_write(led, AW2016_REG_GCR2, led->pdata->imax);
        aw2016_write(led, AW2016_REG_PWM1 + led->id, led->cdev.brightness);
        aw2016_write(led, AW2016_REG_LED1T0 + led->id * 3,
                    ((aw2016_get_timings_cfg(led->pdata->rise_time_ms, 1) << 4) | aw2016_get_timings_cfg(led->pdata->hold_time_ms, 0)));
        aw2016_write(led, AW2016_REG_LED1T1 + led->id * 3,
                    ((aw2016_get_timings_cfg(led->pdata->fall_time_ms, 1) << 4) | aw2016_get_timings_cfg(led->pdata->off_time_ms, 0)));
        aw2016_write(led, AW2016_REG_LCFG1 + led->id,
                    (AW2016_LED_BREATH_MODE_MASK | led->pdata->led_current));
        aw2016_read(led, AW2016_REG_LEDEN, &val);
        aw2016_write(led, AW2016_REG_LEDEN, val | (1 << led->id));
    } else {
        aw2016_read(led, AW2016_REG_LEDEN, &val);
        aw2016_write(led, AW2016_REG_LEDEN, val & (~(1 << led->id)));
    }

    /*
     * If value in AW2016_REG_LEDEN is 0, it means the RGB leds are
     * all off. So we need to power it off.
     */
    aw2016_read(led, AW2016_REG_LEDEN, &val);
    if (val == 0) {
        aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_DISABLE_MASK);
    }

    mutex_unlock(&led->pdata->led->lock);
}

static void aw2016_set_brightness(struct led_classdev *cdev,
                 enum led_brightness brightness)
{
    struct aw2016_led *led = container_of(cdev, struct aw2016_led, cdev);

    led->cdev.brightness = brightness;

    schedule_work(&led->brightness_work);
}

static ssize_t aw2016_store_blink(struct device *dev,
                 struct device_attribute *attr,
                 const char *buf, size_t len)
{
    unsigned long blinking;
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    ssize_t ret = -EINVAL;

    ret = kstrtoul(buf, 10, &blinking);
    if (ret)
        return ret;
    led->blinking = (int)blinking;
    schedule_work(&led->blink_work);

    return len;
}

static ssize_t aw2016_led_time_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);

    return snprintf(buf, PAGE_SIZE, "%d %d %d %d\n",
            led->pdata->rise_time_ms, led->pdata->hold_time_ms,
            led->pdata->fall_time_ms, led->pdata->off_time_ms);
}

static ssize_t aw2016_led_time_store(struct device *dev,
                 struct device_attribute *attr,
                 const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    int rc, rise_time_ms, hold_time_ms, fall_time_ms, off_time_ms;

    rc = sscanf(buf, "%d %d %d %d",
            &rise_time_ms, &hold_time_ms,
            &fall_time_ms, &off_time_ms);

    mutex_lock(&led->pdata->led->lock);
    led->pdata->rise_time_ms = rise_time_ms;
    led->pdata->hold_time_ms = hold_time_ms;
    led->pdata->fall_time_ms = fall_time_ms;
    led->pdata->off_time_ms = off_time_ms;
    led->blinking = 1;
    mutex_unlock(&led->pdata->led->lock);

    schedule_work(&led->blink_work);

    return len;
}

static ssize_t aw2016_reg_show(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);

    unsigned char i, reg_val;
    ssize_t len = 0;

    for (i = 0; i < AW2016_REG_MAX; i++) {
        if (!(aw2016_reg_access[i]&REG_RD_ACCESS))
            continue;
        aw2016_read(led, i, &reg_val);
        len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x\n", i, reg_val);
    }

    return len;
}

static ssize_t aw2016_reg_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    unsigned int databuf[2];

    if (2 == sscanf(buf, "%x %x", &databuf[0], &databuf[1])) {
        aw2016_write(led, (unsigned char)databuf[0], (unsigned char)databuf[1]);
    }

    return len;
}
static DEVICE_ATTR(blink, 0664, NULL, aw2016_store_blink);
static DEVICE_ATTR(led_time, 0664, aw2016_led_time_show, aw2016_led_time_store);
static DEVICE_ATTR(reg, 0664, aw2016_reg_show, aw2016_reg_store);

static struct attribute *aw2016_led_attributes[] = {
    &dev_attr_blink.attr,
    &dev_attr_led_time.attr,
    &dev_attr_reg.attr,
    NULL,
};

static struct attribute_group aw2016_led_attr_group = {
    .attrs = aw2016_led_attributes
};

static bool is_pattern_running_over(struct aw2016_led *led)
{
    u8 val, i = 0;
    int max_repeat_times = (led->pdata->rise_time_ms + led->pdata->hold_time_ms +
            led->pdata->fall_time_ms + led->pdata->off_time_ms) *
            led->pdata->repeat_times / CHECK_PATTERN_STATUS_DELAY + CHECK_PATTERN_STATUS_MORE_TIMES;
#ifdef DEBUG
    AW_LOG("%s max_repeat_times = %d\n", __func__, max_repeat_times);
#endif

    while (i < max_repeat_times) {
        aw2016_read(led, AW2016_REG_PATST, &val);
#ifdef DEBUG
            AW_LOG("%s Reg[0x03] = 0x%x\n", __func__, val);
#endif
        if ((val & AW2016_LED_PATTERN_STATUS_MASK) == 0x0) {  /* led1/led2/led3 pattern is all over */
#ifdef DEBUG
            AW_LOG("%s pattern running over. repeat_times = %d\n", __func__, i);
#endif
            return true;
        }

        if (led->dance_work_state != WORK_RUNNING) {  /* dance work interrupted */
            AW_LOG("pattern running interrupted.\n");
            return false;
        }

        mdelay(CHECK_PATTERN_STATUS_DELAY);
        i++;
    }

    AW_ERR("pattern running waitting too long time.\n");
    return false;
}

static void aw2016_dance_work(struct work_struct *work)
{
    struct aw2016_led *led = container_of(work, struct aw2016_led,
                    dance_work);
    int i = 0, flash_times = led->pdata->repeat_times;

    aw2106_led_init(led);

    /* power on effect --> rise/hold/fall/off time all 0 */
    if (!led->pdata->rise_time_ms && !led->pdata->hold_time_ms &&
            !led->pdata->fall_time_ms && !led->pdata->off_time_ms) {
        int j;
        AW_LOG("%s power on effect start!\n", __func__);
        aw2016_set_led_sync_cfg(led, AW2016_LED_BREATH_MODE_MASK);

        for (i = 0; led->dance_work_state == WORK_RUNNING; i++) {
            for (j = 0; ((j < 2) && (led->dance_work_state == WORK_RUNNING)); j++){
                led->pdata->rise_time_ms = 700;
                led->pdata->hold_time_ms = 200;
                led->pdata->fall_time_ms = 2000;
                led->pdata->off_time_ms = 1000;
                led->pdata->repeat_times = 1;
                aw2016_set_timings(led);
                aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);

                mdelay(CHECK_PATTERN_STATUS_DELAY);
                if (is_pattern_running_over(led)) {
                    ;
#ifdef DEBUG
                    AW_LOG("power on effect pattern running done! i j = %d %d\n", i, j);
#endif
                } else {
                    led->dance_work_state = WORK_STOP;
                    return;
                }

                if (j == 0) {
                    led->pdata->rise_time_ms = 0;
                    led->pdata->hold_time_ms = 100;
                    led->pdata->fall_time_ms = 0;
                    led->pdata->off_time_ms = 100;
                    led->pdata->repeat_times = 2;
                    aw2016_set_timings(led);
                    aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);

                    mdelay(CHECK_PATTERN_STATUS_DELAY);
                    if (is_pattern_running_over(led)) {
                        ;
#ifdef DEBUG
                        AW_LOG("power on effect pattern running done! i j = %d %d\n", i, j);
#endif
                    } else {
                        led->dance_work_state = WORK_STOP;
                        return;
                    }
                }
            }
            if ((flash_times >= 1) && (i >= (flash_times - 1))) {
                led->dance_work_state = WORK_STOP;
                AW_LOG("%s power on effect exit.\n", __func__);
                return;
            }
        }
    }

    /* damon mode --> red/green/blue all 0xF*/
    if ((led->pdata->red_current == LED_CURRENT_MAX) &&
            (led->pdata->green_current == LED_CURRENT_MAX) &&
            (led->pdata->blue_current == LED_CURRENT_MAX)) {
        uint32_t damon_mode_colors[7] =
            {COLOR_PINK, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_PURPLE};
        int len = sizeof(damon_mode_colors) / sizeof(uint32_t);
        led->pdata->repeat_times = 1;
        AW_LOG("%s damon mode enter!\n", __func__);

        for (i = 0; led->dance_work_state == WORK_RUNNING; i = ((i + 1) % len)) {
            aw2016_get_color_current(led, damon_mode_colors[i]);
            aw2016_set_led_sync_cfg(led, AW2016_LED_BREATH_MODE_MASK);
            aw2016_set_timings(led);
            aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);

            mdelay(CHECK_PATTERN_STATUS_DELAY);
            if (is_pattern_running_over(led)) {
                ;
#ifdef DEBUG
                AW_LOG("damon mode pattern running done!  = %d\n", i);
#endif
            } else {
                break;
            }

            mdelay(led->pdata->off_time_ms);
        }

        led->dance_work_state = WORK_STOP;
        AW_LOG("%s daemon mode exit.\n", __func__);
        return;
    }

    /* incoming call effect --> flash DANCE_FLASH_TIMES times + delay DANCE_SLEEP_MS */
    AW_LOG("%s incoming call effect enter!\n", __func__);
    for (i = 0; led->dance_work_state == WORK_RUNNING; i++) {
        aw2016_set_led_sync_cfg(led, AW2016_LED_BREATH_MODE_MASK);
        led->pdata->repeat_times = DANCE_FLASH_TIMES;
        aw2016_set_timings(led);
        aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);

        mdelay(CHECK_PATTERN_STATUS_DELAY);
        if (is_pattern_running_over(led)) {
            ;
#ifdef DEBUG
            AW_LOG("incoming call effect pattern running done! i = %d\n", i);
#endif
        } else {
            break;
        }

        mdelay(DANCE_SLEEP_MS);
        if ((flash_times > 0) && (i == (flash_times - 1))) {
            break;
        }
    }

    led->dance_work_state = WORK_STOP;
    if ((flash_times > 0) && (i == flash_times))
        AW_LOG("%s incoming call effect done(flash_times = %d)!\n", __func__, flash_times);
    else
        AW_LOG("%s incoming call effect exit %d(%d)!\n", __func__, flash_times, i);
    return;
}

static void aw2106_led_light_off(struct aw2016_led *led)
{
    aw2016_write(led, AW2016_REG_GCR1, AW2016_CHARGE_DISABLE_MASK | AW2016_CHIP_DISABLE_MASK);
    AW_LOG("%s done!\n", __func__);
}

static void aw2106_led_light_on(struct aw2016_led *led)
{
    aw2106_led_init(led);
    aw2016_set_led_sync_cfg(led, AW2016_LED_MANUAL_MODE_MASK);
    aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);
    AW_LOG("%s start!\n", __func__);
}

static void aw2106_led_breath_on(struct aw2016_led *led)
{
    aw2106_led_init(led);
    aw2016_set_led_sync_cfg(led, AW2016_LED_BREATH_MODE_MASK);
    aw2016_set_timings(led);
    aw2016_write(led, AW2016_REG_LEDEN, AW2016_LED_ALL_ENABLE_MASK);
    AW_LOG("%s start!\n", __func__);
}

static ssize_t aw2016_groupcolor_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    uint32_t rgb_data = 0;

    if (sscanf(buf, "%x", &rgb_data) != 1) {
        AW_LOG("%s led[%d] Error!\n", __func__, led->id);
        return len;
    }
    AW_LOG("%s led[%d] enter, rgb_data=0x%06x\n", __func__, led->id, rgb_data);

    if (led->dance_work_state != WORK_STOP) {
        led->dance_work_state = WORK_STOP;
        cancel_work_sync(&led->dance_work);
    }

    mutex_lock(&led->pdata->led->lock);
    if (rgb_data == 0) {
        aw2106_led_light_off(led);
        mutex_unlock(&led->pdata->led->lock);
        return len;
    }

    aw2016_get_color_current(led, rgb_data);
    aw2106_led_light_on(led);
    mutex_unlock(&led->pdata->led->lock);
    return len;
}

static ssize_t aw2016_breathcolor_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    uint32_t rgb_data = 0,  on_ms = 0, off_ms = 0;

    if (sscanf(buf, "%x %x %x", &rgb_data, &on_ms, &off_ms) != 3) {
        AW_LOG("%s led[%d] Error!\n", __func__, led->id);
        return len;
    }
    AW_LOG("%s led[%d] enter, rgb_data=0x%06x on_ms=0x%x off_ms=0x%x\n",
            __func__, led->id, rgb_data, on_ms, off_ms);

    if (led->dance_work_state != WORK_STOP) {
        led->dance_work_state = WORK_STOP;
        cancel_work_sync(&led->dance_work);
    }

    mutex_lock(&led->pdata->led->lock);
    if (rgb_data == 0) {
        aw2106_led_light_off(led);
        mutex_unlock(&led->pdata->led->lock);
        return len;
    }

    aw2016_get_color_current(led, rgb_data);
    aw2016_get_timings(led, rgb_data, on_ms, off_ms);
    aw2106_led_breath_on(led);

    mutex_unlock(&led->pdata->led->lock);
    return len;
}

static ssize_t aw2016_dancecolor_store(struct device *dev,
                struct device_attribute *attr,
                const char *buf, size_t len)
{
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct aw2016_led *led =
            container_of(led_cdev, struct aw2016_led, cdev);
    uint32_t rgb_data = 0, on_ms = 0, off_ms = 0;

    if (sscanf(buf, "%x %x %x", &rgb_data, &on_ms, &off_ms) != 3) {
        AW_LOG("%s led[%d] Error!\n", __func__, led->id);
        return len;
    }
    AW_LOG("%s led[%d] enter, rgb_data=0x%x on_ms=0x%x off_ms=0x%x\n",
            __func__, led->id, rgb_data, on_ms, off_ms);

    if (led->dance_work_state != WORK_STOP) {
        led->dance_work_state = WORK_STOP;
        cancel_work_sync(&led->dance_work);
    }

    if (rgb_data == 0) {
        mutex_lock(&led->pdata->led->lock);
        aw2106_led_light_off(led);
        mutex_unlock(&led->pdata->led->lock);
        return len;
    }

    aw2016_get_color_current(led, rgb_data);
    aw2016_get_timings(led, rgb_data, on_ms, off_ms);
    led->dance_work_state = WORK_RUNNING;
    schedule_work(&led->dance_work);

    return len;
}
static DEVICE_ATTR(groupcolor, 0664, NULL, aw2016_groupcolor_store);
static DEVICE_ATTR(breathcolor, 0664, NULL, aw2016_breathcolor_store);
static DEVICE_ATTR(dancecolor, 0664, NULL, aw2016_dancecolor_store);

static struct attribute *aw_led_attributes[] = {
    &dev_attr_groupcolor.attr,
    &dev_attr_breathcolor.attr,
    &dev_attr_dancecolor.attr,
    &dev_attr_reg.attr,
    NULL,
};

static struct attribute_group aw_led_attr_group = {
    .attrs = aw_led_attributes
};

static int aw2016_check_chipid(struct aw2016_led *led)
{
    u8 val;
    u8 cnt;

    for (cnt = 5; cnt > 0; cnt --) {
        aw2016_read(led, AW2016_REG_RESET, &val);
        AW_LOG("aw2016 chip id %0x",val);
        if (val == AW2016_CHIPID) {
            is_breath_led_exist = 1;  /* zte boardtest interface */
            return 0;
        }
    }

    return -EINVAL;
}

static int aw2016_led_err_handle(struct aw2016_led *led_array,
                int parsed_leds)
{
    int i;
    /*
     * If probe fails, cannot free resource of all LEDs, only free
     * resources of LEDs which have allocated these resource really.
     */
    for (i = 0; i < parsed_leds; i++) {
        if (i < 3) {
            sysfs_remove_group(&led_array[i].cdev.dev->kobj,
                    &aw2016_led_attr_group);
            cancel_work_sync(&led_array[i].brightness_work);
            cancel_work_sync(&led_array[i].blink_work);
        } else {
            sysfs_remove_group(&led_array[i].cdev.dev->kobj,
                    &aw_led_attr_group);
            cancel_work_sync(&led_array[i].dance_work);
        }
        led_classdev_unregister(&led_array[i].cdev);
        devm_kfree(&led_array->client->dev, led_array[i].pdata);
        led_array[i].pdata = NULL;
    }
    return i;
}

static int aw2016_led_parse_child_node(struct aw2016_led *led_array,
                struct device_node *node)
{
    struct aw2016_led *led;
    struct device_node *temp;
    struct aw2016_platform_data *pdata;
    int rc = 0, parsed_leds = 0;

    for_each_child_of_node(node, temp) {
        led = &led_array[parsed_leds];
        led->client = led_array->client;

        pdata = devm_kzalloc(&led->client->dev,
                sizeof(struct aw2016_platform_data),
                GFP_KERNEL);
        if (!pdata) {
            AW_ERR("Failed to allocate memory\n");
            goto free_err;
        }
        pdata->led = led_array;
        led->pdata = pdata;

        rc = of_property_read_string(temp, "aw2016,name",
            &led->cdev.name);
        if (rc < 0) {
            AW_ERR("Failure reading led name, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,id",
            &led->id);
        if (rc < 0) {
            AW_ERR("Failure reading id, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,imax",
            &led->pdata->imax);
        if (rc < 0) {
            AW_ERR("Failure reading imax, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,max-brightness",
            &led->cdev.max_brightness);
        if (rc < 0) {
            AW_ERR("Failure reading max-brightness, rc = %d\n",
                rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,rise-time-ms",
            &led->pdata->rise_time_ms);
        if (rc < 0) {
            AW_ERR("Failure reading rise-time-ms, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,hold-time-ms",
            &led->pdata->hold_time_ms);
        if (rc < 0) {
            AW_ERR("Failure reading hold-time-ms, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,fall-time-ms",
            &led->pdata->fall_time_ms);
        if (rc < 0) {
            AW_ERR("Failure reading fall-time-ms, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = of_property_read_u32(temp, "aw2016,off-time-ms",
            &led->pdata->off_time_ms);
        if (rc < 0) {
            AW_ERR("Failure reading off-time-ms, rc = %d\n", rc);
            goto free_pdata;
        }

        rc = led_classdev_register(&led->client->dev, &led->cdev);
        if (rc) {
            AW_ERR("unable to register led %d,rc=%d\n",
                led->id, rc);
            goto free_pdata;
        }

        if (led->id < 3) {
            rc = of_property_read_u32(temp, "aw2016,led-current",
                &led->pdata->led_current);
            if (rc < 0) {
                AW_ERR("Failure reading led-current, rc = %d\n", rc);
                goto free_pdata;
            }

            INIT_WORK(&led->brightness_work, aw2016_brightness_work);
            INIT_WORK(&led->blink_work, aw2016_blink_work);
            led->cdev.brightness_set = aw2016_set_brightness;

            rc = sysfs_create_group(&led->cdev.dev->kobj,
                    &aw2016_led_attr_group);
            if (rc) {
                AW_ERR("aw2016 led sysfs rc: %d\n", rc);
                goto free_class;
            }
        } else {  // aw_led node
            rc = of_property_read_u32(temp, "aw2016,red-current",
                &led->pdata->red_current);
            if (rc < 0) {
                AW_ERR("Failure reading red-current, rc = %d\n", rc);
                goto free_pdata;
            }

            rc = of_property_read_u32(temp, "aw2016,green-current",
                &led->pdata->green_current);
            if (rc < 0) {
                AW_ERR("Failure reading green-current, rc = %d\n", rc);
                goto free_pdata;
            }

            rc = of_property_read_u32(temp, "aw2016,blue-current",
                &led->pdata->blue_current);
            if (rc < 0) {
                AW_ERR("Failure reading blue-current, rc = %d\n", rc);
                goto free_pdata;
            }

            rc = of_property_read_u32(temp, "aw2016,brightness",
                &led->pdata->brightness);
            if (rc < 0) {
                AW_ERR("Failure reading brightness, rc = %d\n",
                    rc);
                goto free_pdata;
            }

            rc = of_property_read_u32(temp, "aw2016,delay-time-ms",
                &led->pdata->delay_time_ms);
            if (rc < 0) {
                AW_ERR("Failure reading delay-time-ms, rc = %d\n", rc);
                goto free_pdata;
            }

            rc = of_property_read_u32(temp, "aw2016,repeat_times",
                &led->pdata->repeat_times);
            if (rc < 0) {
                AW_ERR("Failure reading repeat_times, rc = %d\n", rc);
                goto free_pdata;
            }

            INIT_WORK(&led->dance_work, aw2016_dance_work);
            led->dance_work_state = WORK_STOP;

            rc = sysfs_create_group(&led->cdev.dev->kobj,
                    &aw_led_attr_group);
            if (rc) {
                AW_ERR("aw led sysfs rc: %d\n", rc);
                goto free_class;
            }
        }
        parsed_leds++;
    }

    return 0;

free_class:
    aw2016_led_err_handle(led_array, parsed_leds);
    led_classdev_unregister(&led_array[parsed_leds].cdev);
    cancel_work_sync(&led_array[parsed_leds].brightness_work);
    cancel_work_sync(&led_array[parsed_leds].blink_work);
    devm_kfree(&led->client->dev, led_array[parsed_leds].pdata);
    led_array[parsed_leds].pdata = NULL;
    return rc;

free_pdata:
    aw2016_led_err_handle(led_array, parsed_leds);
    devm_kfree(&led->client->dev, led_array[parsed_leds].pdata);
    return rc;

free_err:
    aw2016_led_err_handle(led_array, parsed_leds);
    return rc;
}

static bool is_charger_mode = false;
static int get_boot_mode(void)
{
    struct device_node *cmdline_node;
    int ret;
    const char *cmd_line;

    cmdline_node = of_find_node_by_path("/chosen");
    ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
    if (ret) {
        AW_ERR("of_property_read_string bootargs failed %d\n", ret);
        return ret;
    }
    if (strstr(cmd_line, "androidboot.mode=charger"))
        is_charger_mode = true;

    AW_LOG("%s is_charge_mode=%d\n", __func__, is_charger_mode);
    return 0;
}

static int aw2016_led_probe(struct i2c_client *client,
                const struct i2c_device_id *id)
{
    struct aw2016_led *led_array;
    struct device_node *node;
    int ret = -EINVAL, num_leds = 0;

    node = client->dev.of_node;
    if (node == NULL)
        return -EINVAL;

    num_leds = of_get_child_count(node);

    if (!num_leds)
        return -EINVAL;

    led_array = devm_kzalloc(&client->dev,
            (sizeof(struct aw2016_led) * num_leds), GFP_KERNEL);
    if (!led_array)
        return -ENOMEM;

    led_array->client = client;
    led_array->num_leds = num_leds;

    mutex_init(&led_array->lock);

    ret = aw2016_led_parse_child_node(led_array, node);
    if (ret) {
        AW_ERR("parsed node error\n");
        goto free_led_arry;
    }

    i2c_set_clientdata(client, led_array);

    /* add boardtest interface start */
    zte_misc_register_callback(&is_breath_led_exist_node, NULL);
    is_breath_led_exist = 0;
    /* add boardtest interface end */

    ret = aw2016_check_chipid(led_array);
    if (ret) {
        AW_ERR("Check chip id error\n");
        goto fail_parsed_node;
    }

    /* soft rst */
    aw2016_soft_reset(led_array);

    /* power on effect */
    get_boot_mode();
    if (!is_charger_mode) {
        struct aw2016_led *led = &led_array[3];
        aw2016_get_color_current(led, COLOR_BLUE);
        aw2016_get_timings(led, (1 << 24) | COLOR_BLUE, 0, 0);
        led->dance_work_state = WORK_RUNNING;
        schedule_work(&led->dance_work);
    }

    AW_LOG("%s sucess!\n", __func__);
    return 0;

fail_parsed_node:
    aw2016_led_err_handle(led_array, num_leds);
free_led_arry:
    mutex_destroy(&led_array->lock);
    devm_kfree(&client->dev, led_array);
    led_array = NULL;
    return ret;
}

static int aw2016_led_remove(struct i2c_client *client)
{
    struct aw2016_led *led_array = i2c_get_clientdata(client);
    int i, parsed_leds = led_array->num_leds;

    for (i = 0; i < parsed_leds; i++) {
        if (i < 3) {
            sysfs_remove_group(&led_array[i].cdev.dev->kobj,
                    &aw2016_led_attr_group);
            cancel_work_sync(&led_array[i].brightness_work);
            cancel_work_sync(&led_array[i].blink_work);
        } else {
            sysfs_remove_group(&led_array[i].cdev.dev->kobj,
                    &aw_led_attr_group);
            cancel_work_sync(&led_array[i].dance_work);
        }
        led_classdev_unregister(&led_array[i].cdev);
        devm_kfree(&client->dev, led_array[i].pdata);
        led_array[i].pdata = NULL;
    }
    mutex_destroy(&led_array->lock);
    devm_kfree(&client->dev, led_array);
    led_array = NULL;
    return 0;
}

static void aw2016_led_shutdown(struct i2c_client *client)
{
    struct aw2016_led *led = i2c_get_clientdata(client);

    aw2016_write(led, AW2016_REG_GCR1, AW2016_CHIP_STANDBY);
}

static const struct i2c_device_id aw2016_led_id[] = {
    {"aw2016_led", 0},
    {},
};

MODULE_DEVICE_TABLE(i2c, aw2016_led_id);

static struct of_device_id aw2016_match_table[] = {
    { .compatible = "awinic,aw2016_led",},
    { },
};

static struct i2c_driver aw2016_led_driver = {
    .probe = aw2016_led_probe,
    .remove = aw2016_led_remove,
    .shutdown = aw2016_led_shutdown,
    .driver = {
        .name = "aw2016_led",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(aw2016_match_table),
    },
    .id_table = aw2016_led_id,
};

static int __init aw2016_led_init(void)
{
    pr_info("%s: driver version: %s\n", __func__, AW2016_DRIVER_VERSION);
    return i2c_add_driver(&aw2016_led_driver);
}
module_init(aw2016_led_init);

static void __exit aw2016_led_exit(void)
{
    i2c_del_driver(&aw2016_led_driver);
}
module_exit(aw2016_led_exit);

MODULE_AUTHOR("<liweilei@awinic.com.cn>");
MODULE_DESCRIPTION("AWINIC AW2016 LED driver");
MODULE_LICENSE("GPL v2");
