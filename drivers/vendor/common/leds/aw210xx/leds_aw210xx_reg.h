#ifndef __LEDS_AW210XX_REG_H__
#define __LEDS_AW210XX_REG_H__

unsigned char aw210xx_group_cfg_led_off[] = {
	0x00, 0x00,
};

unsigned char aw21009_group_led_on[] = {
	0x00, 0x00,
};

unsigned char aw21009_long_breath_red_on[] = {  // T0=T2=1.6s T1=0.2s T3=4s
	0x00, 0x00,
};

unsigned char aw21009_breath_led_on[] = {  // T0=T2=0.7s T1=0.1s T3=4s
	0x00, 0x00,
};

unsigned char aw21009_dance_colors_on[] = {  // T0=T2=0s T1=T3=0.1s flash 3 times, sleep 1s and so on ...
	0x00, 0x00,
};

unsigned char aw21009_power_on_effect[] = {  // 1 flash(T0=T2=0s T1=T3=0.3s) + 1 breath(T0=T2=0.7s T1=0.1s T3=4s)
	0x00, 0x00,
};
#endif