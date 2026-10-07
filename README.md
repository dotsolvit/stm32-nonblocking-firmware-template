# STM32F103 Non-Blocking Firmware Template

🌐 **Read me in English** | [Читати мене Українською](README_uk.md)

A production-ready, production-grade event-driven firmware blueprint for the **STM32F103C8T6** microcontroller, developed using **STM32CubeHAL**. This project serves as a comprehensive template demonstrating clean asynchronous architecture, low-level Flash operations, ring buffers, and precise hardware peripherals configuration.

## 🎯 Key Project Goals
- **Eliminate Blocking Code:** Demonstration of an event-driven system architecture with a completely non-blocking main execution loop.
- **Interrupt Efficiency:** Offloading Interrupt Service Routines (ISR) to maintain minimal execution latency and protect data integrity.
- **Hardware Integration:** Combining distinct MCU peripherals (Flash, UART, Timers/PWM, EXTI, ADC) into a cohesive, production-style application.

## 🛠️ Hardware & Toolchain Specifications
- **Target MCU:** STM32F103C8T6 (ARM Cortex-M3 @ 72 MHz)
- **Clock Profile:** External 8 MHz Crystal (HSE), SysTick/Core configured to maximum **72 MHz**.
- **IDE & Compiler:** STM32CubeIDE (GCC Compiler / Make build system)
- **Configuration Engine:** STM32CubeMX

## 📊 Hardware-Verified Testing & Engineering Assumptions
Due to the absence of the target physical STM32F103 chip during initial staging, the entire asynchronous firmware business logic was **fully verified and hardware-benchmarked on an STM32F072 Discovery board** (ARM Cortex-M0 @ 48 MHz) using an **OWON SDS210S** digital oscilloscope and an ESP8266 USB-UART bridge (Loopback mode).

The following precise engineering calculations and architectural assumptions are implemented in the project configuration:
1. **8-bit PWM Frequency Alignment:** To write raw incoming UART bytes (0–255) directly into the timer's compare register (`TIM1->CCR1`) without computation scaling, the Counter Period (`ARR`) was locked at **255**. Under a 72 MHz bus clock, the Prescaler (`PSC`) was calculated as:
   \[\text{Prescaler} = \frac{72,000,000\text{ Hz}}{5,000\text{ Hz} \times 256} - 1 = 55.25 \implies \mathbf{55}\]
   *This yields a real physical ШІМ (PWM) frequency of 5.02 kHz, completely within strict engineering tolerance.*
2. **Flash Topology Management:** On the target STM32F103, the Flash page size is **1 KB**. The requirements specify reading and writing at the `0x0800FC00` block, which maps perfectly to the beginning of the final page (**Page 63**). Erasing this page via `HAL_FLASHEx_Erase` affects exactly 1024 bytes at the very edge of the Flash matrix, keeping the main application code completely untouched and safe.
3. **ADC Clock Constraints:** To meet the 12 MHz maximum ADC clock limit under a 72 MHz system clock, the APB2 peripheral clock divider for the ADC interface (`ADC Prescaler`) was explicitly configured to **`/6`** inside the CubeMX Clock Tree structure (72 MHz / 6 = 12 MHz). The 3.3V reference serves as the hardware full-scale measurement threshold.

## 💻 Firmware Architecture & Design Choices
- **Non-blocking Scheduling:** Periodic UART packet transmissions completely avoid blocking delay wrappers. Timing intervals are calculated natively via `HAL_GetTick()`. This ensures instantaneous CPU response to the state changes on the `PB2` digital input pin.
- **Asynchronous Callback Interlocking:**
  - **`EXTI0` (Pin PA0):** Fired by a falling edge trigger, it instantly boots a single-shot ADC conversion via `HAL_ADC_Start_IT` and exits immediately to keep ISR execution tight.
  - **`ADC1` (Conversion Complete):** The `HAL_ADC_ConvCpltCallback` captures the 12-bit voltage sample into a `volatile` variable and sets a status flag without blocking the core.
  - **`USART2` (Receive Complete):** Implements an optimized **Ring Buffer (20 bytes)** using modulo arithmetic (`(rx_index + 1) % BUFFER_SIZE`), updates the PWM duty cycle via macro `__HAL_TIM_SET_COMPARE`, and immediately rearms the IT receiver.
- **Robust Error Handling:** The firmware includes explicit isolation against serial bus noise or line collisions. If an overrun (`ORE`) or framing (`FE`) error occurs, `HAL_UART_ErrorCallback` catches the exception, flushes status flags, and restarts the IT receiver to prevent an infinite deadlock. Flash operations include continuous execution tracking via `HAL_FLASH_GetError()`.

## 📦 Compilation Results
The project builds completely cleanly with zero errors and zero warnings. 
Linker memory utilization report (Map Summary):

```text
   text    data     bss     dec     hex filename
  16056      20    1832   17908    45f4 stm32-nonblocking-firmware-template.elf
```
- **Flash Occupied:** `text` + `data` = **16,076 bytes** (~15.7 KB used out of 64 KB available).
- **SRAM Occupied:** `data` + `bss` = **1,852 bytes** (~1.8 KB used out of 20 KB available).
