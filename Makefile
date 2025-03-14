obj-m := dma_phys_addr.o

ARCH := arm64
CROSS_COMPILE := aarch64-linux-gnu-
KDIR := /root/kernel-arm64/build/ # Change this to your ARM64 kernel headers path
PWD := $(shell pwd)
EXTRA_CFLAGS += -DMODULE -D__KERNEL__

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
