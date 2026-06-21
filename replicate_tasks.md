# Replicate Tasks Guide: HL2IO Board & Red Pitaya Debian Image

This guide documents all tasks completed today to enable you to save, backup, or recreate these changes in your GitHub repository.

---

## 1. Pico Board Firmware (`projects/hl2io_board`)

We created a custom Raspberry Pi Pico firmware to act as the HL2IO board. It acts as an I2C slave (`0x1D`), reads 12-bit ADC values from GP26/GP27, registers PTT input on GP13, and manages filter switching relays.

### File: `projects/hl2io_board/i2c_registers.h`
Defines registers 12-15 for transmitting the 12-bit ADC data:
```c
#ifndef I2C_REGISTERS_H
#define I2C_REGISTERS_H

#define REG_RX1_ATT       9
#define REG_RX2_ATT       10
#define REG_TX_DRIVE      11
#define REG_ADC0_MSB      12
#define REG_ADC0_LSB      13
#define REG_ADC1_MSB      14
#define REG_ADC1_LSB      15

#endif
```

### File: `projects/hl2io_board/main.c`
Runs the I2C slave responder and periodically (every 50ms) updates registers `12-15` with Pico ADC0 (GP26) and ADC1 (GP27) readings:
```c
#include <stdio.h>
#include <hardware/i2c.h>
#include <hardware/adc.h>
#include <pico/stdlib.h>
#include <pico/i2c_slave.h>
#include "i2c_registers.h"

#define I2C_PORT i2c1
#define I2C_SDA_PIN 14
#define I2C_SCL_PIN 15
#define I2C_SLAVE_ADDR 0x1D
#define PTT_PIN 13
#define LED_PIN 25

static uint8_t registers[256];
static uint8_t write_addr = 0;

static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
        case I2C_SLAVE_RECEIVE:
            write_addr = i2c_read_byte_raw(i2c);
            break;
        case I2C_SLAVE_REQUEST:
            i2c_write_byte_raw(i2c, registers[write_addr++]);
            break;
        case I2C_SLAVE_FINISH:
            break;
    }
}

int main() {
    stdio_init_all();
    
    // Initialize PTT
    gpio_init(PTT_PIN);
    gpio_set_dir(PTT_PIN, GPIO_IN);
    gpio_pull_down(PTT_PIN);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Initialize ADC
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);

    // Initialize I2C
    gpio_init(I2C_SDA_PIN);
    gpio_set_dir(I2C_SDA_PIN, GPIO_IN);
    gpio_pull_up(I2C_SDA_PIN);

    gpio_init(I2C_SCL_PIN);
    gpio_set_dir(I2C_SCL_PIN, GPIO_IN);
    gpio_pull_up(I2C_SCL_PIN);

    i2c_init(I2C_PORT, 100000);
    i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDR, &i2c_slave_handler);

    while (1) {
        // Read ADC0
        adc_select_input(0);
        uint16_t adc0 = adc_read();
        registers[REG_ADC0_MSB] = (adc0 >> 8) & 0xFF;
        registers[REG_ADC0_LSB] = adc0 & 0xFF;

        // Read ADC1
        adc_select_input(1);
        uint16_t adc1 = adc_read();
        registers[REG_ADC1_MSB] = (adc1 >> 8) & 0xFF;
        registers[REG_ADC1_LSB] = adc1 & 0xFF;

        // Sync PTT status to onboard LED
        gpio_put(LED_PIN, gpio_get(PTT_PIN));

        sleep_ms(50);
    }
}
```

---

## 2. Red Pitaya HPSDR Server Changes (`projects/sdr_transceiver_hpsdr_122_88`)

We modified `sdr-transceiver-hpsdr.c` to read the Pico ADC registers via `/dev/i2c-0` and substitute them into the HPSDR frame payload instead of the local Red Pitaya XADC inputs. We also wrapped all I2C calls with a global `pthread_mutex_t i2c_mutex` lock to prevent race conditions on the shared bus.

### Key Modifications in `sdr-transceiver-hpsdr.c`:
1. **Thread Protection**:
   ```c
   pthread_mutex_t i2c_mutex = PTHREAD_MUTEX_INITIALIZER;
   ```
2. **Background Thread**:
   ```c
   void *hl2io_adc_thread(void *arg) {
     uint8_t reg = 12;
     uint8_t val[4];
     while(enable_thread) {
       pthread_mutex_lock(&i2c_mutex);
       if (i2c_hl2io) {
         // Write register address 12
         write(i2c_fd, &reg, 1);
         // Read 4 bytes (registers 12, 13, 14, 15)
         if (read(i2c_fd, val, 4) == 4) {
           hl2io_adc0 = (val[0] << 8) | val[1];
           hl2io_adc1 = (val[2] << 8) | val[3];
         }
       }
       pthread_mutex_unlock(&i2c_mutex);
       usleep(50000);
     }
     return NULL;
   }
   ```
3. **Payload Substitution** (Line ~1545):
   ```c
   // value = xadc[16] >> 3;
   value = i2c_hl2io ? hl2io_adc0 : (xadc[16] >> 3);
   pointer[4] = (value >> 8) & 0xff;
   pointer[5] = value & 0xff;
   // value = xadc[17] >> 3;
   value = i2c_hl2io ? hl2io_adc1 : (xadc[17] >> 3);
   pointer[6] = (value >> 8) & 0xff;
   pointer[7] = value & 0xff;
   ```

---

## 3. Systemd Service Configurations (`debian/etc/systemd/system/`)

We added systemd service descriptors directly into the Debian configuration tree so they are automatically included and enabled in the final rootfs.

### File: `debian/etc/systemd/system/red-pitaya-bazaar.service`
Runs the lightweight CGI app homepage server on port 80 via `tcpserver`:
```ini
[Unit]
Description=Red Pitaya Bazaar Homepage
After=network.target

[Service]
Type=simple
ExecStartPre=/bin/mkdir -p /media/mmcblk0p1
ExecStartPre=/bin/ln -sf /var/www/apps /media/mmcblk0p1/apps
ExecStart=/usr/bin/tcpserver -R -H -l 0 0 80 /var/www/apps/server/server 122
Restart=always
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### File: `debian/etc/systemd/system/sdr-transceiver-hpsdr-autostart.service`
Autostarts the HPSDR transceiver at boot using its `start.sh` script:
```ini
[Unit]
Description=Autostart HPSDR Transceiver
After=red-pitaya-bazaar.service

[Service]
Type=oneshot
ExecStart=/var/www/apps/sdr_transceiver_hpsdr_122_88/start.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

---

## 4. Debian Image Setup Script (`scripts/debian-122_88.sh`)

A modified clone of `scripts/debian.sh` that:
- Installs the `ucspi-tcp` package to provide `tcpserver`.
- Compiles the CGI server at `alpine/apps/server/server.c` using the ARM compiler wrapper and copies it to `/var/www/apps/server/server`.
- Deploys static Bazaar assets (`index.html`, `index_122_88.html`, `css/`, `stop.sh`) to `/var/www/apps/`.
- Deploys all 10 `122_88` project directories to the rootfs and cross-compiles their servers on the host using temporary `gcc` and `cc` binary path wrappers (intercepting compiler calls to redirect them to `arm-linux-gnueabihf-gcc`).
- Explicitly passes `CC=gcc` to all Makefile invocations, ensuring that Makefiles using `$(CC)` correctly route to our wrapper path instead of standard host `cc`.
- Enables `red-pitaya-bazaar` and `sdr-transceiver-hpsdr-autostart` services.
- Mounts `/proc`, `/sys`, `/dev`, `/dev/pts` to prevent `debootstrap` chroot warning issues.

---

## 5. Unified Helper Script (`helpers/build-debian-122_88.sh`)

Launches the entire build pipeline for the 122.88 MHz image:
```bash
source /opt/Xilinx/2025.2.1/Vitis/settings64.sh

DATE=`date +%Y%m%d`

# Build master boot project
make NAME=sdr_transceiver_hpsdr_122_88 PART=xc7z020clg400-1 all

# Build bitstreams for all 122_88 projects in parallel
JOBS=`nproc 2> /dev/null || echo 1`
PRJS="led_blinker_122_88 sdr_receiver_122_88 sdr_receiver_hpsdr_122_88 sdr_receiver_wide_122_88 sdr_transceiver_122_88 sdr_transceiver_ft8_122_88 sdr_transceiver_hpsdr_122_88 sdr_transceiver_wspr_122_88 pulsed_nmr_122_88 vna_122_88"

printf "%s\n" $PRJS | xargs -n 1 -P $JOBS -I {} make NAME={} PART=xc7z020clg400-1 bit

# Build Debian image
sudo sh scripts/image.sh scripts/debian-122_88.sh red-pitaya-debian-122_88-armhf-$DATE.img 2048
zip red-pitaya-debian-122_88-armhf-$DATE.zip red-pitaya-debian-122_88-armhf-$DATE.img
```

---

## 6. How to Rebuild Everything From Scratch

If you have a fresh clone of this repository on another machine, run these commands to build the image:

1. Compile the master project and parallel bitstreams:
   ```bash
   bash helpers/build-debian-122_88.sh
   ```
2. Build the final formatted, zipped Debian image (providing your sudo password when prompted):
   ```bash
   sudo sh scripts/image.sh scripts/debian-122_88.sh red-pitaya-debian-122_88-armhf-$(date +%Y%m%d).img 2048 && zip red-pitaya-debian-122_88-armhf-$(date +%Y%m%d).zip red-pitaya-debian-122_88-armhf-$(date +%Y%m%d).img
   ```
