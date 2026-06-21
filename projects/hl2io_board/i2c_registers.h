#ifndef I2C_REGISTERS_H
#define I2C_REGISTERS_H

// These are the registers in the HL2 IO board Pico.
#define REG_TX_FREQ_BYTE4 0
#define REG_TX_FREQ_BYTE3 1
#define REG_TX_FREQ_BYTE2 2
#define REG_TX_FREQ_BYTE1 3
#define REG_TX_FREQ_BYTE0 4
#define REG_CONTROL       5
#define REG_INPUT_PINS    6
#define REG_ANTENNA_TUNER 7
#define REG_FAULT         8
#define REG_RX1_ATT       9
#define REG_RX2_ATT       10
#define REG_TX_DRIVE      11
#define REG_ADC0_MSB      12
#define REG_ADC0_LSB      13
#define REG_ADC1_MSB      14
#define REG_ADC1_LSB      15

#endif // I2C_REGISTERS_H
