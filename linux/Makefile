KDIR ?= /lib/modules/$(shell uname -r)/build

BUILD_DIR := $(CURDIR)/build
PROTOCOL_DIR := $(abspath $(CURDIR)/../protocol)
INCLUDE_DIR := $(PROTOCOL_DIR)/include
USB_IDS_DIR := $(abspath $(CURDIR)/../include)

.PHONY: all clean

all:
	# Create the kernel module build directory.
	mkdir -p $(BUILD_DIR)

	# Copy Linux driver sources.
	cp usbuskit_main.c usbuskit_i2c.c usbuskit.h $(BUILD_DIR)/

	# Copy the shared protocol implementation.
	cp $(PROTOCOL_DIR)/src/usbuskit_protocol.c $(BUILD_DIR)/

	# Generate the Kbuild Makefile for the composite module.
	printf '%s\n' \
		'obj-m += usbuskit.o' \
		'usbuskit-y := usbuskit_main.o usbuskit_i2c.o usbuskit_protocol.o' \
		'ccflags-y += -I$(INCLUDE_DIR) -I$(USB_IDS_DIR)' \
		> $(BUILD_DIR)/Makefile

	# Build the kernel module.
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules

clean:
	rm -rf $(BUILD_DIR)