CROSS_PREFIX	?= arm-linux-gnueabi
CROSS_COMPILE	?= $(CROSS_PREFIX)-

ARCH_DIR		= arch/$(ARCH)

PLATFORM_CPUARCH	= cortex-a15
PLATFORM_CFLAGS	 	= -mcpu=$(PLATFORM_CPUARCH) -mthumb -fno-short-enums
PLATFORM_SFLAGS	 	= -mcpu=$(PLATFORM_CPUARCH)
PLATFORM_CPPFLAGS	= -I$(ARCH_DIR)/include -DNUM_CPUS=1 -DNUM_THREADS=2

DEBUG		?= 1
ifeq ($(DEBUG),1)
PLATFORM_CFLAGS += -O0 -g
else
PLATFORM_CFLAGS += -Os
endif

SUBDIRS += $(addprefix $(ARCH_DIR)/, kern libc sm tee)
