#
# Makefile for zte touchscreen_v2 driver.
#
ifeq ($(CONFIG_VENDOR_ZTE_LOG_EXCEPTION),m)
	ccflags-y += -DCONFIG_VENDOR_ZTE_LOG_EXCEPTION
endif
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/goodix_berlin_driver_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chipone_tddi_v2/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chipone_tddi_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/chsc5xxx/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/ilitek_tddi_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ifeq ($(CONFIG_TOUCHSCREEN_FTS_V3_3),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_V3_3/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_V3_3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ifeq ($(CONFIG_TOUCHSCREEN_FTS_UFP),y)
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp/focaltech_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/focaltech_touch_ufp/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
endif
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/tlsc_chip3535/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/tlsc6x_v3/tlsc_chip3536/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/gcore_ts/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/gcore_ts_v3/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/betterlife_ts/factory_test/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/betterlife_ts/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/sitronix_incell/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/
ccflags-y += -I$(srctree)/drivers/vendor/common/touchscreen_v2/omnivision_tcm/firmware_config/$(CONFIG_TOUCHSCREEN_BOARD_NAME)/

zte_tpd-y := ztp_core.o ztp_report_algo.o ztp_state_change.o
zte_tpd-$(CONFIG_TOUCHSCREEN_UFP_MAC) +=  ztp_ufp.o

######################################################
ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_V2)),)
	ifneq ($(filter y, $(CONFIG_TOUCHSCREEN_GOODIX_BRL_I2C)),)
		zte_tpd-y += goodix_berlin_driver_v2/goodix_brl_i2c.o
	else
		zte_tpd-y += goodix_berlin_driver_v2/goodix_brl_spi.o
	endif
endif
zte_tpd-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_V2) += \
	goodix_berlin_driver_v2/goodix_ts_core.o \
	goodix_berlin_driver_v2/goodix_brl_hw.o \
	goodix_berlin_driver_v2/goodix_cfg_bin.o \
	goodix_berlin_driver_v2/goodix_ts_utils.o \
	goodix_berlin_driver_v2/goodix_brl_fwupdate.o \
	goodix_berlin_driver_v2/goodix_ts_gesture.o \
	goodix_berlin_driver_v2/goodix_ts_inspect.o \
	goodix_berlin_driver_v2/goodix_ts_tools.o \
	goodix_berlin_driver_v2/goodix_common_interface.o

################### semi touch #########################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHSC5XXX)	+= \
	chsc5xxx/basic_util.o \
	chsc5xxx/i2c_communication.o \
	chsc5xxx/qual_core.o \
	chsc5xxx/semi_touch_function.o \
	chsc5xxx/semi_touch_upgrade.o \
	chsc5xxx/semi_touch_apk.o \
	chsc5xxx/semi_touch_device.o \
	chsc5xxx/semi_touch_custom.o \
	chsc5xxx/semi_touch_test_5448.o \
	chsc5xxx/semi_common_interface.o
######################################################

###################CHIPONE_TDDI_V3#####################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHIPONE_V3) += \
	chipone_tddi_v3/cts_driver.o \
	chipone_tddi_v3/cts_i2c_driver.o \
	chipone_tddi_v3/cts_spi_driver.o \
	chipone_tddi_v3/cts_core.o \
	chipone_tddi_v3/cts_sfctrlv2.o \
	chipone_tddi_v3/cts_spi_flash.o \
	chipone_tddi_v3/cts_firmware.o \
	chipone_tddi_v3/cts_test.o \
	chipone_tddi_v3/cts_strerror.o \
	chipone_tddi_v3/cts_charger_detect.o \
	chipone_tddi_v3/cts_earjack_detect.o \
	chipone_tddi_v3/cts_platform.o \
	chipone_tddi_v3/cts_tool.o \
	chipone_tddi_v3/cts_sysfs.o \
	chipone_tddi_v3/cts_ioctl.o \
	chipone_tddi_v3/cts_cdev.o \
	chipone_tddi_v3/cts_common_interface.o \
	chipone_tddi_v3/cts_selftest.o \
	chipone_tddi_v3/cts_selftest_ini.o
######################################################
########### ilitek tddi tp driver ###############
zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3) += \
	ilitek_tddi_v3/ilitek_v3.o \
	ilitek_tddi_v3/ilitek_v3_qcom.o \
	ilitek_tddi_v3/ilitek_v3_ic.o \
	ilitek_tddi_v3/ilitek_v3_touch.o \
	ilitek_tddi_v3/ilitek_v3_mp.o \
	ilitek_tddi_v3/ilitek_v3_node.o
zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_I2C) += \
	ilitek_tddi_v3/ilitek_v3_i2c.o \
	ilitek_tddi_v3/ilitek_v3_flash.o

zte_tpd-$(CONFIG_TOUCHSCREEN_ILITEK_TDDI_V3_SPI) += \
	ilitek_tddi_v3/ilitek_v3_spi.o \
	ilitek_tddi_v3/ilitek_v3_hostdl.o \
	ilitek_tddi_v3/ilitek_v3_common_interface.o
########################################################

###################FOCAL TOUCH V3_3#####################
zte_tpd-$(CONFIG_TOUCHSCREEN_FTS_V3_3) += \
	focaltech_touch_V3_3/focaltech_core.o \
	focaltech_touch_V3_3/focaltech_ex_fun.o \
	focaltech_touch_V3_3/focaltech_ex_mode.o \
	focaltech_touch_V3_3/focaltech_gesture.o \
	focaltech_touch_V3_3/focaltech_esdcheck.o \
	focaltech_touch_V3_3/focaltech_point_report_check.o \
	focaltech_touch_V3_3/focaltech_flash.o \
	focaltech_touch_V3_3/focaltech_common_interface.o \
	focaltech_touch_V3_3/focaltech_spi.o \
	focaltech_touch_V3_3/focaltech_test/focaltech_test.o \
	focaltech_touch_V3_3/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_V3_3/focaltech_test/supported_ic/focaltech_test_ft8756.o \
	focaltech_touch_V3_3/focaltech_test/supported_ic/focaltech_test_ft8720.o

######################################################

###################FOCAL TOUCH V3_3#####################
zte_tpd-$(CONFIG_TOUCHSCREEN_FTS_UFP) += \
	focaltech_touch_ufp/focaltech_core.o \
	focaltech_touch_ufp/focaltech_ex_fun.o \
	focaltech_touch_ufp/focaltech_ex_mode.o \
	focaltech_touch_ufp/focaltech_gesture.o \
	focaltech_touch_ufp/focaltech_esdcheck.o \
	focaltech_touch_ufp/focaltech_point_report_check.o \
	focaltech_touch_ufp/focaltech_flash.o \
	focaltech_touch_ufp/focaltech_common_interface.o \
	focaltech_touch_ufp/focaltech_spi.o \
	focaltech_touch_ufp/focaltech_test/focaltech_test.o \
	focaltech_touch_ufp/focaltech_test/focaltech_test_ini.o \
	focaltech_touch_ufp/focaltech_flash/focaltech_upgrade_ft3681.o \
	focaltech_touch_ufp/focaltech_test/supported_ic/focaltech_test_ft3681.o

######################################################

################## tlsc v3 tp driver ####################
zte_tpd-$(CONFIG_TOUCHSCREEN_TLSC6X_V3) += \
	tlsc6x_v3/tlsc6x_main.o \
	tlsc6x_v3/tlsc6x_comp.o \
	tlsc6x_v3/tlsc6x_common_interface.o
########################################################

################## gcore tp driver #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_GCORE_TS) += \
	gcore_ts/gcore_drv_main.o \
	gcore_ts/gcore_drv_qcom.o \
	gcore_ts/gcore_drv_fw_update.o \
	gcore_ts/gcore_drv_interface.o \
	gcore_ts/gcore_drv_mp_test.o \
	gcore_ts/gcore_common_interface.o
########################################################

################## gcore tp driver #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_GCORE_TS_V3) += \
	gcore_ts_v3/gcore_drv_main.o \
	gcore_ts_v3/gcore_drv_qcom.o \
	gcore_ts_v3/gcore_drv_fw_update.o \
	gcore_ts_v3/gcore_drv_interface.o \
	gcore_ts_v3/gcore_drv_mp_test.o \
	gcore_ts_v3/gcore_common_interface.o
########################################################

################## betterlife tp driver #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_BETTERLIFE_TS) += \
	betterlife_ts/bl_ts.o \
	betterlife_ts/fw_update.o \
	betterlife_ts/bl_common_interface.o \
	betterlife_ts/factory_test/bl_test.o \
	betterlife_ts/factory_test/bl_test_ini.o \
	betterlife_ts/factory_test/bl_test_mcsc.o \
	betterlife_ts/factory_test/bl_test_sc.o
########################################################

################## sitronix_incell tp driver #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_SITRONIX_INCELL) += \
	sitronix_incell/sitronix_ts.o \
	sitronix_incell/sitronix_st7123_i2c.o \
	sitronix_incell/sitronix_st7123_spi.o \
	sitronix_incell/sitronix_st7123_utility.o \
	sitronix_incell/sitronix_st7123_mt.o \
	sitronix_incell/sitronix_st7123_nodes.o \
	sitronix_incell/sitronix_st7123_upgrade.o \
	sitronix_incell/sitronix_common_interface.o
########################################################

################## ovt tp driver #####################
zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM) += \
	omnivision_tcm/omnivision_common_interface.o \
	omnivision_tcm/omnivision_tcm_core.o \
	omnivision_tcm/omnivision_tcm_touch.o \
	omnivision_tcm/omnivision_tcm_device.o \
	omnivision_tcm/omnivision_tcm_testing.o \
	omnivision_tcm/omnivision_tcm_recovery.o \
	omnivision_tcm/omnivision_tcm_diagnostics.o

zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_SPI) += \
	omnivision_tcm/omnivision_tcm_spi.o \
	omnivision_tcm/omnivision_tcm_zeroflash.o

zte_tpd-$(CONFIG_TOUCHSCREEN_OMNIVISION_TCM_I2C) += \
	omnivision_tcm/omnivision_tcm_i2c.o \
	omnivision_tcm/omnivision_tcm_reflash.o
########################################################

###################CHIPONE_TDDI_V2#####################
zte_tpd-$(CONFIG_TOUCHSCREEN_CHIPONE_V2) += \
	chipone_tddi_v2/cts_i2c_driver.o \
	chipone_tddi_v2/cts_core.o \
	chipone_tddi_v2/cts_sfctrlv2.o \
	chipone_tddi_v2/cts_spi_flash.o \
	chipone_tddi_v2/cts_firmware.o \
	chipone_tddi_v2/cts_test.o \
	chipone_tddi_v2/cts_hostcomm.o \
	chipone_tddi_v2/cts_tcs.o \
	chipone_tddi_v2/cts_platform.o \
	chipone_tddi_v2/cts_tool.o \
	chipone_tddi_v2/cts_sysfs.o \
	chipone_tddi_v2/cts_common_interface.o \
	chipone_tddi_v2/cts_selftest.o \
	chipone_tddi_v2/cts_selftest_ini.o \
	chipone_tddi_v2/cts_strerror.o
######################################################

obj-$(CONFIG_TOUCHSCREEN_LCD_NOTIFY) += lcd_state_notify.o
obj-$(CONFIG_TOUCHSCREEN_VENDOR_V2) += zte_tpd.o

