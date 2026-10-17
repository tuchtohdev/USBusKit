#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/usb.h>
// #include <usb.h>

#include "usbuskit_usb_ids.h"


static int usbuskit_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
    pr_info("USBusKit: USB Device connected\n");
    return 0;
}

static void usbuskit_disconnect(struct usb_interface *interface)
{
    pr_info("USBusKit: USB Device disconnected\n");
}

static const struct usb_device_id usbuskit_id_table[] = {
    { USB_DEVICE(USBUSKIT_USB_VID, USBUSKIT_USB_PID)},
    { }
};

MODULE_DEVICE_TABLE(usb, usbuskit_id_table);


static struct usb_driver usbuskit_driver = {
    .name = "USBusKit",
    .probe = usbuskit_probe,
    .disconnect = usbuskit_disconnect,
    .id_table = usbuskit_id_table,
};

static int __init usbuskit_init(void) {
    pr_info("USBusKit: init() function\n");
    return usb_register(&usbuskit_driver);
}

static void __exit usbuskit_exit(void) {
    usb_deregister(&usbuskit_driver);
    pr_info("USBusKit: exit() function\n");
}

module_init(usbuskit_init);
module_exit(usbuskit_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SUMIT");
MODULE_DESCRIPTION("USBusKit kernel module");