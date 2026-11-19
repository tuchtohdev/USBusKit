#ifndef USBUSKIT_LINUX_H
#define USBUSKIT_LINUX_H

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/i2c.h>

/* State associated with one connected USBusKit device. */
struct usbuskit_device {
    struct usb_device *udev;
    struct usb_interface *interface;
    unsigned char bulk_in_endpoint;
    unsigned char bulk_out_endpoint;
    struct mutex io_mutex;
    bool disconnected;

    /* Linux I²C adapter associated with this USB device. */
    struct i2c_adapter *i2c_adapter;
};

/* Execute one synchronous USBusKit request-response exchange. */
int usbuskit_usb_transfer(
    struct usbuskit_device *dev,
    const u8 *request,
    size_t request_len,
    u8 *response,
    size_t response_capacity,
    size_t *response_len
);

int usbuskit_i2c_register(struct usbuskit_device *dev);
void usbuskit_i2c_unregister(struct usbuskit_device *dev);

#endif /* USBUSKIT_LINUX_H */