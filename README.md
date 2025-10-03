# 🌱 Smart IoT Irrigation System with ESP32 + Telegram Control

A solar-powered smart irrigation system built using ESP32, DS3231 RTC, and 18650 Li-ion battery backup.
It automates irrigation scheduling while enabling remote monitoring & control via Telegram Bot. Designed for sustainable smart farming, this project is energy-efficient and fully autonomous.

---

### ✨ Features
- ⏰ Automated scheduling – Irrigation runs daily from 10:00 AM to 3:00 PM.
- 🌍 Time synchronization – NTP + RTC integration ensures accurate timekeeping.
- 📱 Telegram bot commands:
  - /on → Turn relay ON
  - /off → Turn relay OFF
  - /status → Get current relay status
  - /time → Check system time
- ⚡ Failsafe design – Relay control works even if WiFi is unavailable.
- ☀️ Solar powered – Sustainable operation with LM2596 buck converter + TP4056 battery charging module.

---
### 🛠 Hardware Used
- **ESP32 board**
- **DS3231 RTC module**
- **Relay module (for irrigation pump)**
- **18650 3.7V 3200mAh Li-ion battery** + **TP4056 charging module**
- **LM2596 DC-DC buck converter**
- **6.6V Solar Panel**
- **Custom enclosure for outdoor deployment**

