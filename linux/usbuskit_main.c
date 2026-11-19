#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include <linux/usb.h>

#include "usbuskit_usb_ids.h"

#include "usbuskit.h"

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

int usbuskit_usb_transfer(
    struct usbuskit_device *dev,
    const u8 *request,
    size_t request_len,
    u8 *response,
    size_t response_capacity,
    size_t *response_len)
{
    int ret;
    int actual_length;

    /* Validate arguments before accessing buffers or device state. */
    if (!dev || !request || !response || !response_len ||
        !request_len || !response_capacity)
        return -EINVAL;

    /* usb_bulk_msg() accepts transfer lengths as integers. */
    if (request_len > INT_MAX || response_capacity > INT_MAX)
        return -EMSGSIZE;

    *response_len = 0;

    /*
     * Serialize communication over the shared USB endpoints.
     * Keep the lock until the response has been received.
     */
    mutex_lock(&dev->io_mutex);

    /* Reject transfers after the USB device has disconnected. */
    if (dev->disconnected) {
        ret = -ENODEV;
        goto unlock;
    }
    
    pr_info("USBusKit USB: Sending request, length=%zu\n", request_len);
    /* Send the complete USBusKit request over bulk OUT. */
    ret = usb_bulk_msg(
        dev->udev,
        usb_sndbulkpipe(dev->udev, dev->bulk_out_endpoint),
        (void *)request,
        (int)request_len,
        &actual_length,
        1000
    );
    pr_info("USBusKit USB: Bulk OUT returned %d, transferred=%d\n",
        ret, actual_length);

    if (ret)
        goto unlock;

    /* A short write means the complete request was not transmitted. */
    if (actual_length != request_len) {
        ret = -EIO;
        goto unlock;
    }

    /* Receive the MCU's response over bulk IN. */
    ret = usb_bulk_msg(
        dev->udev,
        usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpoint),
        response,
        (int)response_capacity,
        &actual_length,
        1000
    );
    pr_info("USBusKit USB: Bulk IN returned %d, received=%d\n",
        ret, actual_length);

    print_hex_dump(KERN_INFO,
               "USBusKit RX: ",
               DUMP_PREFIX_OFFSET,
               16, 1,
               response, actual_length,
               false);

    if (ret)
        goto unlock;

    /* Return the number of response bytes received. */
    *response_len = actual_length;

unlock:
    mutex_unlock(&dev->io_mutex);
    return ret;
}

static int usbuskit_probe(struct usb_interface *interface,
                         const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    struct usbuskit_device *dev;
    int i, ret;

    /* Allocate memory for the USBusKit device context. */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    /* Initialize the mutex used to serialize USB transfers. */
    mutex_init(&dev->io_mutex);

    /* Obtain a reference to the USB device and save its interface. */
    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    /* Retrieve the currently active USB interface descriptor. */
    iface_desc = interface->cur_altsetting;

    pr_info("USBusKit: Interface has %u endpoints\n",
            iface_desc->desc.bNumEndpoints);

    /* Discover the bulk IN and bulk OUT endpoints. */
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

    /* Both bulk endpoints are required for USBusKit communication. */
    if (!dev->bulk_in_endpoint || !dev->bulk_out_endpoint) {
        pr_err("USBusKit: Required bulk endpoints not found\n");

        ret = -ENODEV;
        goto error_cleanup;
    }

    /* Associate the device context with this USB interface. */
    usb_set_intfdata(interface, dev);

    /* Register the USBusKit I²C adapter with the Linux I²C subsystem. */
    ret = usbuskit_i2c_register(dev);
    if (ret) {
        dev_err(&interface->dev,
                "USBusKit: Failed to register I2C adapter: %d\n",
                ret);

        goto error_cleanup;
    }

    /* Create the temporary sysfs attribute for USB bulk echo testing. */
    ret = device_create_file(&interface->dev, &dev_attr_echo_test);
    if (ret) {
        dev_err(&interface->dev,
                "USBusKit: Failed to create echo_test attribute: %d\n",
                ret);

        goto error_unregister_i2c;
    }

    pr_info("USBusKit: USB Device connected\n");

    return 0;

error_unregister_i2c:
    /* Unregister the I²C adapter if sysfs attribute creation failed. */
    usbuskit_i2c_unregister(dev);

error_cleanup:
    /* Remove the interface's reference to the device context. */
    usb_set_intfdata(interface, NULL);

    /* Release the USB device reference acquired during probe. */
    usb_put_dev(dev->udev);

    /* Free the allocated device context. */
    kfree(dev);

    return ret;
}

static void usbuskit_disconnect(struct usb_interface *interface)
{
    struct usbuskit_device *dev = usb_get_intfdata(interface);

    /* Ensure a valid device context exists. */
    if (!dev)
        return;

    /*
     * Prevent new USB transfers from starting.
     * Wait for any currently active transfer to finish.
     */
    mutex_lock(&dev->io_mutex);
    dev->disconnected = true;
    mutex_unlock(&dev->io_mutex);

    /* Remove the temporary USB echo test sysfs attribute. */
    device_remove_file(&interface->dev, &dev_attr_echo_test);

    /*
     * Unregister the Linux I²C adapter.
     * This also waits for ongoing I²C transfers to finish.
     */
    usbuskit_i2c_unregister(dev);

    /* Remove the USB interface's reference to our device context. */
    usb_set_intfdata(interface, NULL);

    /* Release the USB device reference acquired during probe. */
    usb_put_dev(dev->udev);

    /* Free the allocated device context. */
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