#include "stm32f103xb.h"
#include "tusb.h"

static volatile uint32_t g_milliseconds = 0;

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

static void vendor_task(void)
{
    if (!tud_vendor_mounted())
    {
        return;
    }

    uint8_t buffer[64];

    uint32_t count = tud_vendor_read(buffer, sizeof(buffer));

    if (count > 0)
    {
        tud_vendor_write(buffer, count);
        tud_vendor_write_flush();
    }
}

/* --------------------------------------------------
 * Main
 * -------------------------------------------------- */

int main(void)
{
    clock_init();
    systick_init();

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