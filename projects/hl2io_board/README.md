# HL2IO Board Pico Firmware

This is a Raspberry Pi Pico (RP2040) firmware project that emulates the behavior of the HL2IO board. It acts as an I2C slave on address `0x1D`, accepts 40-bit transmit frequency updates to switch relay filter bands, and monitors the PTT input.

## Features
- Listen on 7-bit I2C address `0x1D` (standard HL2IO Pico address).
- Decode the 40-bit transmit frequency (in Hz) from registers `0` through `4`.
- Dynamically toggle GPIO pins corresponding to selected amateur bands (160m, 80m, 40m, 20m, 15m, 10m, 6m).
- Detect PTT states (Transmit / Receive) on a hardware pin with pull-down protection.
- Print live status and logs via the USB Serial debug console.

## Wiring Table

| Signal | Red Pitaya Pin (STEMlab 125-14) | Raspberry Pi Pico Pin | Logic Level |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | **E2 Connector Pin 11** (MIO51) | **GP14** (Pin 19) | 3.3V (Shared) |
| **I2C SCL** | **E2 Connector Pin 10** (MIO50) | **GP15** (Pin 20) | 3.3V (Shared) |
| **GND** | **E1/E2 Connector GND** | **Any GND Pin** | Common Ground |
| **PTT Input** | **E1 Connector Pin 3 (DIO0_P)** | **GP13** (Pin 17) | 3.3V Input |

*Note: GP25 (Onboard Pico LED) will illuminate when PTT is active (TRANSMIT state).*

## Compilation Instructions

To compile the firmware, you need the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) installed and the `PICO_SDK_PATH` environment variable set.

```bash
# Create build directory
mkdir build
cd build

# Run CMake configuration (specifying your Pico SDK path if not in env)
cmake -DPICO_SDK_PATH=/path/to/pico-sdk ..

# Compile the firmware
make
```

After compilation, copy the generated `hl2io_board.uf2` file onto your Raspberry Pi Pico in Bootsel mode.
