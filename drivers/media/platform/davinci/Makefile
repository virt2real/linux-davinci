#
# Makefile for the davinci video device drivers.
#

# VPIF
obj-$(CONFIG_VIDEO_DAVINCI_VPIF) += vpif.o

# DaVinci VPIF Display driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_DISPLAY) += vpif_display.o
# DaVinci VPIF Capture driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_CAPTURE) += vpif_capture.o

# Capture: DM6446 and DM355
obj-$(CONFIG_VIDEO_VPSS_SYSTEM) += vpss.o
obj-$(CONFIG_VIDEO_VPFE_CAPTURE) += vpfe_capture.o vpfe_ccdc.o vpfe_resizer.o vpfe_previewer.o vpfe_aew.o vpfe_af.o vpfe_video.o
obj-$(CONFIG_VIDEO_DM6446_CCDC) += dm644x_ccdc.o
obj-$(CONFIG_VIDEO_DM355_CCDC) += dm355_ccdc.o
obj-$(CONFIG_VIDEO_ISIF) += dm365_ccdc.o
obj-$(CONFIG_VIDEO_DM644X_VPBE) += vpbe.o vpbe_osd.o vpbe_venc.o
obj-$(CONFIG_VIDEO_VPBE_DISPLAY) += vpbe_display.o
obj-$(CONFIG_VIDEO_VPBE_FB) += vpbe_fb.o

dm355_af_driver-objs            += dm355_af.o dm355_af_hw.o
obj-$(CONFIG_VIDEO_DM355_AF)    += dm355_af_driver.o
dm355_aew_driver-objs           += dm355_aew.o dm355_aew_hw.o
obj-$(CONFIG_VIDEO_DM355_AEW)   += dm355_aew_driver.o

dm365_a3_hw_driver-objs := dm365_a3_hw.o
obj-$(CONFIG_VIDEO_DM365_3A_HW) += dm365_a3_hw_driver.o
dm365_af_driver-objs := dm365_af.o
obj-$(CONFIG_VIDEO_DM365_AF)    += dm365_af_driver.o
dm365_aew_driver-objs := dm365_aew.o
obj-$(CONFIG_VIDEO_DM365_AEW)   += dm365_aew_driver.o

dm365_imp-objs                  := dm365_ipipe.o dm365_def_para.o \
                                        dm365_ipipe_hw.o dm3xx_ipipe.o
obj-$(CONFIG_DM365_IPIPE)       += dm365_imp.o

dm355_imp-objs                  := dm355_ipipe.o dm355_def_para.o \
                                        dm355_ipipe_hw.o
obj-$(CONFIG_DM355_IPIPE)       += dm355_imp.o

obj-$(CONFIG_DM355_IPIPE)       += imp_common.o
obj-$(CONFIG_DM365_IPIPE)       += imp_common.o
