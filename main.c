#include <stddef.h>
#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define RCC_BASE            0x40023800UL
#define RCC_CR              REG32(RCC_BASE + 0x00UL)
#define RCC_CFGR            REG32(RCC_BASE + 0x08UL)
#define RCC_AHB1ENR         REG32(RCC_BASE + 0x30UL)
#define RCC_APB1ENR         REG32(RCC_BASE + 0x40UL)
#define FLASH_ACR           REG32(0x40023C00UL)

#define GPIOA_BASE          0x40020000UL
#define GPIOA_MODER         REG32(GPIOA_BASE + 0x00UL)
#define GPIOA_OTYPER        REG32(GPIOA_BASE + 0x04UL)
#define GPIOA_OSPEEDR       REG32(GPIOA_BASE + 0x08UL)
#define GPIOA_PUPDR         REG32(GPIOA_BASE + 0x0CUL)
#define GPIOA_AFRL          REG32(GPIOA_BASE + 0x20UL)

#define GPIOD_BASE          0x40020C00UL
#define GPIOD_MODER         REG32(GPIOD_BASE + 0x00UL)
#define GPIOD_OTYPER        REG32(GPIOD_BASE + 0x04UL)
#define GPIOD_BSRR          REG32(GPIOD_BASE + 0x18UL)

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SMCR;
    volatile uint32_t DIER;
    volatile uint32_t SR;
    volatile uint32_t EGR;
    volatile uint32_t CCMR1;
    volatile uint32_t CCMR2;
    volatile uint32_t CCER;
    volatile uint32_t CNT;
    volatile uint32_t PSC;
    volatile uint32_t ARR;
} TIM_Registers;

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_Registers;

#define TIM2                ((TIM_Registers *)0x40000000UL)
#define USART2              ((USART_Registers *)0x40004400UL)
#define NVIC_ISER           ((volatile uint32_t *)0xE000E100UL)
#define NVIC_IPR            ((volatile uint8_t *)0xE000E400UL)

#define HSI_HZ              16000000UL
#define UART_BAUD           9600UL
#define TIMER_TICK_HZ       1UL

#define RX_BUFFER_SIZE      64U
#define RX_BUFFER_MASK      (RX_BUFFER_SIZE - 1U)
#define TX_BUFFER_SIZE      128U
#define TX_BUFFER_MASK      (TX_BUFFER_SIZE - 1U)

#define USART_SR_PE         (1UL << 0)
#define USART_SR_FE         (1UL << 1)
#define USART_SR_NE         (1UL << 2)
#define USART_SR_ORE        (1UL << 3)
#define USART_SR_RXNE       (1UL << 5)
#define USART_SR_TXE        (1UL << 7)
#define USART_CR1_RE        (1UL << 2)
#define USART_CR1_TE        (1UL << 3)
#define USART_CR1_RXNEIE    (1UL << 5)
#define USART_CR1_TXEIE     (1UL << 7)
#define USART_CR1_UE        (1UL << 13)
#define USART_ERROR_MASK    (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE)

#define TIM_CR1_CEN         (1UL << 0)
#define TIM_CR1_ARPE        (1UL << 7)
#define TIM_DIER_UIE        (1UL << 0)
#define TIM_SR_UIF          (1UL << 0)
#define TIM_EGR_UG          (1UL << 0)

#define TIM2_IRQ_NUMBER     28U
#define USART2_IRQ_NUMBER   38U
#define LED_PIN             12U

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

void Reset_Handler(void);
void Default_Handler(void);
void TIM2_IRQHandler(void);
void USART2_IRQHandler(void);
int main(void);

/* Cortex-M vector numbers: TIM2 is IRQ 28 and USART2 is IRQ 38 on STM32F407. */
__asm__(
    ".syntax unified\n"
    ".thumb\n"
    ".section .isr_vector,\"a\",%progbits\n"
    ".align 9\n"
    ".word _estack\n"
    ".word Reset_Handler\n"
    ".rept 14\n"
    ".word Default_Handler\n"
    ".endr\n"
    ".rept 28\n"
    ".word Default_Handler\n"
    ".endr\n"
    ".word TIM2_IRQHandler\n"
    ".rept 9\n"
    ".word Default_Handler\n"
    ".endr\n"
    ".word USART2_IRQHandler\n"
    ".rept 43\n"
    ".word Default_Handler\n"
    ".endr\n"
    ".previous\n");

static volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;

static volatile uint8_t tx_buffer[TX_BUFFER_SIZE];
static volatile uint32_t tx_head;
static volatile uint32_t tx_tail;

static volatile uint32_t system_tick_count;
static volatile uint32_t rx_overflow_count;
static volatile uint32_t uart_error_count;
static volatile uint32_t tx_drop_count;
static volatile uint32_t led_blink_enabled;
static uint32_t led_is_on;

static inline void data_memory_barrier(void)
{
    __asm volatile ("dmb" ::: "memory");
}

static void system_clock_configure(void)
{
    /* Select the 16 MHz HSI clock and divide HCLK/APB clocks by one. */
    RCC_CR |= (1UL << 0);
    while ((RCC_CR & (1UL << 1)) == 0UL) {
    }

    RCC_CFGR = (RCC_CFGR & ~((3UL << 0) | (0xFUL << 4) |
                              (7UL << 10) | (7UL << 13))) | (0UL << 0);
    while (((RCC_CFGR >> 2) & 3UL) != 0UL) {
    }

    /* At 16 MHz the STM32F407 requires zero flash wait states. */
    FLASH_ACR &= ~0xFUL;
}

static void gpio_configure(void)
{
    /* Enable GPIOA for USART2 and GPIOD for the Discovery board's green LED. */
    RCC_AHB1ENR |= (1UL << 0) | (1UL << 3);
    (void)RCC_AHB1ENR;

    /* PA2/PA3 use alternate-function mode (10), AF7, for USART2 TX/RX. */
    GPIOA_MODER = (GPIOA_MODER & ~((3UL << 4) | (3UL << 6))) |
                  (2UL << 4) | (2UL << 6);
    GPIOA_OTYPER &= ~((1UL << 2) | (1UL << 3));
    GPIOA_OSPEEDR = (GPIOA_OSPEEDR & ~((3UL << 4) | (3UL << 6))) |
                    (2UL << 4) | (2UL << 6);
    GPIOA_PUPDR = (GPIOA_PUPDR & ~((3UL << 4) | (3UL << 6))) | (1UL << 6);
    GPIOA_AFRL = (GPIOA_AFRL & ~((0xFUL << 8) | (0xFUL << 12))) |
                 (7UL << 8) | (7UL << 12);

    /* PD12 is a push-pull output; drive it low before enabling output mode. */
    GPIOD_BSRR = 1UL << (LED_PIN + 16U);
    GPIOD_OTYPER &= ~(1UL << LED_PIN);
    GPIOD_MODER = (GPIOD_MODER & ~(3UL << (LED_PIN * 2U))) |
                  (1UL << (LED_PIN * 2U));
}

static void nvic_enable_irq(uint32_t irq_number, uint8_t priority)
{
    /* STM32F407 implements the upper four priority bits in each IPR byte. */
    NVIC_IPR[irq_number] = (uint8_t)(priority << 4);
    NVIC_ISER[irq_number / 32U] = 1UL << (irq_number % 32U);
}

static void uart_configure(void)
{
    RCC_APB1ENR |= 1UL << 17;
    (void)RCC_APB1ENR;

    USART2->CR1 = 0UL;
    USART2->CR2 = 0UL; /* STOP=00 selects one stop bit. */
    USART2->CR3 = 0UL;
    /* OVER8=0, so BRR is the rounded APB1 clock / baud divisor. */
    USART2->BRR = (HSI_HZ + (UART_BAUD / 2UL)) / UART_BAUD;
    USART2->CR1 = USART_CR1_RE | USART_CR1_TE | USART_CR1_RXNEIE | USART_CR1_UE;

    nvic_enable_irq(USART2_IRQ_NUMBER, 2U);
}

static void timer_configure(void)
{
    RCC_APB1ENR |= 1UL << 0;
    (void)RCC_APB1ENR;

    /* 16 MHz / 16000 / 1000 = one update interrupt per second. */
    TIM2->CR1 = TIM_CR1_ARPE;
    TIM2->PSC = (HSI_HZ / 1000UL) - 1UL;
    TIM2->ARR = (1000UL / TIMER_TICK_HZ) - 1UL;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0UL;
    TIM2->DIER = TIM_DIER_UIE;

    nvic_enable_irq(TIM2_IRQ_NUMBER, 3U);
    TIM2->CR1 |= TIM_CR1_CEN;
}

static int rx_buffer_pop(uint8_t *byte)
{
    uint32_t tail = rx_tail;
    uint32_t head = rx_head;

    if (tail == head) {
        return 0;
    }

    data_memory_barrier();
    *byte = rx_buffer[tail & RX_BUFFER_MASK];
    data_memory_barrier();
    rx_tail = tail + 1U;
    return 1;
}

static int uart_tx_enqueue(const char *data, size_t length)
{
    uint32_t head = tx_head;
    uint32_t tail = tx_tail;
    size_t index;

    if (length == 0U) {
        return 1;
    }
    if (length > (size_t)(TX_BUFFER_SIZE - (head - tail))) {
        tx_drop_count++;
        return 0;
    }

    for (index = 0U; index < length; index++) {
        tx_buffer[(head + (uint32_t)index) & TX_BUFFER_MASK] = (uint8_t)data[index];
    }

    /* Publish the entire message at once; the ISR cannot see a partial reply. */
    data_memory_barrier();
    tx_head = head + (uint32_t)length;
    USART2->CR1 |= USART_CR1_TXEIE;
    return 1;
}

static size_t append_text(char *destination, size_t offset, const char *text)
{
    while (*text != '\0') {
        destination[offset++] = *text++;
    }
    return offset;
}

static size_t append_u32(char *destination, size_t offset, uint32_t value)
{
    char digits[10];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    while (count != 0U) {
        destination[offset++] = digits[--count];
    }
    return offset;
}

static void uart_send_text(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    (void)uart_tx_enqueue(text, length);
}

static void uart_send_status(void)
{
    char response[96];
    size_t length = 0U;
    uint32_t ticks = system_tick_count;
    uint32_t rx_overflows = rx_overflow_count;
    uint32_t uart_errors = uart_error_count;
    uint32_t tx_drops = tx_drop_count;

    length = append_text(response, length, "tick=");
    length = append_u32(response, length, ticks);
    length = append_text(response, length, " blink=");
    length = append_text(response, length, led_blink_enabled ? "on" : "off");
    length = append_text(response, length, " rx_overflow=");
    length = append_u32(response, length, rx_overflows);
    length = append_text(response, length, " uart_error=");
    length = append_u32(response, length, uart_errors);
    length = append_text(response, length, " tx_drop=");
    length = append_u32(response, length, tx_drops);
    response[length++] = '\r';
    response[length++] = '\n';
    (void)uart_tx_enqueue(response, length);
}

static void process_command(uint8_t command)
{
    if (command == (uint8_t)'1') {
        led_blink_enabled = led_blink_enabled ? 0UL : 1UL;
        uart_send_text(led_blink_enabled ? "blink=on\r\n" : "blink=off\r\n");
    } else if (command == (uint8_t)'2') {
        uart_send_status();
    }
}

void TIM2_IRQHandler(void)
{
    if ((TIM2->SR & TIM_SR_UIF) != 0UL) {
        TIM2->SR = 0UL; /* Writing zero clears the update flag. */
        system_tick_count++;

        if (led_blink_enabled != 0UL) {
            if (led_is_on == 0UL) {
                GPIOD_BSRR = 1UL << LED_PIN;
                led_is_on = 1UL;
            } else {
                GPIOD_BSRR = 1UL << (LED_PIN + 16U);
                led_is_on = 0UL;
            }
        } else if (led_is_on != 0UL) {
            GPIOD_BSRR = 1UL << (LED_PIN + 16U);
            led_is_on = 0UL;
        }
    }
}

void USART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;

    /* Reading SR followed by DR clears STM32F4 receive error conditions. */
    if ((status & USART_ERROR_MASK) != 0UL) {
        volatile uint32_t discarded = USART2->DR;
        (void)discarded;
        uart_error_count++;
    } else if ((status & USART_SR_RXNE) != 0UL) {
        uint8_t byte = (uint8_t)USART2->DR;
        uint32_t head = rx_head;
        uint32_t tail = rx_tail;

        if ((head - tail) < RX_BUFFER_SIZE) {
            rx_buffer[head & RX_BUFFER_MASK] = byte;
            data_memory_barrier();
            rx_head = head + 1U;
        } else {
            rx_overflow_count++;
        }
    }

    if (((USART2->SR & USART_SR_TXE) != 0UL) &&
        ((USART2->CR1 & USART_CR1_TXEIE) != 0UL)) {
        uint32_t tail = tx_tail;
        uint32_t head = tx_head;

        if (tail != head) {
            USART2->DR = tx_buffer[tail & TX_BUFFER_MASK];
            data_memory_barrier();
            tx_tail = tail + 1U;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE;
        }
    }
}

void Default_Handler(void)
{
    for (;;) {
        __asm volatile ("wfi");
    }
}

void Reset_Handler(void)
{
    uint32_t *source = &_sidata;
    uint32_t *destination;

    /* Initialize .data from flash and clear .bss before entering C code. */
    for (destination = &_sdata; destination < &_edata; destination++) {
        *destination = *source++;
    }
    for (destination = &_sbss; destination < &_ebss; destination++) {
        *destination = 0UL;
    }

    system_clock_configure();
    gpio_configure();
    uart_configure();
    timer_configure();

    __asm volatile ("cpsie i" ::: "memory");
    (void)main();
    for (;;) {
        __asm volatile ("wfi");
    }
}

int main(void)
{
    uint8_t command;

    for (;;) {
        while (rx_buffer_pop(&command) != 0) {
            process_command(command);
        }
        __asm volatile ("wfi");
    }
}