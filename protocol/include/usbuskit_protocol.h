#ifndef USBUSKIT_PROTOCOL_H
#define USBUSKIT_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/types.h>

typedef u8  usbuskit_u8;
typedef u16 usbuskit_u16;

#else
#include <stdint.h>

typedef uint8_t  usbuskit_u8;
typedef uint16_t usbuskit_u16;
#endif

/*
+-------------+-------------+----------+----------------+------------------+
| Protocol ID | Instance ID |  Opcode  | Payload Length |     Payload      |
+-------------+-------------+----------+----------------+------------------+
|   1 byte    |   1 byte    |  1 byte  |    2 bytes     |     N bytes      |
+-------------+-------------+----------+----------------+------------------+

Header              : 5 bytes
Total packet size   : 5 + N bytes
All multibyte fields use little-endian encoding.

Protocol ID: ID of the protocol supported
Instance ID: Tracks the instance incase of protocols have multiple 
*/

struct usbuskit_header {
    usbuskit_u8 protocol_id;
    usbuskit_u8 instance_id;
    usbuskit_u8 opcode;
    usbuskit_u16 payload_length;
};

#define USBUSKIT_HDR_PROTOCOL_OFFSET   0
#define USBUSKIT_HDR_INSTANCE_OFFSET   1
#define USBUSKIT_HDR_OPCODE_OFFSET     2
#define USBUSKIT_HDR_LENGTH_OFFSET     3

#define USBUSKIT_HEADER_SIZE           5


/* Protocol IDs: List of supported protcols*/
#define USBUSKIT_PROTO_I2C       0x01
#define USBUSKIT_PROTO_SPI       0x02
#define USBUSKIT_PROTO_GPIO      0x03
#define USBUSKIT_PROTO_UART      0x04
#define USBUSKIT_PROTO_CAN       0x05


/* Both functions require a buffer containing at least USBUSKIT_HEADER_SIZE bytes.
 * The caller is responsible for checking that.
 */
void usbuskit_encode_header(
    usbuskit_u8 *buffer,
    const struct usbuskit_header *header
);

void usbuskit_decode_header(
    const usbuskit_u8 *buffer,
    struct usbuskit_header *header
);

///// I2C Section

/*
Address + Flags : 2 bytes
Data Length     : 2 bytes
Write Data      : N bytes (only for writes)

I²C transfer request
+----------------------+-------------------+
| USBusKit Header      | 5 bytes           |
+----------------------+-------------------+
| Message Count        | 1 byte            |
+----------------------+-------------------+
| Message 0                                |
|   Address + Flags    | 2 bytes           |
|   Data Length        | 2 bytes           |
|   Write Data         | N bytes (if WRITE)|
+----------------------+-------------------+
| Message 1                                |
|   Address + Flags    | 2 bytes           |
|   Data Length        | 2 bytes           |
|   Write Data         | N bytes (if WRITE)|
+----------------------+-------------------+
| ...                                      |
+------------------------------------------+

I²C transfer response
+----------------------+-------------------+
| USBusKit Header      | 5 bytes           |
+----------------------+-------------------+
| Message Count        | 1 byte            |
+----------------------+-------------------+
| Message 0                                |
|   Status             | 1 byte            |
|   Read Data          | N bytes (if READ  |
|                      |          succeeds)|
+----------------------+-------------------+
| Message 1                                |
|   Status             | 1 byte            |
|   Read Data          | N bytes (if READ  |
|                      |          succeeds)|
+----------------------+-------------------+
| ...                                      |
+------------------------------------------+


*/
#define USBUSKIT_I2C_MSG_HEADER_SIZE  4

/* I2C opcodes */
#define USBUSKIT_I2C_OP_TRANSFER 0x01

/* I2C address and flags */
#define USBUSKIT_I2C_ADDR_MASK   0x03FF
#define USBUSKIT_I2C_FLAG_READ   (1U << 10)
#define USBUSKIT_I2C_FLAG_ADDR10 (1U << 11)

/* I2C status codes */
#define USBUSKIT_I2C_STATUS_SUCCESS          0x00
#define USBUSKIT_I2C_STATUS_ADDRESS_NACK     0x01
#define USBUSKIT_I2C_STATUS_DATA_NACK        0x02
#define USBUSKIT_I2C_STATUS_TIMEOUT          0x03
#define USBUSKIT_I2C_STATUS_ARBITRATION_LOST 0x04
#define USBUSKIT_I2C_STATUS_BUS_ERROR        0x05
#define USBUSKIT_I2C_STATUS_BUS_BUSY         0x06
#define USBUSKIT_I2C_STATUS_INVALID_REQUEST  0x07
#define USBUSKIT_I2C_STATUS_UNSUPPORTED      0x08
#define USBUSKIT_I2C_STATUS_NOT_EXECUTED     0x09

/* Initial implementation limits */
#define USBUSKIT_I2C_MAX_MESSAGES       8U
#define USBUSKIT_MAX_REQUEST_PAYLOAD    256U
#define USBUSKIT_MAX_RESPONSE_PAYLOAD   256U
#define USBUSKIT_MAX_PACKET_SIZE        (USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_REQUEST_PAYLOAD)
#define USBUSKIT_MAX_RESPONSE_SIZE      (USBUSKIT_HEADER_SIZE + USBUSKIT_MAX_RESPONSE_PAYLOAD)

/*
 * Write a 16-bit unsigned integer in little-endian format.
 *
 * buf must point to at least 2 writable bytes.
 */
void usbuskit_put_u16_le(usbuskit_u8 *buf, usbuskit_u16 value);

/*
 * Read a 16-bit unsigned integer in little-endian format.
 *
 * buf must point to at least 2 readable bytes.
 */
usbuskit_u16 usbuskit_get_u16_le(const usbuskit_u8 *buf);

#endif /* USBUSKIT_PROTOCOL_H */