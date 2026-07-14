#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include "sqc_platform.h"

int sqc_get_boot_mode(void)
{
	return SQC_BOOT_NORMAL;
}
EXPORT_SYMBOL_GPL(sqc_get_boot_mode);
