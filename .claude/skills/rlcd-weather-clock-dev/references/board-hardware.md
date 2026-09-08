# Board hardware reference — Waveshare ESP32-S3-RLCD-4.2

First-party facts (Waveshare + Espressif), adapted for this project and this
Windows machine. Upstream research snapshot: Waveshare repo commit
[`eb1f634`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/commit/eb1f63427d735a22b9c30e22fa63ebddae1834d3).

Do not reuse any of this for the similarly-named `ESP32-S3-Touch-LCD-*` boards.

## Specification

| Item | Official | Notes |
| --- | --- | --- |
| MCU | ESP32-S3-WROOM-1-N16R8, dual Xtensa LX7, to 240 MHz | |
| Flash | 16 MB Quad SPI | A full dump is exactly `0x1000000` = 16,777,216 bytes |
| PSRAM | 8 MB Octal SPI | ESP-IDF: Octal 80 MHz. Confirm from boot log or `esp_psram_get_size()`, never from `flash-id` |
| Display | ST7305, 300x400 monochrome fully-reflective, **no backlight** | Presented as 400x300 landscape |
| Touch | **None** | No touch controller enumerates. Never add a touch driver or LVGL input device |

The panel gets *more* readable in bright light and looks dim in the dark. That is
the technology, not a fault.

## Pin map

| Peripheral | IC / function | GPIO / bus | Notes |
| --- | --- | --- | --- |
| RLCD | ST7305 | SCK 11, MOSI 12, DC 5, CS 40, RST 41, TE 6 | SPI mode 0, write-only, no MISO; official driver runs 10 MHz |
| USB-C | USB Serial/JTAG | D- 19, D+ 20 | Flash, console and JTAG share the native USB controller |
| I2C | shared bus | SDA 13, SCL 14 | RTC, SHTC3 and the audio codecs share it |
| RTC | PCF85063A | I2C `0x51` | |
| Temp/humidity | SHTC3 | I2C `0x70` | |
| Audio DAC | ES8311 | I2C + I2S | speaker output |
| Audio ADC | ES7210 | I2C + I2S | dual microphone |
| I2S | audio data | DOUT 8, BCLK 9, DIN 10, MCLK 16, LRCLK 45 | |
| Speaker amp | NS4150B enable | GPIO46 | **Also a strapping pin** — set high only after app startup, never externally pulled high during reset/download |
| microSD | SDMMC 1-bit | CLK 38, CMD 21, D0 39 | Not on the display SPI bus |
| Battery sense | 18650 divider | GPIO4 / ADC1 ch3 | 3x divider; needs per-unit software calibration |
| BOOT | active-low button | GPIO0 | Download strap at reset; also an app button here |
| KEY | active-low button | GPIO18 | User button |
| PWR | power management | not an app GPIO | Short press = on, long press = off |
| UART0 | console header | TX 43, RX 44 | Present on the schematic; see "escape hatch" below |

Verified against this repo's `main/user_config.h` for every pin this project uses.

**Physical order, left to right: PWR, KEY, BOOT.** To identify them without
guessing: the firmware logs a line on KEY and BOOT presses, and PWR is not an app
GPIO so it produces nothing. A short press of PWR on an already-running board also
does nothing (short press is power-on), so probing each one is safe.

## Download mode — there is no RESET button

This board has **no RESET button**. Any instruction of the form "hold BOOT, tap
RESET" is wrong here. Waveshare's procedure:

1. **Long-press PWR** to power off.
2. **Hold BOOT.**
3. **Short-press PWR** to power on, keep BOOT held about 1 second, then release.
4. Re-list serial ports — the native USB re-enumerates and the port number may change.
5. `idf.py -p COM8 flash`, or `python -m esptool --chip esp32s3 -p COM8 flash-id`.

GPIO0 low at reset enters the ROM serial bootloader; GPIO46 must be floating or
low or the downloader is not reached.

## The UART escape hatch

`esp_light_sleep_start()` suspends USB Serial/JTAG, and this project's console is
USB-only (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, `CONFIG_ESP_CONSOLE_UART_NUM=-1`),
so there is no log to read while the app runs.

**UART0 on TX 43 / RX 44 is the way out** if a real log is ever needed: set
`CONFIG_ESP_CONSOLE_UART_DEFAULT` (or the secondary console) and attach a
USB-serial adapter. A UART does not vanish from the host when the chip sleeps, so
it survives the sleep loop that makes USB CDC unusable.

Until that is wired up, put diagnostics **on the panel** — that is why the last
`esp_reset_reason()` is displayed on the settings screen.

## Back up before the first write

Enter download mode first. Treat dumps as sensitive: they contain NVS, which holds
Wi-Fi credentials. Do not commit one.

```powershell
python -m esptool --chip esp32s3 -p COM8 flash-id
python -m esptool --chip esp32s3 -p COM8 read-flash 0x0 0x1000000 factory-backup.bin
(Get-Item factory-backup.bin).Length          # must be 16777216
Get-FileHash factory-backup.bin -Algorithm SHA256
```

## Restore

**From this board's own dump** — the only thing that restores its NVS,
calibration and settings:

```powershell
python -m esptool --chip esp32s3 -p COM8 write-flash 0x0 factory-backup.bin
python -m esptool --chip esp32s3 -p COM8 verify-flash 0x0 factory-backup.bin
```

**From Waveshare's factory image** — restores the demo, *not* your data. Pinned to
the research snapshot:

```text
URL:    https://raw.githubusercontent.com/waveshareteam/ESP32-S3-RLCD-4.2/eb1f63427d735a22b9c30e22fa63ebddae1834d3/03_Firmware/01_Factory_V1.bin
size:   4,548,144 bytes
sha256: d0591315a722d33f4a08931a0341ab840a6c15c56b289d621e8fc18bec8d55a8
```

Verify the hash before writing, then `write-flash 0x0` (it is a merged image).
`erase-flash` first only if a genuinely clean state is wanted — it destroys
everything, so it requires a verified backup first, and it is blocked by default
under Secure Boot or Flash Encryption. Never `--force` against an unknown
security state.

## Traps

1. **Wrong memory mode.** ESP-IDF needs Octal PSRAM 80 MHz and 16 MB Flash. QSPI PSRAM or an 8 MB Flash setting can compile fine and crash at runtime.
2. **Mixing LVGL major versions.** Keep example, `lv_conf.h` and library as a set.
3. **Treating the RLCD as a colour TFT.** It is monochrome ST7305 — no ST7789/ILI9341/RGB565 flush. An `RGB565A8` asset name in an LVGL demo does not make the panel colour; the BSP does the monochrome conversion.
4. **Expecting a backlight.** There is none.
5. **Looking for a touch driver.** There is none.
6. **USB port vanishing after a flash.** The app may have changed the console config, crashed early, or re-enumerated. Use the PWR+BOOT recovery, not an erase.
7. **GPIO46.** Speaker-amp enable *and* strapping pin. No external pull-up.
8. **SD bus.** SDMMC 1-bit on 38/21/39, not the display SPI bus.
9. **RTC battery.** The holder charges, so it takes **ML1220 or compatible rechargeable only — never CR1220.**
10. **First boot on battery.** After fitting an 18650, connect Type-C once to start the power path, then unplug to test battery operation.
11. **High baud.** 921600 is not reliable on every cable/hub. Drop to 460800 or 115200 on connection errors.
12. **No backup before the first write.** The factory image cannot restore this unit's NVS or calibration.

## First-party sources

- [Product documentation](https://docs.waveshare.com/ESP32-S3-RLCD-4.2)
- [Resources: schematic, datasheets, repo](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Resources-And-Documents)
- [ESP-IDF setup guide](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/Development-Environment-Setup-ESP-IDF)
- [FAQ](https://docs.waveshare.com/ESP32-S3-RLCD-4.2/FAQ)
- [Waveshare GitHub repo](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
- [Schematic PDF](https://files.waveshare.com/wiki/ESP32-S3-RLCD-4.2/ESP32-S3-RLCD-4.2-schematic.pdf)
- [ESP32-S3-WROOM-1 datasheet](https://www.espressif.com/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
- [USB Serial/JTAG console](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html)
- [esptool basic commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
