CROSS_PREFIX	?= arm-linux-gnueabi
CROSS_COMPILE	?= $(CROSS_PREFIX)-

ARCH_DIR		 = arch/$(ARCH)

PLATFORM_CPUARCH	 = cortex-a15
PLATFORM_CFLAGS	 	 = -mcpu=$(PLATFORM_CPUARCH) -mthumb -fno-short-enums
PLATFORM_SFLAGS	 	 = -mcpu=$(PLATFORM_CPUARCH)
PLATFORM_CPPFLAGS	 = -I$(ARCH_DIR)/include -DNUM_CPUS=1 -DNUM_THREADS=2
PLATFORM_CPPFLAGS	+= -DWITH_STACK_CANARIES=1

DEBUG		?= 1
ifeq ($(DEBUG),1)
PLATFORM_CFLAGS += -O0
else
PLATFORM_CFLAGS += -Os
endif
PLATFORM_CFLAGS += -g -g3
PLATFORM_SFLAGS += -g -g3

SUBDIRS += $(addprefix $(ARCH_DIR)/, kern libc sm tee)
