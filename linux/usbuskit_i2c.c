#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#include "usbuskit_protocol.h"
#include "usbuskit.h"

static int usbuskit_i2c_calc_payload_length(
    const struct i2c_msg *msgs,
    int num)
{
    size_t payload_length = 1; /* Message count */
    int i;

    if (!msgs || num <= 0 || num > USBUSKIT_I2C_MAX_MESSAGES)
        return -EINVAL;

    /* Firmware currently supports only single-message writes. */
    if (num != 1)
        return -EOPNOTSUPP;
    if (msgs[0].flags & I2C_M_RD)
        return -EOPNOTSUPP;

    for (i = 0; i < num; i++) {
        /* Only 7-bit addressing and ordinary read/write supported */
        /* DMA_SAFE is a Linux buffer property, not an I²C bus operation so allow it. */
        if (msgs[i].flags & ~(I2C_M_RD | I2C_M_DMA_SAFE))
            return -EOPNOTSUPP;
        if (msgs[i].addr > 0x7F)
            return -EINVAL;

        if (msgs[i].len && !msgs[i].buf)
            return -EINVAL;

        // Address-only transfers are not yet supported by the firmware
        if (msgs[i].len == 0)
            return -EOPNOTSUPP;

        payload_length += USBUSKIT_I2C_MSG_HEADER_SIZE;

        if (!(msgs[i].flags & I2C_M_RD))
            payload_length += msgs[i].len;

        if (payload_length > USBUSKIT_MAX_REQUEST_PAYLOAD)
            return -EMSGSIZE;
    }

    return (int)payload_length;
}

static int usbuskit_i2c_serialize_request(
    const struct i2c_msg *msgs,
    int num,
    u8 instance,
    u8 *buffer,
    size_t buffer_size)
{
    int payload_length;
    size_t offset = USBUSKIT_HEADER_SIZE;
    int i;

    struct usbuskit_header header = {
        .protocol_id = USBUSKIT_PROTO_I2C,
        .instance_id = instance,
        .opcode = USBUSKIT_I2C_OP_TRANSFER,
    };

    if (!buffer)
        return -EINVAL;

    payload_length = usbuskit_i2c_calc_payload_length(msgs, num);
    if (payload_length < 0)
        return payload_length;

    if (buffer_size < USBUSKIT_HEADER_SIZE + payload_length)
        return -EMSGSIZE;

    header.payload_length = payload_length;
    usbuskit_encode_header(buffer, &header);

    buffer[offset++] = (u8)num;

    for (i = 0; i < num; i++) {
        u16 addr_flags = msgs[i].addr;

        if (msgs[i].flags & I2C_M_RD)
            addr_flags |= USBUSKIT_I2C_FLAG_READ;

        usbuskit_put_u16_le(&buffer[offset], addr_flags);
        offset += 2;

        usbuskit_put_u16_le(&buffer[offset], msgs[i].len);
        offset += 2;

        if (!(msgs[i].flags & I2C_M_RD)) {
            memcpy(&buffer[offset], msgs[i].buf, msgs[i].len);
            offset += msgs[i].len;
        }
    }

    return (int)offset; /* Total packet size, including header */
}

static int usbuskit_i2c_status_to_errno(u8 status)
{
    switch (status) {
    case USBUSKIT_I2C_STATUS_SUCCESS:
        return 0;

    case USBUSKIT_I2C_STATUS_ADDRESS_NACK:
        return -ENXIO;

    case USBUSKIT_I2C_STATUS_DATA_NACK:
        return -EREMOTEIO;

    case USBUSKIT_I2C_STATUS_TIMEOUT:
        return -ETIMEDOUT;

    case USBUSKIT_I2C_STATUS_ARBITRATION_LOST:
        return -EAGAIN;

    case USBUSKIT_I2C_STATUS_BUS_ERROR:
        return -EIO;

    case USBUSKIT_I2C_STATUS_BUS_BUSY:
        return -EBUSY;

    case USBUSKIT_I2C_STATUS_INVALID_REQUEST:
        return -EINVAL;

    case USBUSKIT_I2C_STATUS_UNSUPPORTED:
        return -EOPNOTSUPP;

    default:
        /* Unknown statuses indicate an invalid protocol response. */
        return -EPROTO;
    }
}

static int usbuskit_i2c_parse_response(
    struct i2c_msg *msgs,
    int num,
    const u8 *buffer,
    size_t buffer_size,
    u8 expected_instance)
{
    struct usbuskit_header header;
    size_t offset;
    size_t end;
    int first_error = 0;
    int i;
    
    /* Validate arguments before accessing the response buffer. */
    if (!msgs || !buffer || num <= 0 || num > USBUSKIT_I2C_MAX_MESSAGES)
    return -EINVAL;
    
    /* A valid response must contain the common 5-byte header. */
    if (buffer_size < USBUSKIT_HEADER_SIZE)
    return -EPROTO;
    
    usbuskit_decode_header(buffer, &header);
    
    pr_info("USBusKit I2C: Response protocol=%u instance=%u opcode=%u payload_len=%u\n",
        header.protocol_id,
        header.instance_id,
        header.opcode,
        header.payload_length);

    /* Ensure the response belongs to the expected I2C instance. */
    if (header.protocol_id != USBUSKIT_PROTO_I2C ||
        header.instance_id != expected_instance ||
        header.opcode != USBUSKIT_I2C_OP_TRANSFER)
        return -EPROTO;

    /* Reject oversized or incomplete response payloads. */
    if (header.payload_length > USBUSKIT_MAX_RESPONSE_PAYLOAD ||
        header.payload_length != buffer_size - USBUSKIT_HEADER_SIZE)
        return -EPROTO;

    offset = USBUSKIT_HEADER_SIZE;
    end = buffer_size;

    /* The first payload byte contains the message count. */
    if (offset >= end)
        return -EPROTO;

    if (buffer[offset++] != (u8)num)
        return -EPROTO;

    /*
     * PASS 1: Validate every status and read-data segment.
     * Do not modify any i2c_msg buffers during this pass.
     */
    for (i = 0; i < num; i++) {
        u8 status;
        int error;

        /* Every message must have a status byte. */
        if (offset >= end)
            return -EPROTO;

        status = buffer[offset++];

        if (first_error) {
            /* Messages after the first failure must not execute. */
            if (status != USBUSKIT_I2C_STATUS_NOT_EXECUTED)
                return -EPROTO;

            continue;
        }

        if (status != USBUSKIT_I2C_STATUS_SUCCESS) {
            error = usbuskit_i2c_status_to_errno(status);

            /* NOT_EXECUTED cannot be the first failure. */
            if (status == USBUSKIT_I2C_STATUS_NOT_EXECUTED ||
                error == -EPROTO)
                return -EPROTO;

            first_error = error;
            continue;
        }

        /* Successful writes contain no response data. */
        if (!(msgs[i].flags & I2C_M_RD))
            continue;

        /* Ensure the expected read bytes fit in the response. */
        if (msgs[i].len > end - offset)
            return -EPROTO;

        if (msgs[i].len && !msgs[i].buf)
            return -EINVAL;

        offset += msgs[i].len;
    }

    /* Reject unexpected trailing bytes. */
    if (offset != end)
        return -EPROTO;

    /* A failed transaction returns an error without copying read data. */
    if (first_error)
        return first_error;

    /*
     * PASS 2: The complete response is valid.
     * Copy each successful read into its original Linux I2C buffer.
     */
    offset = USBUSKIT_HEADER_SIZE + 1;

    for (i = 0; i < num; i++) {
        /* Skip the status byte already validated in pass 1. */
        offset++;

        if ((msgs[i].flags & I2C_M_RD) && msgs[i].len) {
            memcpy(msgs[i].buf, &buffer[offset], msgs[i].len);
            offset += msgs[i].len;
        }
    }

    /* Linux master_xfer() expects the number of completed messages. */
    return num;
}

static int usbuskit_i2c_master_xfer(
    struct i2c_adapter *adapter,
    struct i2c_msg *msgs,
    int num)
{
    struct usbuskit_device *dev = i2c_get_adapdata(adapter);
    u8 *request;
    u8 *response;
    size_t response_len = 0;
    int request_len;
    int ret;

    pr_info("USBusKit I2C: master_xfer called, num=%d\n", num);

    /* Allocate DMA-compatible buffers for USB bulk transfers. */
    request = kmalloc(
        USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_REQUEST_PAYLOAD,
        GFP_KERNEL
    );

    if (!request)
        return -ENOMEM;

    response = kmalloc(
        USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_RESPONSE_PAYLOAD,
        GFP_KERNEL
    );

    if (!response) {
        ret = -ENOMEM;
        goto free_request;
    }

    /* Convert Linux I²C messages into a USBusKit request packet. */
    request_len = usbuskit_i2c_serialize_request(
        msgs,
        num,
        0, // Default for now
        request,
        USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_REQUEST_PAYLOAD
    );
    pr_info("USBusKit I2C: Request serialized, length=%d\n",
        request_len);

    if (request_len < 0) {
        ret = request_len;
        goto free_response;
    }

    /* Send the request to the MCU and receive its response. */
    ret = usbuskit_usb_transfer(
        dev,
        request,
        request_len,
        response,
        USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_RESPONSE_PAYLOAD,
        &response_len
    );

    if (ret)
        goto free_response;

    /* Validate the response and populate the original read buffers. */
    ret = usbuskit_i2c_parse_response(
        msgs,
        num,
        response,
        response_len,
        0 /* I²C instance 0, for our initial implementation. */
    );

free_response:
    kfree(response);

free_request:
    kfree(request);

    return ret;
}

static u32 usbuskit_i2c_functionality(struct i2c_adapter *adapter)
{
    /* Initially support ordinary 7-bit I²C transfers. */
    return I2C_FUNC_I2C;
}

static const struct i2c_algorithm usbuskit_i2c_algorithm = {
    .master_xfer = usbuskit_i2c_master_xfer,
    .functionality = usbuskit_i2c_functionality,
};

int usbuskit_i2c_register(struct usbuskit_device *dev)
{
    struct i2c_adapter *adapter;
    int ret;

    /* Allocate an adapter managed by the USBusKit device. */
    adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
    if (!adapter)
        return -ENOMEM;

    /* Associate the adapter with our I²C algorithm. */
    adapter->owner = THIS_MODULE;
    adapter->algo = &usbuskit_i2c_algorithm;

    /* Associate the adapter with its parent USB interface. */
    adapter->dev.parent = &dev->interface->dev;

    /* Provide a descriptive adapter name. */
    strscpy(adapter->name, "USBusKit I2C Adapter",
            sizeof(adapter->name));

    /* Allow master_xfer() to retrieve our USB device context. */
    i2c_set_adapdata(adapter, dev);

    /* Register the adapter with the Linux I²C subsystem. */
    ret = i2c_add_adapter(adapter);
    if (ret) {
        kfree(adapter);
        return ret;
    }

    dev->i2c_adapter = adapter;

    return 0;
}

void usbuskit_i2c_unregister(struct usbuskit_device *dev)
{
    if (!dev->i2c_adapter)
        return;

    /* Remove the adapter from the Linux I²C subsystem. */
    i2c_del_adapter(dev->i2c_adapter);

    /* Release the adapter allocation. */
    kfree(dev->i2c_adapter);
    dev->i2c_adapter = NULL;
}