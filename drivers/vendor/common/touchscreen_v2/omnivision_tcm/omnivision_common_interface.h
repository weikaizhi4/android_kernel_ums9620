#ifndef _OMNIVISION_COMMON_INTERFACE_H_
#define _OMNIVISION_COMMON_INTERFACE_H_

#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <uapi/linux/sched/types.h>
#include "omnivision_tcm_core.h"
#include "omnivision_tcm_zeroflash.h"
#include "omnivision_tcm_testing.h"

#define MAX_NAME_LEN_50		50

/*ovt tp test item*/
extern int testing_device_id(void);
extern int testing_config_id(void);
extern int testing_reset_open(void);
extern int testing_pt01_trx_trx_short(void);
extern int testing_pt05_full_raw(void);
extern int testing_pt07_dynamic_range(void);
extern int testing_pt10_noise(void);
extern int testing_pt11_open_detection(void);

extern void testing_get_frame_size_words(unsigned int *size, bool image_only);
extern int testing_run_prod_test_item(enum test_code test_code);
extern void testing_standard_frame_output(bool image_only);

#endif /* _OMNIVISION_COMMON_INTERFACE_H_ */
