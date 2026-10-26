#include <stdint.h>
#include "stm32f7xx.h"

#define NUMBER_1 10
#define NUMBER_2 22

#define DISPLAY_DELAY 3000U

#define SEG_A (1U << 0)
#define SEG_B (1U << 1)
#define SEG_C (1U << 2)
#define SEG_D (1U << 3)
#define SEG_E (1U << 4)
#define SEG_F (1U << 5)
#define SEG_G (1U << 6)


/* ==================== TYPES & PIN MAPPING ==================== */

typedef struct {
    GPIO_TypeDef *port;
    uint8_t pin;
} SegmentPin;

typedef enum {
    OP_NONE,
    OP_ADD,
    OP_SUB
} Operation;

typedef enum {
	SW_ADD,
	SW_SUB,
	SW_MEM,
	SW_EQ
} buttons;

static const SegmentPin segments[7] = {
    {GPIOG, 6}, // A
    {GPIOB, 4}, // B
    {GPIOG, 7}, // C
    {GPIOI, 0}, // D
    {GPIOH, 6}, // E
    {GPIOI, 3}, // F
    {GPIOI, 2}  // G
};

static const SegmentPin digit_selects[4] = {
    {GPIOA, 8},  // D10
    {GPIOB, 15}, // D11
    {GPIOB, 14}, // D12
    {GPIOI, 1}   // D13
};


/* ==================== GPIO INITIALIZATION ==================== */

/* Configures a GPIO pin as a general-purpose push-pull output. */
static void gpio_output(GPIO_TypeDef *gpio, uint32_t pin) {
    uint32_t shift = pin * 2U;

    gpio->OTYPER &= ~(1U << pin);
    gpio->PUPDR &= ~(3U << shift);

    gpio->MODER &= ~(3U << shift);
    gpio->MODER |=  (1U << shift);
}

/* Configures a GPIO pin as an input with an internal pull-up. */
static void gpio_input_pullup(GPIO_TypeDef *gpio, uint32_t pin) {
    uint32_t shift = pin * 2U;

    gpio->MODER &= ~(3U << shift);

    gpio->PUPDR &= ~(3U << shift);
    gpio->PUPDR |=  (1U << shift);
}

/* Enables GPIO clocks and initializes display and switch pins. */
static void gpio_init(void) {
    RCC->AHB1ENR |= (
        RCC_AHB1ENR_GPIOAEN |
        RCC_AHB1ENR_GPIOBEN |
        RCC_AHB1ENR_GPIOFEN |
        RCC_AHB1ENR_GPIOGEN |
        RCC_AHB1ENR_GPIOHEN |
        RCC_AHB1ENR_GPIOIEN
    );

    (void)RCC->AHB1ENR;

    // Initialize segments OFF and digit selects disabled.
    GPIOG->BSRR = (1U << 6) | (1U << 7);

    GPIOB->BSRR = (1U << 4) |
                  (1U << (9 + 16)) |
                  (1U << (14 + 16)) |
                  (1U << (15 + 16));

    GPIOH->BSRR = (1U << 6);

    GPIOI->BSRR = (1U << 0) |
                  (1U << 2) |
                  (1U << 3) |
                  (1U << (1 + 16));

    GPIOA->BSRR = (1U << (8 + 16));

    // Segment outputs
    gpio_output(GPIOG, 6);
    gpio_output(GPIOB, 4);
    gpio_output(GPIOG, 7);
    gpio_output(GPIOI, 0);
    gpio_output(GPIOH, 6);
    gpio_output(GPIOI, 3);
    gpio_output(GPIOI, 2);

    // Digit-select outputs
    gpio_output(GPIOA, 8);
    gpio_output(GPIOB, 15);
    gpio_output(GPIOB, 14);
    gpio_output(GPIOI, 1);
    gpio_output(GPIOB, 9); // D14, currently unused

    // Switch inputs
    gpio_input_pullup(GPIOA, 0);
    gpio_input_pullup(GPIOF, 10);
    gpio_input_pullup(GPIOF, 9);
    gpio_input_pullup(GPIOF, 8);
}


/* ==================== DISPLAY CONTROL ==================== */

/* Turns a segment on or off using the GPIO BSRR register. */
static void edit_display(const SegmentPin *segment, uint8_t clear) {
    if (clear) {
        segment->port->BSRR = (1UL << segment->pin);
    } else {
        segment->port->BSRR = (1UL << (segment->pin + 16U));
    }
}

/* Turns off all seven shared segments. */
static void clear_segments(void) {
    for (uint8_t i = 0; i < 7; i++) {
        edit_display(&segments[i], 1);
    }
}

/* Converts a decimal digit into the corresponding segment pattern. */
static void edit_digit(uint8_t digit) {
    uint8_t pattern;

    switch (digit) {
        case 0: pattern = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F; break;
        case 1: pattern = SEG_B | SEG_C; break;
        case 2: pattern = SEG_A | SEG_B | SEG_D | SEG_E | SEG_G; break;
        case 3: pattern = SEG_A | SEG_B | SEG_C | SEG_D | SEG_G; break;
        case 4: pattern = SEG_B | SEG_C | SEG_F | SEG_G; break;
        case 5: pattern = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G; break;
        case 6: pattern = SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G; break;
        case 7: pattern = SEG_A | SEG_B | SEG_C; break;
        case 8: pattern = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G; break;
        case 9: pattern = SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G; break;
        default: return;
    }

    for (uint8_t i = 0; i < 7; i++) {
        if (pattern & (1U << i)) {
            edit_display(&segments[i], 0);
        }
    }
}

/* Disables all four digit-select anodes. */
static void disable_digits(void) {
    for (uint8_t i = 0; i < 4; i++) {
        digit_selects[i].port->BSRR =
            (1UL << (digit_selects[i].pin + 16U));
    }
}

/* Enables the specified digit position (0-3). */
static void select_digit(uint8_t digit) {
    if (digit >= 4) return;

    digit_selects[digit].port->BSRR =
        (1UL << digit_selects[digit].pin);
}

/* Displays a minus sign on the currently selected segment pattern. */
static void display_sign(uint8_t negative) {
    if (negative) {
        edit_display(&segments[6], 0);
    }
}

/* Extracts decimal digits and refreshes the multiplexed display. */
static void display_number(int16_t number) {
    uint8_t digits[3];
    uint8_t negative = (number < 0);

    uint32_t value = negative
        ? (uint32_t)(-(int32_t)number)
        : (uint32_t)number;

    if (value > 999) {
        disable_digits();
        return;
    }

    for (int8_t i = 2; i >= 0; i--) {
        digits[i] = value % 10;
        value /= 10;
    }

    for (uint8_t i = 0; i < 4; i++) {
        disable_digits();
        clear_segments();

        if (i == 0) {
            display_sign(negative);
        } else {
            edit_digit(digits[i - 1]);
        }

        select_digit(i);

        // Temporary software delay for multiplexing.
        for (volatile uint32_t j = 0; j < DISPLAY_DELAY; j++) {
            __NOP();
        }
    }

    disable_digits();
}


/* ==================== SWITCH CONTROL ==================== */

/* Reads the four active-low switches into a button bitmask. */
static uint8_t read_buttons(void) {
    uint8_t buttons = 0;

    if ((GPIOA->IDR & (1UL << 0)) == 0)
        buttons |= SW_ADD;

    if ((GPIOF->IDR & (1UL << 10)) == 0)
        buttons |= SW_SUB;

    if ((GPIOF->IDR & (1UL << 9)) == 0)
        buttons |= SW_MEM;

    if ((GPIOF->IDR & (1UL << 8)) == 0)
        buttons |= SW_EQ;

    return buttons;
}


/* ==================== MAIN APPLICATION ==================== */

/* Handles calculator operations and continuously refreshes the display. */
int main(void) {
    gpio_init();

    Operation selected_op = OP_NONE;

    int16_t displayed_value = 0;
    int16_t last_result = 0;

    uint8_t previous_buttons = 0;

    while (1) {
        uint8_t buttons = read_buttons();

        // Detect newly pressed buttons.
        uint8_t pressed = buttons & (uint8_t)~previous_buttons;
        previous_buttons = buttons;

        switch (pressed) {
            case SW_ADD:
                selected_op = OP_ADD;
                break;

            case SW_SUB:
                selected_op = OP_SUB;
                break;

            case SW_EQ:
                if (selected_op == OP_ADD) {
                    last_result = NUMBER_1 + NUMBER_2;
                    displayed_value = last_result;
                } else if (selected_op == OP_SUB) {
                    last_result = NUMBER_1 - NUMBER_2;
                    displayed_value = last_result;
                }
                break;

            case SW_MEM:
                displayed_value = last_result;
                break;

            default:
                break;
        }

        display_number(displayed_value);
    }
}
