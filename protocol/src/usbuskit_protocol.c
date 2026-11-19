#include "usbuskit_protocol.h"

void usbuskit_put_u16_le(usbuskit_u8 *buf, usbuskit_u16 value)
{
    buf[0] = (usbuskit_u8)(value & 0xFF);
    buf[1] = (usbuskit_u8)((value >> 8) & 0xFF);
}

usbuskit_u16 usbuskit_get_u16_le(const usbuskit_u8 *buf)
{
    return (usbuskit_u16)buf[0] |
           ((usbuskit_u16)buf[1] << 8);
}

void usbuskit_encode_header(
    usbuskit_u8 *buffer,
    const struct usbuskit_header *header)
{
    buffer[USBUSKIT_HDR_PROTOCOL_OFFSET] = header->protocol_id;
    buffer[USBUSKIT_HDR_INSTANCE_OFFSET] = header->instance_id;
    buffer[USBUSKIT_HDR_OPCODE_OFFSET] = header->opcode;

    usbuskit_put_u16_le(
        &buffer[USBUSKIT_HDR_LENGTH_OFFSET],
        header->payload_length
    );
}

void usbuskit_decode_header(
    const usbuskit_u8 *buffer,
    struct usbuskit_header *header)
{
    header->protocol_id = buffer[USBUSKIT_HDR_PROTOCOL_OFFSET];
    header->instance_id = buffer[USBUSKIT_HDR_INSTANCE_OFFSET];
    header->opcode = buffer[USBUSKIT_HDR_OPCODE_OFFSET];

    header->payload_length = usbuskit_get_u16_le(
        &buffer[USBUSKIT_HDR_LENGTH_OFFSET]
    );
}