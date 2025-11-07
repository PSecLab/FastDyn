/**
  @page GPIO_LED_UART_Toggle LED and UART basic example using HAL

  @verbatim
  ******************** (C) COPYRIGHT 2025 STMicroelectronics ********************
  * @file    GPIO/GPIO_LED_UART_Toggle/readme.txt
  * @author  MCD Application Team
  * @brief   Description of the GPIO LED Toggle and UART initialization example.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  @endverbatim

  @par Example Description

  This example demonstrates how to configure GPIOs and basic peripherals
  using the STM32L5 HAL drivers to blink on-board LEDs and initialize the UART
  peripheral.

  The application is based on the STM32L562E-DK Discovery board.

  The following operations are performed:
   - System clock configuration at 110 MHz using the MSI and PLL.
   - Enable instruction cache.
   - Initialize GPIO ports and configure LED pins as output.
   - Toggle the on-board LEDs (LED_RED and LED_GREEN) in an infinite loop.
   - Insert a delay of 500 ms between toggles using HAL_Delay().

  The project can also be executed in a simulation or Hardware-In-The-Loop
  (HITL) environment (for example, under QEMU or passthrough emulation).
  In such cases, all peripheral register reads and writes may be logged for
  observation or forwarded to hardware via a backend interface.

  The example also includes an optional low-level GPIO configuration routine
  (`gpio_init_pa5()`) that directly accesses registers without the HAL layer,
  to demonstrate bit manipulation and atomic GPIO control using the BSRR register.

  When executed successfully:
   - LED_GREEN and LED_RED toggle alternately every 500 ms.
   - SysTick interrupts (IRQ #15) are generated every 1 ms for HAL time base.
   - No Error_Handler() should be entered.

  @note The GPIO initialization and LED toggle are performed using HAL functions.
        In case of error during initialization, the system will remain in
        Error_Handler().

  @note When the firmware is run in a HITL or QEMU environment, unimplemented
        peripheral accesses (such as RCC or FLASH registers) may generate
        "IO Access NOT Handled" messages. These can be avoided by mapping the
        corresponding memory regions (0x40021000–0x40022FFF) and bit-band alias
        region (0x42000000–0x43FFFFFF).

  @par Keywords

  GPIO, LED, HAL, UART, RCC, Clock Configuration, SysTick, QEMU, HITL, STM32L5

  @par Directory contents

    - GPIO/GPIO_LED_UART_Toggle/Inc/stm32l5xx_hal_conf.h     HAL configuration file
    - GPIO/GPIO_LED_UART_Toggle/Inc/stm32l5xx_it.h           Interrupt handlers header file
    - GPIO/GPIO_LED_UART_Toggle/Inc/main.h                   Main program header file
    - GPIO/GPIO_LED_UART_Toggle/Src/main.c                   Main program body
    - GPIO/GPIO_LED_UART_Toggle/Src/stm32l5xx_it.c           Interrupt handlers
    - GPIO/GPIO_LED_UART_Toggle/Src/stm32l5xx_hal_msp.c      HAL MSP module
    - GPIO/GPIO_LED_UART_Toggle/Src/system_stm32l5xx.c       System clock configuration

  @par Hardware and Software environment

   - This example runs on STM32L562xx devices.

   - The example has been tested with STMicroelectronics STM32L562E-DK board
     and can be easily adapted to any other STM32L5xx-based board.

   - Two on-board LEDs are used:
        * LED_GREEN: GPIOG pin
        * LED_RED  : GPIOB pin

   - UART pins are initialized but not actively used in this demonstration.

   - The example can also be executed under QEMU or other simulation backends.

  @par How to use it ?

   In order to make the program work, you must do the following:
    - Open your preferred toolchain supporting STM32 (e.g., STM32CubeIDE)
    - Rebuild all files and load the image into target memory
    - Run the example
    - Observe LED toggling every 500 ms

   For QEMU/HITL execution:
    - Ensure peripheral regions (RCC, FLASH, GPIO, bit-band alias) are included
      in the emulated memory map.
    - Start the binary using your HITL configuration JSON with passthrough
      enabled for the required address ranges.

  @par Notes

   Care must be taken when using HAL_Delay(), as this function depends on the
   SysTick interrupt to increment the time base. Ensure that SysTick has higher
   priority (numerically lower) than other peripheral interrupts to maintain
   correct delay operation.

  @par Example Behavior

   - LED_RED and LED_GREEN toggle alternately.
   - SysTick interrupt triggers regularly.
   - If any HAL configuration function fails, Error_Handler() is executed,
     causing the application to halt.

  @par References

   - STM32L5xx Reference Manual (RM0438)
   - STM32L562E-DK Discovery Kit User Manual (UM2638)
   - STM32CubeL5 Firmware Package

  @par Revision History

   @table
     <tr><th>Date</th><th>Version</th><th>Description</th></tr>
     <tr><td>Feb-2025</td><td>V1.0.0</td><td>Initial version</td></tr>
   @endtable

  ******************************************************************************
*/
