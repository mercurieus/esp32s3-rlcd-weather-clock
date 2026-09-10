# ESP32-S3 RLCD Weather Clock

A simple desk clock built around the [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/esp32-s3-rlcd-4.2.htm) — a 4.2" reflective LCD (e-paper-like) development board.

The project was written from scratch in **VS Code with ESP-IDF**, with help from AI along the way. I really don't like working in the Arduino environment, so ESP-IDF + VS Code was the natural choice here.

![Finished clock](img/photo.jpg)

## Features

- Time and date on a low-power reflective display
- Indoor temperature and humidity from the onboard SHTC3 sensor (top-left corner of the screen) — being an onboard sensor, it sits close to the electronics, so readings are only approximate, not lab-grade
- 4-day weather forecast fetched over Wi-Fi (Open-Meteo)
- Battery voltage / percentage indicator
- RTC-based timekeeping (PCF85063) that survives power loss

## Setup

Everything configurable lives in menuconfig, under **Weather Clock**:

```
idf.py menuconfig
```

| Submenu | What to set |
|---|---|
| Wi-Fi | SSID and password |
| Time | POSIX timezone string, NTP server, the two daily time-sync hours |
| Weather | Latitude and longitude, refresh interval, stale threshold |
| Battery | Low-battery warning and critical-shutdown thresholds |

Your answers are written to `sdkconfig`, which is **not tracked by git** — it
holds your Wi-Fi password and your location. `sdkconfig.defaults` is tracked and
carries only project-wide settings, so a fresh clone builds with empty
credentials and you set your own.

## Controls

| Button | Action |
|---|---|
| Left   | Force an immediate weather update |
| Middle | Long press: power off · Short press: power back on |
| Right  | Not used |

## How it works

The clock spends most of its time in **light sleep** to save power, waking once a minute to refresh the time from the internal RTC. Twice a day, at **05:00** and **15:00**, it syncs the RTC against NTP. Weather runs on its own shorter cycle - **every 30 minutes** by default - and shares a Wi-Fi bring-up with the time sync whenever the two coincide. All four values are configurable.

While a USB host is attached the clock does not light-sleep at all: light sleep suspends the USB Serial/JTAG peripheral, which would make the serial port disappear. On battery it sleeps as normal.

## ⚠️ Battery life

This module is **not particularly battery-friendly**. With the screen on, it draws around **8 mA even in light sleep**, which works out to roughly **14 days** of runtime on a single charge. Keep that in mind if you're planning to run it purely on battery.

## Hardware

- [Waveshare ESP32-S3-RLCD-4.2](https://www.waveshare.com/esp32-s3-rlcd-4.2.htm)
