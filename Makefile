KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR := $(CURDIR)/build

.PHONY: all clean

all:
	mkdir -p $(BUILD_DIR)
	cp usbuskit.c $(BUILD_DIR)/
	printf 'obj-m += usbuskit.o\n' > $(BUILD_DIR)/Makefile
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules

clean:
	rm -rf $(BUILD_DIR)