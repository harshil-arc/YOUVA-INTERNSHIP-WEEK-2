# STM32F407VG Bare-Metal UART Firmware

This example targets the STM32F407VG on the STM32F4-Discovery board. It uses
only direct memory-mapped registers and GNU Arm Embedded GCC; no vendor HAL or
CMSIS headers are required.

## Hardware

- USART2: PA2 (TX) and PA3 (RX), 9600 baud, 8 data bits, no parity, 1 stop bit.
- Connect a 3.3 V USB-to-UART adapter with crossed TX/RX and a shared ground.
	Do not connect a 5 V UART signal to the MCU.
- TIM2 generates a 1-second interrupt.
- The on-board green LED at PD12 toggles once per second while blinking is on.
- The clock is explicitly configured to the 16 MHz internal HSI oscillator.

## Commands

Send one ASCII byte at a time:

- `1`: toggle LED blinking and receive `blink=on` or `blink=off`.
- `2`: receive tick, blink, receive-overflow, UART-error, and transmit-drop
	counters.

The RX ring holds 64 bytes (all 64 slots are usable). The main loop parses
commands while UART RX and TX remain interrupt-driven. Bytes arriving after the
RX ring fills are discarded and counted in `rx_overflow`.

## Build

Install the GNU Arm Embedded toolchain (`arm-none-eabi-gcc` and
`arm-none-eabi-objcopy`), then run:

```sh
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -std=c11 -O2 \
	-Wall -Wextra -Werror -ffreestanding -fno-builtin \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffunction-sections -fdata-sections -nostdlib \
	-Tstm32f407vg.ld -Wl,--gc-sections \
	main.c -lgcc -o firmware.elf
arm-none-eabi-objcopy -O ihex firmware.elf firmware.hex
```

`main.c` includes the reset handler, vector table, `.data`/`.bss` initialization,
peripheral setup, ISRs, and application loop. The linker script describes the
STM32F407VG's 1 MiB flash and 128 KiB main SRAM. To flash with OpenOCD and an
ST-Link, for example:

```sh
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
	-c "program firmware.elf verify reset exit"
```

## Simulation

QEMU builds that include the STM32F405/F407 `netduinoplus2` machine can be used
to exercise the Cortex-M startup and UART path, for example with
`qemu-system-arm -M netduinoplus2 -kernel firmware.elf -nographic`. Peripheral
and pin-model coverage varies by QEMU version; confirm its USART2 console
mapping before relying on it for UART testing. Wokwi can also be used when the
selected project board exposes a compatible STM32F4 and USART2 pins. Neither
simulator replaces checking the LED pin, baud rate, and interrupt behavior on
the actual target.