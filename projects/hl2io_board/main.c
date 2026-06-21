#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "i2c_registers.h"

// I2C Slave Address configuration
#define I2C_ADDR 0x1D

// Pico GPIO definitions
#define PIN_PTT_INPUT 13
#define PIN_LED       25

// SDA/SCL pins
#define PIN_I2C_SDA   14
#define PIN_I2C_SCL   15

// Relay/Band outputs
#define BAND_160M     0
#define BAND_80M      1
#define BAND_60M_40M  2
#define BAND_30M      3
#define BAND_20M      4
#define BAND_17M_15M  5
#define BAND_12M_10M  6
#define BAND_6M       7

const uint8_t band_gpios[] = {
    BAND_160M, BAND_80M, BAND_60M_40M, BAND_30M,
    BAND_20M, BAND_17M_15M, BAND_12M_10M, BAND_6M
};
#define NUM_BAND_GPIOS (sizeof(band_gpios) / sizeof(band_gpios[0]))

// Registers array
static uint8_t Registers[256];
static uint8_t write_addr = 0;
static bool write_addr_written = false;

// Flags for main loop
static volatile bool frequency_updated = false;
static volatile bool reset_requested = false;
static volatile bool rx1_att_updated = false;
static volatile bool rx2_att_updated = false;
static volatile bool tx_drive_updated = false;

// Track PTT status
static bool ptt_active = false;

// Update filter relays based on frequency
void update_band_filters(uint64_t freq_hz) {
    // Clear all band filter pins first
    for (int i = 0; i < NUM_BAND_GPIOS; i++) {
        gpio_put(band_gpios[i], 0);
    }

    uint8_t selected_pin = 255;
    const char *band_name = "Unknown/None";

    if (freq_hz >= 1800000 && freq_hz <= 2000000) {
        selected_pin = BAND_160M;
        band_name = "160m";
    } else if (freq_hz >= 3500000 && freq_hz <= 4000000) {
        selected_pin = BAND_80M;
        band_name = "80m";
    } else if (freq_hz >= 5000000 && freq_hz <= 7300000) {
        selected_pin = BAND_60M_40M;
        band_name = "60m/40m";
    } else if (freq_hz >= 10100000 && freq_hz <= 10150000) {
        selected_pin = BAND_30M;
        band_name = "30m";
    } else if (freq_hz >= 14000000 && freq_hz <= 14350000) {
        selected_pin = BAND_20M;
        band_name = "20m";
    } else if (freq_hz >= 18000000 && freq_hz <= 21450000) {
        selected_pin = BAND_17M_15M;
        band_name = "17m/15m";
    } else if (freq_hz >= 24890000 && freq_hz <= 29700000) {
        selected_pin = BAND_12M_10M;
        band_name = "12m/10m";
    } else if (freq_hz >= 50000000 && freq_hz <= 54000000) {
        selected_pin = BAND_6M;
        band_name = "6m";
    }

    if (selected_pin != 255) {
        gpio_put(selected_pin, 1);
        printf("[HL2IO] Freq: %llu Hz -> Selected Band: %s (GPIO %d High)\n", freq_hz, band_name, selected_pin);
    } else {
        printf("[HL2IO] Freq: %llu Hz -> Out of amateur bands. All relays off.\n", freq_hz);
    }
}

// I2C Slave interrupt handler callback
static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
        case I2C_SLAVE_RECEIVE: { // master writes to slave
            uint8_t byte = i2c_read_byte_raw(i2c);
            if (!write_addr_written) {
                write_addr = byte;
                write_addr_written = true;
            } else {
                Registers[write_addr] = byte;
                
                // If LSB of TX frequency is written, trigger freq update
                if (write_addr == REG_TX_FREQ_BYTE0) {
                    frequency_updated = true;
                }
                
                // If control register is written with reset code
                if (write_addr == REG_CONTROL && byte == 1) {
                    reset_requested = true;
                }

                // If attenuation or drive registers are written
                if (write_addr == REG_RX1_ATT) {
                    rx1_att_updated = true;
                }
                if (write_addr == REG_RX2_ATT) {
                    rx2_att_updated = true;
                }
                if (write_addr == REG_TX_DRIVE) {
                    tx_drive_updated = true;
                }
                
                write_addr++;
            }
            break;
        }
        case I2C_SLAVE_REQUEST: { // master reads from slave
            i2c_write_byte_raw(i2c, Registers[write_addr]);
            write_addr++;
            break;
        }
        case I2C_SLAVE_FINISH: { // master finished transaction
            write_addr_written = false;
            break;
        }
        default:
            break;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("[HL2IO] Board Emulator starting...\n");

    // Initialize onboard LED
    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 0);

    // Initialize ADC hardware
    adc_init();
    adc_gpio_init(26); // GP26/ADC0
    adc_gpio_init(27); // GP27/ADC1

    // Initialize PTT input pin (with pull-down resistor)
    gpio_init(PIN_PTT_INPUT);
    gpio_set_dir(PIN_PTT_INPUT, GPIO_IN);
    gpio_pull_down(PIN_PTT_INPUT);

    // Initialize band filter relay outputs
    for (int i = 0; i < NUM_BAND_GPIOS; i++) {
        gpio_init(band_gpios[i]);
        gpio_set_dir(band_gpios[i], GPIO_OUT);
        gpio_put(band_gpios[i], 0);
    }

    // Initialize I2C0 pins
    gpio_init(PIN_I2C_SDA);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);

    gpio_init(PIN_I2C_SCL);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SCL);

    // Setup I2C0 hardware peripheral at 100 kHz as a slave
    i2c_init(i2c0, 100000);
    i2c_slave_init(i2c0, I2C_ADDR, &i2c_slave_handler);

    printf("[HL2IO] I2C Slave initialized at 0x%02X on SDA/GP14, SCL/GP15.\n", I2C_ADDR);
    printf("[HL2IO] PTT input configured on GP13. Waiting for data...\n");

    while (1) {
        // Handle frequency update request
        if (frequency_updated) {
            frequency_updated = false;
            
            // Reassemble the 40-bit frequency from the register bytes
            uint64_t freq_hz = ((uint64_t)Registers[REG_TX_FREQ_BYTE4] << 32) |
                               ((uint64_t)Registers[REG_TX_FREQ_BYTE3] << 24) |
                               ((uint64_t)Registers[REG_TX_FREQ_BYTE2] << 16) |
                               ((uint64_t)Registers[REG_TX_FREQ_BYTE1] << 8)  |
                               (uint64_t)Registers[REG_TX_FREQ_BYTE0];
            
            update_band_filters(freq_hz);
        }

        // Handle RX1 attenuation update
        if (rx1_att_updated) {
            rx1_att_updated = false;
            printf("[HL2IO] RX1 Attenuation updated: %d dB\n", Registers[REG_RX1_ATT]);
        }

        // Handle RX2 attenuation update
        if (rx2_att_updated) {
            rx2_att_updated = false;
            printf("[HL2IO] RX2 Attenuation updated: %d dB\n", Registers[REG_RX2_ATT]);
        }

        // Handle TX drive level update
        if (tx_drive_updated) {
            tx_drive_updated = false;
            printf("[HL2IO] TX Drive Level updated: %d\n", Registers[REG_TX_DRIVE]);
        }

        // Handle software reset request
        if (reset_requested) {
            reset_requested = false;
            printf("[HL2IO] Reset command received. Resetting registers and outputs.\n");
            
            for (int i = 0; i < 256; i++) {
                Registers[i] = 0;
            }
            for (int i = 0; i < NUM_BAND_GPIOS; i++) {
                gpio_put(band_gpios[i], 0);
            }
        }

        // Poll PTT state
        bool current_ptt = gpio_get(PIN_PTT_INPUT);
        if (current_ptt != ptt_active) {
            ptt_active = current_ptt;
            gpio_put(PIN_LED, ptt_active ? 1 : 0);
            printf("[HL2IO] PTT changed -> state: %s\n", ptt_active ? "TRANSMIT" : "RECEIVE");
            
            // Keep Register 6 updated with input status for client reads (bit 0 = PTT status)
            Registers[REG_INPUT_PINS] = ptt_active ? 1 : 0;
        }

        // Periodically read Pico ADC inputs (every 50ms / 5 loops)
        static int adc_read_counter = 0;
        if (++adc_read_counter >= 5) {
            adc_read_counter = 0;
            adc_select_input(0);
            uint16_t adc0_val = adc_read();
            adc_select_input(1);
            uint16_t adc1_val = adc_read();

            Registers[REG_ADC0_MSB] = (adc0_val >> 8) & 0xff;
            Registers[REG_ADC0_LSB] = adc0_val & 0xff;
            Registers[REG_ADC1_MSB] = (adc1_val >> 8) & 0xff;
            Registers[REG_ADC1_LSB] = adc1_val & 0xff;
        }

        // Sleep to avoid pegging cpu core
        sleep_ms(10);
    }

    return 0;
}
