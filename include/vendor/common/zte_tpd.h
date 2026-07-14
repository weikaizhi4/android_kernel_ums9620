#ifndef __ZTE_TPD_H__
#define __ZTE_TPD_H__

#include <linux/input.h>

#define KEY_GESTURE_DOUBLEC		0x2a0
#define KEY_GESTURE_L			KEY_RESERVED
#define KEY_GESTURE_DOWN		KEY_RESERVED
#define KEY_GESTURE_U			KEY_RESERVED
#define KEY_GESTURE_UP			KEY_RESERVED
#define KEY_GESTURE_C			KEY_RESERVED
#define KEY_GESTURE_E			KEY_RESERVED
#define KEY_GESTURE_M			KEY_RESERVED
#define KEY_GESTURE_LEFT		KEY_RESERVED
#define KEY_GESTURE_RIGHT		KEY_RESERVED
#define KEY_GESTURE_S			KEY_RESERVED
#define KEY_GESTURE_W			KEY_RESERVED
#define KEY_GESTURE_V			KEY_RESERVED
#define KEY_GESTURE_O			KEY_RESERVED
#define KEY_GESTURE_Z			KEY_RESERVED

enum lcd_state {
	LCD_POWER_ON = 0,
	LCD_RESET,
	LCD_CMD_ON,
	LCD_CMD_OFF,
	LCD_POWER_OFF,
	LCD_POWER_OFF_RESET_LOW,
	LCD_SHUTDOWN,
	LCD_ENTER_AOD,
	LCD_EXIT_AOD,

};

enum lcd_suspend_power {
	LCD_SUSPEND_POWER_ON,
	LCD_SUSPEND_POWER_OFF,
};
extern int lcd_notifier_register_client(struct notifier_block *nb);
extern int lcd_notifier_unregister_client(struct notifier_block *nb);
extern int lcd_notifier_call_chain(unsigned long val);
extern int tpd_notifier_register_client(struct notifier_block *nb);
extern int tpd_notifier_unregister_client(struct notifier_block *nb);
extern int tpd_notifier_call_chain(unsigned long val);
#endif