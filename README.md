<div align="center">

```
███████╗██╗     ██╗██████╗ ██████╗ ███████╗██████╗
██╔════╝██║     ██║██╔══██╗██╔══██╗██╔════╝██╔══██╗
█████╗  ██║     ██║██████╔╝██████╔╝█████╗  ██████╔╝
██╔══╝  ██║     ██║██╔═══╝ ██╔═══╝ ██╔══╝  ██╔══██╗
██║     ███████╗██║██║     ██║     ███████╗██║  ██║
╚═╝     ╚══════╝╚═╝╚═╝     ╚═╝     ╚══════╝╚═╝  ╚═╝
███████╗████████╗ ██████╗  ██████╗██╗  ██╗███████╗
██╔════╝╚══██╔══╝██╔═══██╗██╔════╝██║ ██╔╝██╔════╝
███████╗   ██║   ██║   ██║██║     █████╔╝ ███████╗
╚════██║   ██║   ██║   ██║██║     ██╔═██╗ ╚════██║
███████║   ██║   ╚██████╔╝╚██████╗██║  ██╗███████║
╚══════╝   ╚═╝    ╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝
```

**Real-time candlestick charts on your Flipper Zero**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Flipper%20Zero-orange)](https://flipperzero.one)
[![Firmware](https://img.shields.io/badge/Firmware-Momentum-purple)](https://momentum-fw.dev)
[![API](https://img.shields.io/badge/Data-Yahoo%20Finance-blue)](https://finance.yahoo.com)
[![Built with](https://img.shields.io/badge/Built%20with-C-lightgrey)](https://en.wikipedia.org/wiki/C_(programming_language))

</div>

---

## 📈 What is this?

**Flipper Stocks** turns your Flipper Zero into a pocket trading terminal. Pull up live OHLCV candlestick charts, switch between timeframes, zoom in and out, and browse any stock or commodity — all from a device that fits in your pocket.

No API key. No subscription. No PC required once it's flashed.

---

## ✨ Features

- 🕯️ **Candlestick charts** with volume bars — rendered pixel-perfect on the 128×64 monochrome display
- 📡 **Live data** via Yahoo Finance (no API key needed)
- ⏱️ **6 timeframes** — 1m, 5m, 15m, 1h, 1D, 1W
- 🔍 **Zoom control** — squeeze between 4 and 8 candles on screen
- 📋 **OHLC detail mode** — navigate individual candles and inspect open/high/low/close
- 📊 **8 instruments** — stocks, silver futures, forex
- 🎛️ **Stock selector menu** — switch instruments without leaving the app
- 📶 **WiFi powered** — via the Flipper Zero WiFi Dev Board + FlipperHTTP

---

## 🎮 Controls

| Button | Action |
|--------|--------|
| `↑` / `↓` | Cycle timeframes (1m → 5m → 15m → 1h → 1D → 1W) |
| `←` / `→` | Zoom in / out (4–8 candles) |
| `OK` (short) | Toggle OHLC detail mode |
| `←` / `→` in OHLC | Navigate between candles |
| `OK` (long press) | Open stock selector menu |
| `Back` | Exit OHLC mode / exit app |

---

## 🛠️ Requirements

| Component | Details |
|-----------|---------|
| **Flipper Zero** | Any hardware revision |
| **Firmware** | [Momentum](https://momentum-fw.dev) (recommended) or Official |
| **WiFi Dev Board** | Flipper Zero WiFi Developer Board |
| **FlipperHTTP** | Flashed onto the WiFi Dev Board |
| **WiFi** | 2.4GHz network |

---

## ⚡ Installation

### 1. Flash FlipperHTTP to your WiFi Dev Board

Follow the [FlipperHTTP installation guide](https://github.com/jblanked/FlipperHTTP).

On your Flipper: **Apps → GPIO → ESP Flasher → FlipperHTTP**

### 2. Clone this repo

```bash
git clone https://github.com/YOUR_USERNAME/flipper-stocks.git
cd flipper-stocks
```

### 3. Set up your WiFi credentials

```bash
cp credentials.h.example credentials.h
```

Edit `credentials.h`:

```c
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASS "YourPassword"
```

> ⚠️ `credentials.h` is in `.gitignore` — your password will never be committed.

### 4. Install ufbt

```bash
pip install ufbt
ufbt update
```

### 5. Build and flash

Plug in your Flipper and run:

```bash
ufbt launch
```

The app appears under **Apps → Tools → Flipper Stocks**.

---

## 📊 Supported Instruments

| Symbol | Name |
|--------|------|
| `AAPL` | Apple Inc. |
| `SI=F` | Silver Futures |
| `NVDA` | NVIDIA Corporation |
| `MSFT` | Microsoft Corporation |
| `AMZN` | Amazon.com Inc. |
| `TSLA` | Tesla Inc. |
| `GOOGL` | Alphabet Inc. |
| `AMD` | Advanced Micro Devices |

> More instruments can be added by editing `TICKERS[]` in `flipper_stocks.c`.

---

## 🏗️ Project Structure

```
flipper-stocks/
├── flipper_stocks.c       # Main app — all logic in one file
├── application.fam        # ufbt build config
├── credentials.h          # Your WiFi credentials (gitignored)
├── credentials.h.example  # Template for credentials
├── flipper_http/          # FlipperHTTP UART library
├── jsmn/                  # Minimal JSON header builder
└── README.md
```

---

## 🔧 How it works

```
Yahoo Finance API
      │
      ▼ HTTPS
WiFi Dev Board (ESP32 + FlipperHTTP)
      │
      ▼ UART
Flipper Zero
      │
      ▼
flipper_stocks.c
  ├── Connects WiFi via FlipperHTTP
  ├── Fetches OHLCV JSON from Yahoo Finance
  ├── Parses open/high/low/close/volume arrays
  └── Renders candlestick chart on 128×64 display
```

Data is fetched fresh on every ticker/timeframe change and cached to the SD card between refreshes.

---

## 🤝 Contributing

Pull requests welcome. To add a new instrument, just add its Yahoo Finance ticker to `TICKERS[]` in `flipper_stocks.c` and update `N_TICKERS`.

Yahoo Finance tickers:
- Stocks: `AAPL`, `TSLA`, `NVDA`, etc.
- Commodities: `GC=F` (Gold), `SI=F` (Silver), `CL=F` (Oil)
- Forex: `EURUSD=X`, `GBPUSD=X`
- Crypto: `BTC-USD`, `ETH-USD`

---

## 📄 License

MIT — do whatever you want with it.

---

<div align="center">

Built for the Flipper Zero community 🐬

*Data provided by Yahoo Finance. Not financial advice.*

</div>
