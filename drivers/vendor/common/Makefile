
# Makefile for the common kernel device drivers.
obj-$(CONFIG_TOUCHSCREEN_VENDOR_V2)	+= touchscreen_v2/
obj-$(CONFIG_NXP_TFA9894_SMARTPA)	+= nxp_tfa9894/
obj-$(CONFIG_POWER_VENDOR_COMMON)	+= power/
obj-y   += vcfg/
#for AWINIC AW881XXSmart PA
obj-$(CONFIG_SND_SMARTPA_AW881XX) += awinic/
obj-$(CONFIG_SND_SOC_TFA98XX) += tfa9873/
obj-$(CONFIG_VENDOR_SQC_CHARGER)   += sqc/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V2)   += sqc_v2/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V8)   += sqc_v8/
obj-$(CONFIG_VENDOR_SQC_CHARGER_BASE)   += sqc_base/
obj-$(CONFIG_VENDOR_SQC_CHARGER_V7)   += sqc_v7/
obj-$(CONFIG_VENDOR_ZTE_DEV_MONITOR_SYSTEM)   += zlog/
obj-y   += sound/

#for sensor
obj-y   += sensor/

#for breathing light leds
obj-$(CONFIG_ZTE_BREATHING_LIGHT_LEDS) += leds/

#for zcard_det
obj-y   += zcard_det/

#lcd
obj-$(CONFIG_ZTE_LCDBL_I2C_CTRL_VSP_VSN) += zte_lcdbl/

##### Fingerprint #####
obj-$(CONFIG_ZTE_PLATFORM_FINGERPRINT)    += fingerprint/
