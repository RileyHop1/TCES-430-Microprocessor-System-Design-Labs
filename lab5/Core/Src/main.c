/*
 * Lab 5 - Interrupt-Driven Digital Countdown Safe Lock
 * STM32F746G-DISCO | Bare-metal C / CMSIS (no HAL)
 *
 * Keypad: columns PI0-PI3 (EXTI0-3); rows PB8, PB9, PB14, PB15
 * LCD:    D4=PA0, D5=PF10, D6=PF9, D7=PF8, RS=PF6, E=PF7
 * Buzzer: active, HIGH=ON, PB0 (verify rated voltage/current)
 * Timer:  TIM2 32-bit free-running millisecond counter (poll CNT)
 */

#include "main.h"


// ============================================================
// KEYPAD PINS - COLUMNS: GPIOI PI0-PI3 (EXTI0-EXTI3)
// ============================================================
#define M_COLUMN_PIN1  0U
#define M_COLUMN_PIN2  1U
#define M_COLUMN_PIN3  2U
#define M_COLUMN_PIN4  3U
// KEYPAD PINS - ROWS: GPIOB, avoiding the LCD's PF6-PF10
// PB8=D15, PB9=D14, PB14=D12, PB15=D11 (Arduino header)
#define M_ROW_PIN1     8U
#define M_ROW_PIN2     9U
#define M_ROW_PIN3    14U
#define M_ROW_PIN4    15U
#define COLUMN_MASK  0x000FU
#define ROW_MASK     0xC300U  // PB8, PB9, PB14, PB15
#define ROW_MODE_MASK ( \
    (3U << (2U * M_ROW_PIN1)) | \
    (3U << (2U * M_ROW_PIN2)) | \
    (3U << (2U * M_ROW_PIN3)) | \
    (3U << (2U * M_ROW_PIN4)) )
#define ROW_OUTPUT_MODE ( \
    (1U << (2U * M_ROW_PIN1)) | \
    (1U << (2U * M_ROW_PIN2)) | \
    (1U << (2U * M_ROW_PIN3)) | \
    (1U << (2U * M_ROW_PIN4)) )

// ============================================================
// LCD PINS - ORIGINAL LAB WIRING (UNCHANGED)
// ============================================================
// LCD D4 -> PA0; D5 -> PF10; D6 -> PF9; D7 -> PF8
// LCD RS -> PF6; E -> PF7; R/W -> GND
#define DB4  0U
#define DB5 10U
#define DB6  9U
#define DB7  8U
#define RS   6U
#define E    7U
// HD44780-style 4-bit LCD commands from the previous lab
#define LCD_INIT_8BIT     0x03U
#define LCD_INIT_4BIT     0x02U
#define LCD_FUNCTION_SET  0x28U
#define LCD_DISPLAY_OFF   0x08U
#define LCD_DISPLAY_ON    0x0CU
#define LCD_CLEAR         0x01U
#define LCD_ENTRY_MODE    0x06U
#define LCD_LINE1         0x80U
#define LCD_LINE2         0xC0U
// Preserve the previous lab's software delay counts.
// These are approximate busy-loop delays, NOT calibrated microseconds.
#define LCD_PULSE_DELAY   500U
#define LCD_STARTUP_DELAY 4000000U
#define LCD_INIT_DELAY    500000U
#define LCD_CMD_DELAY     5000U
#define LCD_CLEAR_DELAY   200000U
#define LCD_BYTE_DELAY    5000U

// ============================================================
// APPLICATION CONSTANTS
// ============================================================
#define PASS_CODE             "1234"
#define PIN_LENGTH            4U
#define LOCKOUT_DURATION_MS   30000U
#define KEY_BEEP_MS           100U
#define SUCCESS_BEEP_MS       2000U
#define SUCCESS_MESSAGE_MS    2000U
#define ERROR_MESSAGE_MS      1000U
#define BUZZER_PIN 6U  // PG6, Arduino D2

// ============================================================
// APPLICATION STATE AND ISR-MAIN COMMUNICATION
// ============================================================
volatile uint8_t key_ready = 0U;
volatile uint8_t keypad_locked = 0U;
volatile uint8_t last_row = 10U;
volatile uint8_t last_col = 10U;
volatile char last_key = '\0';
static const char key_table[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};
// Four digits + null terminator.
char entered_pin[5] = {0};
uint8_t pin_index = 0U;

// Buzzer: timed keypress/success tone or alternating lockout tone.
uint32_t buzzer_start_ms = 0U;
uint32_t buzzer_duration_ms = 0U;
uint8_t buzzer_active = 0U;

// Separate security lockout state (not the release-debounce lock).
uint32_t lockout_start_ms = 0U;
uint8_t lockout_active = 0U;
uint8_t fail_count = 0U;
uint32_t last_displayed_second = UINT32_MAX;

// Show results briefly before allowing another PIN.
uint8_t result_message_active = 0U;
uint32_t result_message_start_ms = 0U;
uint32_t result_message_duration_ms = 0U;

// Re-enable EXTI only after release and after any display/lockout state.
uint8_t keypad_rearm_pending = 0U;

// Shared polled timebase and debounce bookkeeping.
uint32_t now_ms = 0U;
uint8_t release_started = 0U;
uint32_t release_time = 0U;

// ============================================================
// LCD DRIVER - REUSED FROM YOUR PREVIOUS LAB
// ============================================================
void simpleDelay(uint32_t count) {
    while (count--) {
        __NOP();
    }
}

void setup_lcd_data_bus(void) {
    // RCC clocks for GPIOA and GPIOF are enabled in main().
    // Make PA0 and PF6-PF10 GPIO outputs.
    GPIOA->MODER &= ~(3U << (DB4 * 2U));
    GPIOF->MODER &= ~(3U << (DB5 * 2U));
    GPIOF->MODER &= ~(3U << (DB6 * 2U));
    GPIOF->MODER &= ~(3U << (DB7 * 2U));
    GPIOF->MODER &= ~(3U << (RS  * 2U));
    GPIOF->MODER &= ~(3U << (E   * 2U));
    GPIOA->MODER |= (1U << (DB4 * 2U));
    GPIOF->MODER |= (1U << (DB5 * 2U));
    GPIOF->MODER |= (1U << (DB6 * 2U));
    GPIOF->MODER |= (1U << (DB7 * 2U));
    GPIOF->MODER |= (1U << (RS  * 2U));
    GPIOF->MODER |= (1U << (E   * 2U));
    // Push-pull outputs, matching your previous LCD wiring.
    GPIOA->OTYPER &= ~(1U << DB4);
    GPIOF->OTYPER &= ~(1U << DB5);
    GPIOF->OTYPER &= ~(1U << DB6);
    GPIOF->OTYPER &= ~(1U << DB7);
    GPIOF->OTYPER &= ~(1U << RS);
    GPIOF->OTYPER &= ~(1U << E);
}

void send_lcd_nibble(uint8_t nibble, uint8_t rs) {
    // Clear all four data pins and RS.
    GPIOA->BSRR = (1U << (DB4 + 16U));
    GPIOF->BSRR =
        (1U << (DB5 + 16U)) |
        (1U << (DB6 + 16U)) |
        (1U << (DB7 + 16U)) |
        (1U << (RS  + 16U));
    // Place the nibble on D4-D7; RS selects command/data register.
    GPIOA->BSRR = ((nibble & 1U) << DB4);
    GPIOF->BSRR =
        (((nibble >> 1U) & 1U) << DB5) |
        (((nibble >> 2U) & 1U) << DB6) |
        (((nibble >> 3U) & 1U) << DB7) |
        ((rs & 1U) << RS);
    // HD44780 accepts the nibble on E's falling edge.
    simpleDelay(LCD_PULSE_DELAY);
    GPIOF->BSRR = (1U << E);
    simpleDelay(LCD_PULSE_DELAY);
    GPIOF->BSRR = (1U << (E + 16U));
    simpleDelay(LCD_PULSE_DELAY);
}

void setup_lcd(void) {
    GPIOF->BSRR = (1U << (E + 16U));
    simpleDelay(LCD_STARTUP_DELAY);
    // Synchronize LCD into known 8-bit startup state.
    send_lcd_nibble(LCD_INIT_8BIT, 0U);
    simpleDelay(LCD_INIT_DELAY);
    send_lcd_nibble(LCD_INIT_8BIT, 0U);
    simpleDelay(LCD_INIT_DELAY);
    send_lcd_nibble(LCD_INIT_8BIT, 0U);
    simpleDelay(LCD_INIT_DELAY);
    // Switch to four-bit transactions.
    send_lcd_nibble(LCD_INIT_4BIT, 0U);
    simpleDelay(LCD_CMD_DELAY);
    // Preserve your previous lab's sequence.
    send_lcd_nibble(LCD_FUNCTION_SET >> 4U, 0U);
    send_lcd_nibble(LCD_FUNCTION_SET & 0x0FU, 0U);
    simpleDelay(LCD_CMD_DELAY);
    send_lcd_nibble(LCD_DISPLAY_OFF >> 4U, 0U);
    send_lcd_nibble(LCD_DISPLAY_OFF & 0x0FU, 0U);
    simpleDelay(LCD_CMD_DELAY);
    send_lcd_nibble(LCD_CLEAR >> 4U, 0U);
    send_lcd_nibble(LCD_CLEAR & 0x0FU, 0U);
    simpleDelay(LCD_CLEAR_DELAY);
    send_lcd_nibble(LCD_ENTRY_MODE >> 4U, 0U);
    send_lcd_nibble(LCD_ENTRY_MODE & 0x0FU, 0U);
    simpleDelay(LCD_CMD_DELAY);
    send_lcd_nibble(LCD_DISPLAY_ON >> 4U, 0U);
    send_lcd_nibble(LCD_DISPLAY_ON & 0x0FU, 0U);
    simpleDelay(LCD_CMD_DELAY);
}

void lcd_write(uint8_t value, uint8_t rs) {
    send_lcd_nibble(value >> 4U, rs);
    send_lcd_nibble(value & 0x0FU, rs);
    simpleDelay(LCD_BYTE_DELAY);
}

// Small adapter so the existing LCD driver can display a startup prompt.
void lcd_print(const char *message) {
    while (*message != '\0') {
        lcd_write((uint8_t)*message++, 1U);
    }
}

// ============================================================
// ACTIVE BUZZER - GPIOB PB0 (HIGH = ON)
// ============================================================


void setup_buzzer(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->BSRR = (1U << (BUZZER_PIN + 16U));

    GPIOG->MODER &= ~(3U << (BUZZER_PIN * 2U));
    GPIOG->MODER |=  (1U << (BUZZER_PIN * 2U));

    GPIOG->OTYPER &= ~(1U << BUZZER_PIN);
    GPIOG->PUPDR &= ~(3U << (BUZZER_PIN * 2U));
}

void buzzer_on(void) {
    GPIOG->BSRR = (1U << BUZZER_PIN);
}

void buzzer_off(void) {
    GPIOG->BSRR = (1U << (BUZZER_PIN + 16U));
}


// ============================================================
// KEYPAD GPIO MODES: DEFAULT = ROWS LOW / COLUMNS PULL-UP
// ============================================================
void default_matrix_gpios(void) {
    // GPIOB row output latches LOW, GPIOI column latches LOW.
    GPIOB->BSRR = ROW_MASK << 16U;
    GPIOI->BSRR = COLUMN_MASK << 16U;
    GPIOB->OTYPER &= ~ROW_MASK;
    GPIOB->MODER =
        (GPIOB->MODER & ~ROW_MODE_MASK) | ROW_OUTPUT_MODE;
    GPIOB->PUPDR &= ~ROW_MODE_MASK;
    // Restore columns to pull-up inputs after rows become LOW outputs.
    GPIOI->OTYPER &= ~COLUMN_MASK;
    GPIOI->PUPDR =
        (GPIOI->PUPDR & ~0xFFU) | 0x55U;
    GPIOI->MODER &= ~0xFFU;
}

void alternate_matrix_gpios(uint8_t col) {
    // Protect against an invalid column index.
    if (col > 3U) {
        return;
    }
    // Enable row pull-ups BEFORE switching them into input mode.
    GPIOB->PUPDR =
        (GPIOB->PUPDR & ~ROW_MODE_MASK) | ROW_OUTPUT_MODE;
    GPIOB->MODER &= ~ROW_MODE_MASK;
    // Only the EXTI-triggering column is driven LOW.
    GPIOI->BSRR = (1U << (col + 16U));
    GPIOI->OTYPER &= ~(1U << col);
    GPIOI->PUPDR &= ~(3U << (2U * col));
    GPIOI->MODER =
        (GPIOI->MODER & ~(3U << (2U * col))) |
        (1U << (2U * col));
}

void configure_matrix_interrupts(void) {
    // EXTI0-3 are sourced from port I (port selector 8).
    SYSCFG->EXTICR[0] = 0x8888U;
    EXTI->IMR &= ~COLUMN_MASK;
    EXTI->RTSR &= ~COLUMN_MASK;
    EXTI->FTSR |= COLUMN_MASK;
    EXTI->PR = COLUMN_MASK;
    NVIC_SetPriority(EXTI0_IRQn, 10U);
    NVIC_SetPriority(EXTI1_IRQn, 10U);
    NVIC_SetPriority(EXTI2_IRQn, 10U);
    NVIC_SetPriority(EXTI3_IRQn, 10U);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);
    NVIC_ClearPendingIRQ(EXTI1_IRQn);
    NVIC_ClearPendingIRQ(EXTI2_IRQn);
    NVIC_ClearPendingIRQ(EXTI3_IRQn);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
    NVIC_EnableIRQ(EXTI2_IRQn);
    NVIC_EnableIRQ(EXTI3_IRQn);
    EXTI->IMR |= COLUMN_MASK;
    __enable_irq();
}

// ============================================================
// POLLED TIM2: FREE-RUNNING 32-BIT MILLISECOND COUNTER
// ============================================================
void configure_timer(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1 &= ~TIM_CR1_CEN;

    // Assumes TIM2's input clock is 16 MHz.
    // 16,000,000 / (15999 + 1) = 1000 counter ticks per second.
    TIM2->PSC = 15999U;
    TIM2->ARR = 0xFFFFFFFFU;
    TIM2->EGR = TIM_EGR_UG;  // Load the new prescaler.
    TIM2->CNT = 0U;
    TIM2->SR = 0U;
    TIM2->CR1 |= TIM_CR1_CEN;
}

// ============================================================
// KEYPAD ISR SCAN + SINGLE EVENT PUBLICATION
// ============================================================
void keypad_interrupt_handler(uint8_t col) {
    if (keypad_locked) {
        return;
    }
    keypad_locked = 1U;
    EXTI->IMR &= ~COLUMN_MASK;
    uint8_t row = 10U;  // Invalid row until a LOW input is found.
    alternate_matrix_gpios(col);
    // Short settle delay (not the 20 ms mechanical debounce interval).
    for (volatile uint32_t i = 0U; i < 300U; ++i) {
        __NOP();
    }
    uint32_t row_states = GPIOB->IDR;
    if ((row_states & (1U << M_ROW_PIN1)) == 0U) {
        row = 0U;
    } else if ((row_states & (1U << M_ROW_PIN2)) == 0U) {
        row = 1U;
    } else if ((row_states & (1U << M_ROW_PIN3)) == 0U) {
        row = 2U;
    } else if ((row_states & (1U << M_ROW_PIN4)) == 0U) {
        row = 3U;
    }
    default_matrix_gpios();
    if (row == 10U) {
        return;  // Main will unlock after a stable keypad release.
    }
    last_row = row;
    last_col = col;
    last_key = key_table[row][col];
    key_ready = 1U;
}

void EXTI0_IRQHandler(void) {
    if (EXTI->PR & (1U << M_COLUMN_PIN1)) {
        keypad_interrupt_handler(M_COLUMN_PIN1);
        EXTI->PR = (1U << M_COLUMN_PIN1);
    }
}

void EXTI1_IRQHandler(void) {
    if (EXTI->PR & (1U << M_COLUMN_PIN2)) {
        keypad_interrupt_handler(M_COLUMN_PIN2);
        EXTI->PR = (1U << M_COLUMN_PIN2);
    }
}

void EXTI2_IRQHandler(void) {
    if (EXTI->PR & (1U << M_COLUMN_PIN3)) {
        keypad_interrupt_handler(M_COLUMN_PIN3);
        EXTI->PR = (1U << M_COLUMN_PIN3);
    }
}

void EXTI3_IRQHandler(void) {
    if (EXTI->PR & (1U << M_COLUMN_PIN4)) {
        keypad_interrupt_handler(M_COLUMN_PIN4);
        EXTI->PR = (1U << M_COLUMN_PIN4);
    }
}

// ============================================================
// LCD APPLICATION HELPERS
// ============================================================
void lcd_clear_screen(void) {
    lcd_write(LCD_CLEAR, 0U);
    simpleDelay(LCD_CLEAR_DELAY);
}

void lcd_show_pin_prompt(void) {
    lcd_clear_screen();
    lcd_write(LCD_LINE1, 0U);
    lcd_print("Enter PIN:");
    lcd_write(LCD_LINE2, 0U);
}

void lcd_show_result(const char *message) {
    // Overwrite any masked digits or older result text on line two.
    lcd_write(LCD_LINE2, 0U);
    lcd_print("                ");
    lcd_write(LCD_LINE2, 0U);
    lcd_print(message);
}

// Caller supplies >= 11 bytes for any uint32_t value.
void number_to_string(uint32_t number, char *buffer) {
    uint32_t temp = number;
    uint8_t digits = 0U;

    do {
        ++digits;
        temp /= 10U;
    } while (temp != 0U);

    buffer[digits] = '\0';
    for (uint8_t i = digits; i > 0U; --i) {
        uint8_t digit = (uint8_t)(number % 10U);
        buffer[i - 1U] = (char)('0' + digit);
        number /= 10U;
    }
}

void lcd_show_lockout_time(uint32_t remaining_seconds) {
    char time_string[12];
    number_to_string(remaining_seconds, time_string);

    lcd_write(LCD_LINE2, 0U);
    lcd_print("Time: ");
    lcd_print(time_string);
    lcd_print("  ");  // Clear old digit when changing 10 -> 9.
}

// ============================================================
// BUZZER TIMING AND SECURITY STATE
// ============================================================
void start_normal_beep(uint32_t duration_ms) {
    buzzer_on();
    buzzer_start_ms = TIM2->CNT;
    buzzer_duration_ms = duration_ms;
    buzzer_active = 1U;
}

void show_result_for(const char *message, uint32_t duration_ms) {
    EXTI->IMR &= ~COLUMN_MASK;  // Ignore additional keys during result.
    result_message_active = 1U;
    result_message_start_ms = TIM2->CNT;
    result_message_duration_ms = duration_ms;
    lcd_show_result(message);
}

void start_security_lockout(void) {
    EXTI->IMR &= ~COLUMN_MASK;
    key_ready = 0U;
    keypad_rearm_pending = 0U;
    result_message_active = 0U;
    fail_count = 0U;

    lockout_active = 1U;
    lockout_start_ms = TIM2->CNT;
    last_displayed_second = UINT32_MAX;

    buzzer_on();
    buzzer_active = 1U;

    lcd_clear_screen();
    lcd_write(LCD_LINE1, 0U);
    lcd_print("LOCKED!");
    lcd_show_lockout_time(30U);
    last_displayed_second = 30U;
}

// ============================================================
// PIN INPUT AND AUTHENTICATION
// ============================================================
void handle_key(char key) {
    // The lab calls for brief feedback on every physical keypress.
    start_normal_beep(KEY_BEEP_MS);

    // Non-numeric keys are ignored by the PIN-entry mechanism.
    if (key < '0' || key > '9' || pin_index >= PIN_LENGTH) {
        return;
    }

    entered_pin[pin_index] = key;
    ++pin_index;
    entered_pin[pin_index] = '\0';
    lcd_write('*', 1U);

    if (pin_index == PIN_LENGTH) {
        if (strcmp(entered_pin, PASS_CODE) == 0) {
            fail_count = 0U;
            start_normal_beep(SUCCESS_BEEP_MS); // Overrides 100 ms beep.
            show_result_for("Access Granted", SUCCESS_MESSAGE_MS);
        } else {
            ++fail_count;
            if (fail_count >= 3U) {
                start_security_lockout();
            } else {
                show_result_for("Incorrect PIN", ERROR_MESSAGE_MS);
            }
        }

        // Reset PIN buffer while retaining the result/lockout display.
        memset(entered_pin, 0, sizeof(entered_pin));
        pin_index = 0U;
    }
}

// ============================================================
// MAIN-LOOP STATE UPDATES
// ============================================================
void update_release_debounce(void) {
    if (!keypad_locked) {
        return;
    }

    if ((GPIOI->IDR & COLUMN_MASK) != COLUMN_MASK) {
        release_started = 0U;
        return;
    }

    if (!release_started) {
        release_started = 1U;
        release_time = now_ms;
    } else if ((uint32_t)(now_ms - release_time) >= 20U) {
        keypad_locked = 0U;
        release_started = 0U;
        keypad_rearm_pending = 1U;
    }
}

void update_normal_buzzer(void) {
    if (!lockout_active && buzzer_active &&
        (uint32_t)(now_ms - buzzer_start_ms) >= buzzer_duration_ms) {
        buzzer_off();
        buzzer_active = 0U;
    }
}

void update_result_message(void) {
    if (result_message_active &&
        (uint32_t)(now_ms - result_message_start_ms) >=
            result_message_duration_ms) {
        result_message_active = 0U;
        lcd_show_pin_prompt();
        keypad_rearm_pending = 1U;
    }
}

void update_security_lockout(void) {
    if (!lockout_active) {
        return;
    }

    uint32_t elapsed_ms = (uint32_t)(now_ms - lockout_start_ms);
    if (elapsed_ms >= LOCKOUT_DURATION_MS) {
        buzzer_off();
        buzzer_active = 0U;
        lockout_active = 0U;
        lcd_show_pin_prompt();
        keypad_rearm_pending = 1U;
        return;
    }

    // Alternating seconds: 1 s ON, 1 s OFF, repeated.
    uint8_t should_sound = ((elapsed_ms / 1000U) % 2U) == 0U;
    if (should_sound != buzzer_active) {
        if (should_sound) {
            buzzer_on();
        } else {
            buzzer_off();
        }
        buzzer_active = should_sound;
    }

    // Round up so 30 appears initially and 1 remains until expiry.
    uint32_t seconds_remaining =
        (LOCKOUT_DURATION_MS - elapsed_ms + 999U) / 1000U;

    if (seconds_remaining != last_displayed_second) {
        lcd_show_lockout_time(seconds_remaining);
        last_displayed_second = seconds_remaining;
    }
}

void update_keypad_rearm(void) {
    if (!keypad_rearm_pending || keypad_locked ||
        lockout_active || result_message_active) {
        return;
    }

    // Prevent a held key or an old pending edge from retriggering.
    if ((GPIOI->IDR & COLUMN_MASK) == COLUMN_MASK) {
        EXTI->PR = COLUMN_MASK;
        EXTI->IMR |= COLUMN_MASK;
        keypad_rearm_pending = 0U;
    }
}

// ============================================================
// MAIN: INITIALIZE HARDWARE, POLL TIMER AND UPDATE STATES
// ============================================================
int main(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOFEN |
                    RCC_AHB1ENR_GPIOIEN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Initialize the LCD before starting the timebase.
    setup_lcd_data_bus();
    setup_lcd();
    lcd_show_pin_prompt();

    default_matrix_gpios();
    setup_buzzer();
    configure_timer();
    configure_matrix_interrupts();

    while (1) {
        if (key_ready) {
            char key = last_key;
            key_ready = 0U;
            if (!lockout_active && !result_message_active) {
                handle_key(key);
            }
        }

        // TIM2's 32-bit counter increments once per millisecond.
        now_ms = TIM2->CNT;


        update_release_debounce();
        update_normal_buzzer();
        update_result_message();
        update_security_lockout();
        update_keypad_rearm();
    }
}
