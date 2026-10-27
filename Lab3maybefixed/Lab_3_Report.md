# Lab 3 — STM32F746G-DISCO

## Objective

Use GPIO outputs, TIM2 timing, two button interrupts, and a 1602A LCD in 4-bit mode to display the current LED sequence and blink count.

## Setup and wiring

Equipment: STM32F746G-DISCO, 1602A LCD, three LEDs, two pushbuttons, breadboard, jumper wires, and 330 Ω resistors.

**Evidence:** GPIO assignments below are verified against the supplied source code. Header locations are checked against ST's board manual. Physical LED polarity, LCD supply/backlight wiring, and the exact VO divider tap were not independently confirmed; those connections are marked as inferred. A5/A4 must be routed to PF6/PF7, rather than the alternate I²C pins.

| Device / signal | MCU pin | Board header | Connection / status |
|---|---|---|---|
| LED1, green | PI3 | D7, CN4-8 | GPIO assignment verified; inferred: through 330 Ω to LED anode, cathode to GND |
| LED2, orange | PB4 | D3, CN4-4 | Same inferred LED connection |
| LED3, blue | PG7 | D4, CN4-5 | Same inferred LED connection |
| SW1 | PI1 | D13, CN7-6 | Button to GND, inferred from internal pull-up and falling-edge EXTI1 configuration |
| SW2 | PI2 | D8, CN7-1 | Button to GND, inferred from internal pull-up and falling-edge EXTI2 configuration |
| LCD 4, RS | PF6 | A5, CN5-6 | Code-verified signal assignment |
| LCD 6, E | PF7 | A4, CN5-5 | Code-verified signal assignment |
| LCD 11, DB4 | PA0 | A0, CN5-1 | Code-verified signal assignment |
| LCD 12, DB5 | PF10 | A1, CN5-2 | Code-verified signal assignment |
| LCD 13, DB6 | PF9 | A2, CN5-3 | Code-verified signal assignment |
| LCD 14, DB7 | PF8 | A3, CN5-4 | Code-verified signal assignment |
| LCD 1, VSS | — | GND, CN6-6 or CN6-7 | Common ground; inferred |
| LCD 2, VDD | — | +5 V, CN6-5 | Typical 5 V hookup; actual supply not confirmed |
| LCD 3, VO | — | Resistor-divider tap | Contrast fix confirmed; exact tap and rail voltage unconfirmed |
| LCD 5, R/W | — | GND | Inferred write-only connection; code never reads LCD |
| LCD 7–10, DB0–DB3 | — | — | Unused in 4-bit mode; leave disconnected |
| LCD 15, A / LED+ | — | Backlight supply through current limiting | Inferred; preserve the working module's current-limiting arrangement |
| LCD 16, K / LED− | — | GND | Inferred |

Header reference: [ST UM1907, Arduino connector table](https://www.st.com/resource/en/user_manual/dm00190424-discovery-kit-for-stm32f7-series-with-stm32f746ng-mcu-stmicroelectronics.pdf). LCD numbering follows the [standard LCD1602 interface](https://www.waveshare.com/lcd1602-blue.htm); check the module's printed labels.

## Simple wiring diagram

[View the circuit schematic (PDF)](Lab_3_Circuit_Schematic.pdf) — component symbols and connecting wires for the board, LCD, LEDs, buttons, and contrast resistor chain. Unconfirmed connections are labeled.

```text
STM32 board                          1602A LCD
A5 / PF6 ---------------------------  4 RS
A4 / PF7 ---------------------------  6 E
A0 / PA0 --------------------------- 11 DB4
A1 / PF10 -------------------------- 12 DB5
A2 / PF9 --------------------------- 13 DB6
A3 / PF8 --------------------------- 14 DB7

Inferred power connections:
+5 V -------------------------------  2 VDD
GND --------------------------------  1 VSS
GND --------------------------------  5 R/W
Divider tap (see below) -------------  3 VO
Backlight supply -- current limiting 15 A (+)
GND -------------------------------- 16 K (-)
                                     7–10 unused

Inferred LED and button connections:
D7 / PI3 -- 330 ohm -- green LED anode;  cathode -- GND
D3 / PB4 -- 330 ohm -- orange LED anode; cathode -- GND
D4 / PG7 -- 330 ohm -- blue LED anode;   cathode -- GND
D13/ PI1 -- SW1 -- GND  (internal pull-up)
D8 / PI2 -- SW2 -- GND  (internal pull-up)

Contrast divider — topology illustrative; actual VO tap unconfirmed:
Vcontrast --[330 ohm]--o--[330 ohm]--o--[330 ohm]-- GND
                      T1          T2
LCD pin 3 VO -------- actual working tap: T1 or T2, not recorded
```

The three contrast resistors are **in series**, totaling **990 Ω**. With the illustrated rail-to-ground arrangement, unloaded T1 is 2/3 of the rail and T2 is 1/3. These are calculated possibilities, not measured VO values. Three resistors in series alone do not establish VO without a defined tap and rail connections.

## Program operation

- Normal mode: orange LED blinks five times, blue LED blinks eight times, then the sequence pauses for eight TIM2 updates and repeats.
- SW1: EXTI1 runs three green LED blinks. SW2: EXTI2 runs five blue LED blinks. SW2 has higher configured priority. Each handler restores the previous display mode afterward.
- LCD: RS, E, and DB4–DB7 send each byte as two nibbles. Status messages identify normal/interrupt mode, LED color, and blink count in column 16.
- Timing: TIM2 uses `PSC = 15999`, `ARR = 250`, and a polled update flag. At an assumed 16 MHz TIM2 input, one count is 1 ms and one update is **251 ms**, giving a normal full blink of approximately 502 ms. The eight-update pause is approximately 2.008 s. The actual timer clock and periods were not measured.
- Interrupt blinks use software delays. Button rearming requires 20 timer counts of stable release, approximately 20 ms under the same clock assumption. Blocking handlers can delay normal sequencing; elapsed timer updates are not queued individually by the update flag.

## Photos of the completed lab

| Photo | What it documents |
|---|---|
| [Full board and breadboard](photos/Full_board.jpg) | Overall hardware setup, external LEDs, wiring, and powered LCD. |
| [INT1: green LED mode](photos/INT1-_GRN-_number.jpg) | LCD displays `INT1 -> GRN ->` with count `2`. |
| [Normal mode: blue LED](photos/NOR-_BLU-_number.jpg) | LCD displays `NOR -> BLU ->` with count `8`. |
| [Normal mode: orange LED](photos/Nor-_ORG-_number.jpg) | LCD displays `NOR -> ORG ->` with count `2`. |

These photos document readable LCD status messages in normal mode and INT1 mode. Still images do not establish blink timing, complete blink sequences, or INT2 operation. The exact VO tap and supply voltages cannot be established reliably from these views.

## Results and conclusion

User-reported results: all three LEDs blinked, both button interrupts worked, and the completed lab operated correctly. The LCD initially showed blocks or poorly distinguishable text. Installing the VO contrast divider using three 330 Ω resistors in series made the text readable and resolved the final issue.

The final fault was LCD contrast. The source establishes the intended timing and blink counts; these exact values and interrupt preemption behavior were not independently measured. The remaining wiring detail to record is the actual VO tap, divider rail voltage, and LCD/backlight supply arrangement.
