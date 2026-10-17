#include <string.h>
#include "tusb.h"
#include "usbuskit_usb_ids.h"

#define USB_VID USBUSKIT_USB_VID
#define USB_PID USBUSKIT_USB_PID

enum
{
    ITF_NUM_VENDOR = 0,
    ITF_NUM_TOTAL
};

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_VENDOR
};

static const tusb_desc_device_t desc_device =
{
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,

    .bNumConfigurations = 1
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

static const uint8_t desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(
        1,
        ITF_NUM_TOTAL,
        0,
        CONFIG_TOTAL_LEN,
        0,
        100
    ),

    TUD_VENDOR_DESCRIPTOR(
        ITF_NUM_VENDOR,
        STRID_VENDOR,
        0x01,
        0x81,
        64
    )
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static const char *string_desc_arr[] =
{
    (const char[]){0x09, 0x04},
    "USBusKit",
    "USBusKit Bluepill Vendor",
    "000001",
    "USBusKit Vendor Interface"
};

static uint16_t desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0)
    {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    }
    else
    {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
        {
            return NULL;
        }

        const char *str = string_desc_arr[index];

        chr_count = (uint8_t)strlen(str);

        if (chr_count > 31)
        {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++)
        {
            desc_str[1 + i] = (uint16_t)str[i];
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                             (2 * chr_count + 2));

    return desc_str;
}