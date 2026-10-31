.syntax unified
.cpu cortex-m7
.thumb
// Memory Layout Declarations
.equ RCC_BASE, 0x40023800
.equ GPIOI_BASE, 0x40022000
.equ RCC_AHB1ENR, 0x30
.equ GPIO_MODER, 0x00
.equ GPIO_ODR, 0x14
// Bit Field Definitions for Pin PI1 (Actual Onboard Green LED LD1)
.equ RCC_GPIOIEN, (1 << 8)
.equ GPIOI_PIN1_OUT, (1 << 2) // Sets Pin 1 to Output (01 in bits 3:2)
.equ GPIOI_PIN1_ON, (1 << 1) // Sets Pin 1 High
.section .data
.align 2
SRAM_NUMBER:
.word 0x00000080 // Data memory test target value (128)
.section .text
.global compare_LED
.align 2
.thumb_func
compare_LED:
// 1. Enable peripheral clock gate for Port I
LDR R0, =RCC_BASE
LDR R1, [R0, #RCC_AHB1ENR]
ORR R1, R1, #RCC_GPIOIEN
STR R1, [R0, #RCC_AHB1ENR]
// 2. Configure Pin PI1 (Internal LED) as Output
LDR R0, =GPIOI_BASE
LDR R1, [R0, #GPIO_MODER]
BIC R1, R1, #(3 << 2) // Clear bits 3:2
ORR R1, R1, #GPIOI_PIN1_OUT // Set to Output mode
STR R1, [R0, #GPIO_MODER]
// 3. Perform value retrievals from target spaces
LDR R2, =SRAM_NUMBER
LDR R2, [R2] // Extract SRAM value into R2
LDR R3, =FLASH_NUMBER
LDR R3, [R3] // Extract Flash value into R3
// 4. Condition evaluations
CMP R2, R3
BLE LED_OFF // Skip activation if SRAM value <= Flash value
LED_ON:
LDR R1, [R0, #GPIO_ODR]
ORR R1, R1, #GPIOI_PIN1_ON
STR R1, [R0, #GPIO_ODR]
BX LR // Return execution context to caller
LED_OFF:
LDR R1, [R0, #GPIO_ODR]
BIC R1, R1, #GPIOI_PIN1_ON
STR R1, [R0, #GPIO_ODR]
BX LR // Return execution context to caller
.align 2
FLASH_NUMBER:
.word 0x00000042 // Code memory threshold reference value (66)
