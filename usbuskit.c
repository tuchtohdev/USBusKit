#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include <linux/usb.h>

#include "usbuskit_usb_ids.h"

struct usbuskit_device {
    struct usb_device *udev;

    unsigned char bulk_in_endpoint;
    unsigned char bulk_out_endpoint;

    struct mutex io_mutex;
    bool disconnected;
};

static ssize_t echo_test_store(struct device *device,
                               struct device_attribute *attr,
                               const char *buf,
                               size_t count)
{
    struct usb_interface *interface = to_usb_interface(device);
    struct usbuskit_device *dev = usb_get_intfdata(interface);

    const char message[] = "Hello USBusKit";

    char *tx_buffer;
    char *rx_buffer;
    int actual_length;
    int ret;

    if (!dev)
        return -ENODEV;

    mutex_lock(&dev->io_mutex);

    if (dev->disconnected) {
        ret = -ENODEV;
        goto unlock;
    }

    tx_buffer = kzalloc(sizeof(message) - 1, GFP_KERNEL);
    if (!tx_buffer) {
        ret = -ENOMEM;
        goto unlock;
    }

    rx_buffer = kzalloc(64, GFP_KERNEL);
    if (!rx_buffer) {
        ret = -ENOMEM;
        goto free_tx;
    }

    memcpy(tx_buffer, message, sizeof(message) - 1);

    /* Send data to STM32 */
    ret = usb_bulk_msg(
        dev->udev,
        usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
        tx_buffer,
        sizeof(message) - 1,
        &actual_length,
        1000
    );

    if (ret)
        goto unlock;

    if (actual_length != sizeof(message) - 1) {
        ret = -EIO;
        goto unlock;
    }

    /* Receive echo from STM32 */
    ret = usb_bulk_msg(
        dev->udev,
        usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpoint),
        rx_buffer,
        64,
        &actual_length,
        1000
    );

    if (ret)
        goto unlock;

    if (actual_length != sizeof(message) - 1 ||
        memcmp(message, rx_buffer, sizeof(message) - 1) != 0) {
        ret = -EIO;
        goto free_buffers;
    }

    pr_info("USBusKit: Kernel USB bulk echo successful\n");

    ret = count;

free_buffers:
    kfree(rx_buffer);

free_tx:
    kfree(tx_buffer);

unlock:
    mutex_unlock(&dev->io_mutex);
    return ret;
}

static DEVICE_ATTR_WO(echo_test);

static int usbuskit_probe(struct usb_interface *interface,
                          const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i, ret;
    struct usbuskit_device *dev;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);

    if (!dev)
        return -ENOMEM;

    mutex_init(&dev->io_mutex);

    dev->udev = usb_get_dev(interface_to_usbdev(interface));

    iface_desc = interface->cur_altsetting;

    pr_info("USBusKit: Interface has %u endpoints\n",
            iface_desc->desc.bNumEndpoints);

    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {

        endpoint = &iface_desc->endpoint[i].desc;

        if (usb_endpoint_is_bulk_in(endpoint)) {
            dev->bulk_in_endpoint = endpoint->bEndpointAddress;
            pr_info("USBusKit: Bulk IN endpoint: 0x%02X\n",
                    endpoint->bEndpointAddress);
        }
        else if (usb_endpoint_is_bulk_out(endpoint)) {
            dev->bulk_out_endpoint = endpoint->bEndpointAddress;
            pr_info("USBusKit: Bulk OUT endpoint: 0x%02X\n",
                    endpoint->bEndpointAddress);
        }
    }

    if (!dev->bulk_in_endpoint || !dev->bulk_out_endpoint) {
        pr_err("USBusKit: Required bulk endpoints not found\n");

        usb_put_dev(dev->udev);
        kfree(dev);

        return -ENODEV;
    }

    usb_set_intfdata(interface, dev);

    pr_info("USBusKit: USB Device connected\n");

    ret = device_create_file(&interface->dev, &dev_attr_echo_test);
    if (ret) {
        usb_set_intfdata(interface, NULL);
        usb_put_dev(dev->udev);
        kfree(dev);
        return ret;
    }

    return 0;
}

static void usbuskit_disconnect(struct usb_interface *interface)
{
    struct usbuskit_device *dev = usb_get_intfdata(interface);

    device_remove_file(&interface->dev, &dev_attr_echo_test);

    if (!dev)
        return;

    mutex_lock(&dev->io_mutex);
    dev->disconnected = true;
    mutex_unlock(&dev->io_mutex);

    usb_set_intfdata(interface, NULL);

    usb_put_dev(dev->udev);
    kfree(dev);

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