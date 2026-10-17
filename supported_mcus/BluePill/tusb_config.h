#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

/* STM32F103 USB Full-Speed device controller */
#define CFG_TUSB_MCU OPT_MCU_STM32F1

/* Use USB device mode */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* No operating system (bare-metal firmware) */
#define CFG_TUSB_OS OPT_OS_NONE

/* USB device endpoint 0 size */
#define CFG_TUD_ENDPOINT0_SIZE 64

/* Enable one CDC vendor interface */
#define CFG_TUD_VENDOR 1

/* Disable other USB device classes */
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_CDC 0

/* CDC buffers */
#define CFG_TUD_VENDOR_RX_BUFSIZE 64
#define CFG_TUD_VENDOR_TX_BUFSIZE 64

#endif /* TUSB_CONFIG_H_ */