#include "stm32f103xb.h"
#include "tusb.h"

#include "usbuskit_protocol.h"

static volatile uint32_t g_milliseconds = 0;


static uint8_t rx_buffer[USBUSKIT_MAX_PACKET_SIZE];
static uint16_t rx_byte_count = 0;

static uint8_t tx_buffer[USBUSKIT_MAX_RESPONSE_SIZE];
static uint16_t tx_length = 0;
static bool tx_pending = false;

/* --------------------------------------------------
 * System clock: 72 MHz
 * USB clock:    48 MHz
 * -------------------------------------------------- */

static void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    FLASH->ACR |= FLASH_ACR_PRFTBE;
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_2;

    RCC->CFGR &= ~(RCC_CFGR_HPRE |
                   RCC_CFGR_PPRE1 |
                   RCC_CFGR_PPRE2 |
                   RCC_CFGR_USBPRE);

    RCC->CFGR |= RCC_CFGR_PPRE1_DIV2;

    RCC->CFGR &= ~(RCC_CFGR_PLLSRC |
                   RCC_CFGR_PLLXTPRE |
                   RCC_CFGR_PLLMULL);

    RCC->CFGR |= RCC_CFGR_PLLSRC |
                 RCC_CFGR_PLLMULL9;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_PLL;

    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    SystemCoreClockUpdate();
}

static void i2c_init(void)
{
    /*
     * Enable clocks for:
     * GPIOB (APB2)
     * I2C1  (APB1)
     */
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    /*
     * PB6 = I2C1_SCL
     * PB7 = I2C1_SDA
     *
     * STM32F1 GPIO configuration:
     * MODE = 11 : Output mode, 50 MHz
     * CNF  = 11 : Alternate-function open-drain
     *
     * Each pin therefore uses configuration 0xF.
     */
    GPIOB->CRL &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB->CRL |=  ((0xFU << 24) | (0xFU << 28));

    /*
     * Reset I2C1 peripheral registers.
     * Release reset before configuring timing.
     */
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

    /* Keep the I2C peripheral disabled during configuration. */
    I2C1->CR1 &= ~I2C_CR1_PE;

    /*
     * PCLK1 = 36 MHz.
     * CR2.FREQ must contain the APB1 frequency in MHz.
     */
    I2C1->CR2 = 36U;

    /*
     * Standard mode: 100 kHz.
     *
     * CCR = PCLK1 / (2 * SCL)
     *     = 36 MHz / (2 * 100 kHz)
     *     = 180
     *
     * FS = 0 selects standard mode.
     */
    I2C1->CCR = 180U;

    /*
     * Maximum rise time for standard mode = 1000 ns.
     *
     * TRISE = 36 MHz * 1000 ns + 1 = 37
     */
    I2C1->TRISE = 37U;

    /* Enable ACK generation for future read operations. */
    I2C1->CR1 |= I2C_CR1_ACK;

    /* Enable I2C1 peripheral. */
    I2C1->CR1 |= I2C_CR1_PE;
}

#define I2C_POLL_TIMEOUT  100000U

#define I2C_POLL_TIMEOUT 100000U

static uint8_t i2c_wait_for_flag(uint32_t flag, bool address_phase)
{
    uint32_t timeout = I2C_POLL_TIMEOUT;

    while (timeout--)
    {
        uint32_t sr1 = I2C1->SR1;

        /* Arbitration was lost to another controller. */
        if (sr1 & I2C_SR1_ARLO)
            return USBUSKIT_I2C_STATUS_ARBITRATION_LOST;

        /* I2C bus error detected. */
        if (sr1 & I2C_SR1_BERR)
            return USBUSKIT_I2C_STATUS_BUS_ERROR;

        /* Slave did not acknowledge the address or data. */
        if (sr1 & I2C_SR1_AF)
        {
            return address_phase
                ? USBUSKIT_I2C_STATUS_ADDRESS_NACK
                : USBUSKIT_I2C_STATUS_DATA_NACK;
        }

        /* Expected hardware event occurred. */
        if (sr1 & flag)
            return USBUSKIT_I2C_STATUS_SUCCESS;
    }

    return USBUSKIT_I2C_STATUS_TIMEOUT;
}

static uint8_t i2c_write(uint8_t address,
                         const uint8_t *data,
                         uint16_t length)
{
    uint8_t status;
    uint32_t timeout = I2C_POLL_TIMEOUT;
    volatile uint32_t dummy;

    /* Validate the slave address and data pointer. */
    if (address > 0x7F || (length > 0 && data == NULL))
        return USBUSKIT_I2C_STATUS_INVALID_REQUEST;

    /* Wait until the bus becomes available. */
    while (I2C1->SR2 & I2C_SR2_BUSY)
    {
        if (--timeout == 0)
            return USBUSKIT_I2C_STATUS_BUS_BUSY;
    }

    /* Generate START condition. */
    I2C1->CR1 |= I2C_CR1_START;

    status = i2c_wait_for_flag(I2C_SR1_SB, true);
    if (status != USBUSKIT_I2C_STATUS_SUCCESS)
        goto error;

    /* Transmit the 7-bit slave address with the WRITE bit. */
    I2C1->DR = (uint8_t)(address << 1);

    status = i2c_wait_for_flag(I2C_SR1_ADDR, true);
    if (status != USBUSKIT_I2C_STATUS_SUCCESS)
        goto error;

    /* Clear ADDR by reading SR1 followed by SR2. */
    dummy = I2C1->SR1;
    dummy = I2C1->SR2;
    (void)dummy;

    /* Transmit each data byte. */
    for (uint16_t i = 0; i < length; i++)
    {
        status = i2c_wait_for_flag(I2C_SR1_TXE, false);
        if (status != USBUSKIT_I2C_STATUS_SUCCESS)
            goto error;

        I2C1->DR = data[i];
    }

    /*
     * Wait until the final byte has completed transmission.
     * This also allows detection of a NACK on the final byte.
     */
    status = i2c_wait_for_flag(I2C_SR1_BTF, false);
    if (status != USBUSKIT_I2C_STATUS_SUCCESS)
        goto error;

    /* Generate STOP after successful transmission. */
    I2C1->CR1 |= I2C_CR1_STOP;

    return USBUSKIT_I2C_STATUS_SUCCESS;

error:
    /*
     * After arbitration loss, STM32 hardware releases the bus.
     * We must not issue STOP because another controller owns it.
     */
    if (status != USBUSKIT_I2C_STATUS_ARBITRATION_LOST)
        I2C1->CR1 |= I2C_CR1_STOP;

    /* Clear the hardware error flags. */
    I2C1->SR1 &= ~(I2C_SR1_AF |
                   I2C_SR1_BERR |
                   I2C_SR1_ARLO);

    return status;
}

/* --------------------------------------------------
 * SysTick
 * -------------------------------------------------- */

void SysTick_Handler(void)
{
    g_milliseconds++;
}

static void systick_init(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
}

/* --------------------------------------------------
 * USB interrupt
 * -------------------------------------------------- */

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    tud_int_handler(0);
}

struct usbuskit_i2c_message {
    uint16_t address;
    bool read;
    uint16_t length;
    const uint8_t *write_data;
};

static bool usbuskit_i2c_decode_request(
    const uint8_t *payload,
    uint16_t payload_len,
    struct usbuskit_i2c_message *msgs,
    uint8_t *num_msgs);

static bool usbuskit_i2c_validate_response_size(
    const struct usbuskit_i2c_message *msgs,
    uint8_t num_msgs);

static bool usbuskit_i2c_build_response(
    const struct usbuskit_header *request_header,
    uint8_t num_msgs,
    uint8_t status)
{
    uint8_t i;

    /* Do not overwrite a response awaiting transmission. */
    if (tx_pending)
        return false;

    if (num_msgs == 0 || num_msgs > USBUSKIT_I2C_MAX_MESSAGES)
        return false;

    /*
     * One I2C message:
     * 1 byte message count + 1 byte status.
     */
    struct usbuskit_header response_header = {
        .protocol_id = USBUSKIT_PROTO_I2C,
        .instance_id = request_header->instance_id,
        .opcode = USBUSKIT_I2C_OP_TRANSFER,
        .payload_length = 1U + num_msgs
    };

    /* Encode the common 5-byte USBusKit header. */
    usbuskit_encode_header(tx_buffer, &response_header);

    tx_buffer[USBUSKIT_HEADER_SIZE] = num_msgs;

    for (i = 0; i < num_msgs; i++) {
        /*
         * Report the first message's result.
         * Remaining messages were not executed.
         */
        tx_buffer[USBUSKIT_HEADER_SIZE + 1U + i] =
            (i == 0) ? status : USBUSKIT_I2C_STATUS_NOT_EXECUTED;
    }

    tx_length = USBUSKIT_HEADER_SIZE + response_header.payload_length;
    tx_pending = true;

    return true;
}

// static bool usbuskit_i2c_build_status_response(
//     const struct usbuskit_header *request_header,
//     uint8_t num_msgs,
//     uint8_t status)
// {
//     uint16_t payload_length = 1U + num_msgs;

//     /* Reject responses exceeding our configured limit. */
//     if (payload_length > USBUSKIT_MAX_RESPONSE_PAYLOAD)
//         return false;

//     /* Do not overwrite a response awaiting transmission. */
//     if (tx_pending)
//         return false;

//     /*
//      * Encode the response header.
//      * The protocol, instance and opcode match the original request.
//      */
//     struct usbuskit_header response_header = {
//         .protocol_id = USBUSKIT_PROTO_I2C,
//         .instance_id = request_header->instance_id,
//         .opcode = USBUSKIT_I2C_OP_TRANSFER,
//         .payload_length = payload_length
//     };

//     usbuskit_encode_header(tx_buffer, &response_header);

//     /* First payload byte contains the number of messages. */
//     tx_buffer[USBUSKIT_HEADER_SIZE] = num_msgs;

//     /* Populate the status for every message. */
//     for (uint8_t i = 0; i < num_msgs; i++)
//     {
//         tx_buffer[USBUSKIT_HEADER_SIZE + 1U + i] =
//             (i == 0) ? status : USBUSKIT_I2C_STATUS_NOT_EXECUTED;
//     }

//     tx_length = USBUSKIT_HEADER_SIZE + payload_length;
//     tx_pending = true;

//     return true;
// }

static bool usbuskit_i2c_validate_response_size(
    const struct usbuskit_i2c_message *msgs,
    uint8_t num_msgs)
{
    /* One byte for message count and one status byte per message. */
    uint32_t response_size = 1U + num_msgs;

    for (uint8_t i = 0; i < num_msgs; i++)
    {
        /* Only READ messages contribute response data. */
        if (msgs[i].read)
        {
            response_size += msgs[i].length;
        }

        /* Ensure the response fits within the protocol limit. */
        if (response_size > USBUSKIT_MAX_RESPONSE_PAYLOAD)
        {
            return false;
        }
    }

    return true;
}

static void usbuskit_i2c_process_request(
    const struct usbuskit_header *header,
    const uint8_t *payload,
    uint16_t payload_len)
{
    struct usbuskit_i2c_message msgs[USBUSKIT_I2C_MAX_MESSAGES];
    uint8_t num_msgs = 0;
    uint8_t status = USBUSKIT_I2C_STATUS_UNSUPPORTED;

    /* Initially support only I2C instance 0 and TRANSFER. */
    if (header->instance_id != 0 ||
        header->opcode != USBUSKIT_I2C_OP_TRANSFER)
        return;

    /* Decode the I2C messages from the USB request. */
    if (!usbuskit_i2c_decode_request(
            payload, payload_len, msgs, &num_msgs))
        return;

    /* Check that the response will fit our configured buffer. */
    if (!usbuskit_i2c_validate_response_size(msgs, num_msgs))
        return;

    /*
     * Initial implementation:
     * Only one I2C WRITE message is supported.
     */
    if (num_msgs != 1 || msgs[0].read || msgs[0].length < 1) {
        status = USBUSKIT_I2C_STATUS_UNSUPPORTED;
    } else {
        /* Execute the physical I2C transaction. */
        status = i2c_write(
            msgs[0].address,
            msgs[0].write_data,
            msgs[0].length
        );
    }

    /* Construct the USBusKit response containing the I2C result. */
    usbuskit_i2c_build_response(header, num_msgs, status);
}

static void usbuskit_transmit_task(void)
{
    /* Nothing to transmit. */
    if (!tx_pending)
        return;

    /* Ensure the USB vendor interface is mounted. */
    if (!tud_vendor_mounted())
        return;

    /*
     * Wait until TinyUSB has enough FIFO space
     * to accept the entire response.
     */
    if (tud_vendor_write_available() < tx_length)
        return;

    /* Queue the complete response for USB transmission. */
    uint32_t written = tud_vendor_write(tx_buffer, tx_length);

    /*
     * The entire response must be accepted.
     * A partial write requires separate recovery handling.
     */
    if (written != tx_length)
        return;

    /* Start transmission of any remaining buffered data. */
    tud_vendor_write_flush();

    /* The complete response has been handed to TinyUSB. */
    tx_pending = false;
    tx_length = 0;
}


static bool usbuskit_i2c_decode_request(
    const uint8_t *payload,
    uint16_t payload_len,
    struct usbuskit_i2c_message *msgs,
    uint8_t *num_msgs)
{
    uint16_t offset = 0;
    uint8_t count;
    uint8_t i;

    /* Validate pointers and ensure the message count exists. */
    if (!payload || !msgs || !num_msgs || payload_len < 1)
        return false;

    /* The first payload byte specifies the number of I²C messages. */
    count = payload[offset++];

    if (count == 0 || count > USBUSKIT_I2C_MAX_MESSAGES)
        return false;

    for (i = 0; i < count; i++)
    {
        uint16_t address_flags;
        uint16_t length;

        /* Each message contains 2 bytes of flags and 2 bytes of length. */
        if (payload_len - offset < USBUSKIT_I2C_MSG_HEADER_SIZE)
            return false;

        /* Decode the combined address and flags field. */
        address_flags = usbuskit_get_u16_le(&payload[offset]);
        offset += 2;

        /* Decode the requested transfer length. */
        length = usbuskit_get_u16_le(&payload[offset]);
        offset += 2;

        /* Initially support only 7-bit I²C addresses and the READ flag. */
        if (address_flags & ~(0x007FU | USBUSKIT_I2C_FLAG_READ))
            return false;

        msgs[i].address = address_flags & 0x007F;
        msgs[i].read = (address_flags & USBUSKIT_I2C_FLAG_READ) != 0;
        msgs[i].length = length;
        msgs[i].write_data = NULL;

        if (!msgs[i].read)
        {
            /* Ensure all write-data bytes are present in the request. */
            if (length > payload_len - offset)
                return false;

            /* Reference the write data directly inside the receive buffer. */
            msgs[i].write_data = &payload[offset];
            offset += length;
        }
    }

    /* Reject requests containing unexpected trailing bytes. */
    if (offset != payload_len)
        return false;

    *num_msgs = count;
    return true;
}

static void usbuskit_process_request(
    const uint8_t *request,
    uint16_t request_len)
{
    struct usbuskit_header header;

    /* Ensure the request contains the complete common header. */
    if (!request || request_len < USBUSKIT_HEADER_SIZE)
        return;

    /* Decode the USBusKit common header. */
    usbuskit_decode_header(request, &header);

    /* Validate the advertised payload length. */
    if (header.payload_length > USBUSKIT_MAX_REQUEST_PAYLOAD)
        return;

    /* Ensure the received packet matches its declared length. */
    if (request_len != USBUSKIT_HEADER_SIZE + header.payload_length)
        return;

    /* Identify the requested peripheral protocol. */
    switch (header.protocol_id)
    {
        case USBUSKIT_PROTO_I2C:

            /* Forward the request to the MCU I²C handler. */
            usbuskit_i2c_process_request(
                &header,
                request + USBUSKIT_HEADER_SIZE,
                header.payload_length
            );

            break;

        /*
         * Additional protocols will be registered here.
         *
         * case USBUSKIT_PROTO_SPI:
         * case USBUSKIT_PROTO_GPIO:
         * case USBUSKIT_PROTO_UART:
         * case USBUSKIT_PROTO_CAN:
         */

        default:
            /* Unsupported protocol. */
            break;
    }
}

static void usbuskit_receive_task(void)
{
    struct usbuskit_header header;

    /* Process all bytes currently available from TinyUSB. */
    while (tud_vendor_available() > 0)
    {
        /* Prevent writing beyond our receive buffer. */
        uint32_t remaining = sizeof(rx_buffer) - rx_byte_count;

        if (remaining == 0)
        {
            rx_byte_count = 0;
            return;
        }

        /* Append received bytes to the existing request. */
        uint32_t read_count = tud_vendor_read(
            &rx_buffer[rx_byte_count],
            remaining
        );

        if (read_count == 0)
            return;

        rx_byte_count += read_count;

        /* Wait until the complete common header is available. */
        if (rx_byte_count < USBUSKIT_HEADER_SIZE)
            continue;

        /* Decode the header to determine the request length. */
        usbuskit_decode_header(rx_buffer, &header);

        /* Reject requests exceeding our configured payload limit. */
        if (header.payload_length > USBUSKIT_MAX_REQUEST_PAYLOAD)
        {
            rx_byte_count = 0;
            return;
        }

        uint16_t expected_length =
            USBUSKIT_HEADER_SIZE + header.payload_length;

        /* Wait for the remaining request bytes. */
        if (rx_byte_count < expected_length)
            continue;

        /* A complete USBusKit request has been assembled. */
        if (rx_byte_count == expected_length)
        {
            /*
             * Next step: Dispatch the request to the appropriate
             * protocol handler (I2C, SPI, etc.).
             */
            usbuskit_process_request(rx_buffer, rx_byte_count);

            rx_byte_count = 0;
        }
    }
}

static void vendor_task(void)
{
    if (!tud_vendor_mounted())
        return;

    /* Send any response waiting from a previous iteration. */
    usbuskit_transmit_task();

    /*
     * Accept a new request only when no response
     * is waiting to be queued.
     */
    if (!tx_pending)
        usbuskit_receive_task();

    /* Transmit a response generated by the receive task. */
    usbuskit_transmit_task();
}


/* --------------------------------------------------
 * Main
 * -------------------------------------------------- */

int main(void)
{
    clock_init();
    systick_init();

    i2c_init();

    /* Enable GPIOC */
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;

    /* Configure PC13 as output */
    GPIOC->CRH &= ~(0xFU << 20);
    GPIOC->CRH |= (0x2U << 20);

    /* Enable USB peripheral clock */
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;

    /* Initialize TinyUSB */
    tusb_init();

    uint32_t last_blink = g_milliseconds;
    bool led_on = false;

    while (1)
    {
        /* Process USB events */
        tud_task();

        /* CDC echo */
        vendor_task();

        /* Non-blocking LED blink */
        uint32_t now = g_milliseconds;
        uint32_t interval = led_on ? 200U : 500U;

        if ((uint32_t)(now - last_blink) >= interval)
        {
            last_blink = now;
            led_on = !led_on;

            if (led_on)
            {
                GPIOC->BRR = GPIO_BRR_BR13;
            }
            else
            {
                GPIOC->BSRR = GPIO_BSRR_BS13;
            }
        }
    }
}