#ifndef __ZTE_TPD_H__
#define __ZTE_TPD_H__

enum lcd_state {
	LCD_POWER_ON = 0,
	LCD_RESET,
	LCD_CMD_ON,
	LCD_CMD_OFF,
	LCD_POWER_OFF,
};

extern int lcd_notifier_register_client(struct notifier_block *nb);
extern int lcd_notifier_unregister_client(struct notifier_block *nb);
extern int lcd_notifier_call_chain(unsigned long val);
#endif