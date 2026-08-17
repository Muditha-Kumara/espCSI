# ESP-CSI Human Activity Recognition Data Collection System

Two-device ESP-IDF v6.0.1 system for collecting Wi-Fi Channel State Information (CSI) data to train machine learning models for human activity recognition.

## Architecture

- **Receiver** (ESP32-S3, 16 MB flash): Connects to home Wi-Fi, hosts web UI at `http://espcsi.local`, captures CSI data, stores CSV files on FATFS (~11.9 MB storage)
- **Broadcaster** (ESP-WROOM-32, 4 MB flash): Sends ESP-NOW frames at configurable rate (default 100 Hz) as CSI source

## Features

- Web-based session control (label, duration, rate, channel)
- Auto-pairing via ESP-NOW announce
- CSV data export (esp-csi tool compatible format)
- OTA updates from local Python server
- mDNS hostname (`espcsi.local`)
- Status LEDs on both devices
- Live CSI amplitude preview (when on AP channel)

## Quick Start

### 1. Build and Flash

```bash
# Activate ESP-IDF 6.0.1
. ~/.espressif/v6.0.1/esp-idf/export.sh

# Build receiver
cd receiver
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Build broadcaster (separate terminal)
cd broadcaster
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB1 flash monitor
```

### 2. Configure Wi-Fi

Edit `sdkconfig` or use `idf.py menuconfig`:
- Receiver: `Component config → Example Configuration → WiFi SSID/Password`
- Broadcaster: same menu

Or edit `main/Kconfig.projbuild` defaults before building.

### 3. Start OTA Server

```bash
cd tools
python3 ota_server.py --port 8070
```

Both devices will check for updates every 30 minutes when idle.

### 4. Use Web UI

Browse to `http://espcsi.local` (or the receiver's IP address).

- **Start Session**: Enter label, duration (seconds), rate (Hz), channel
- **Sessions**: Download CSV files, delete old sessions
- **Live Preview**: View CSI amplitude in real-time (only when collecting on AP channel)
- **OTA**: Check for updates, trigger manual update

## Session Flow

1. **Idle**: Both devices on home Wi-Fi. Broadcaster announces via ESP-NOW every 5s.
2. **Start**: Web UI sends START command → receiver switches to work channel → enables CSI capture → streams CSV to storage
3. **Collect**: Broadcaster sends ESP-NOW frames at requested rate; receiver records CSI
4. **Stop**: Auto after duration (or BOOT button early stop) → flush CSV → reconnect Wi-Fi → send STOP
5. **OTA**: Idle-only periodic check (30 min) + manual web buttons

## CSV Format

```
CSI_DATA,<id>,<mac>,<rssi>,<rate>,<noise_floor>,<channel>,<timestamp>,<sig_len>,<rx_format>,<len>,<first_word>,"[i,q,...]"
```

Compatible with Espressif `esp-csi` tooling.

## Project Structure

```
espCSI/
├── common/espnow_proto/    # Shared ESP-NOW protocol definitions
├── receiver/               # ESP32-S3 CSI collector + web UI
├── broadcaster/            # ESP-WROOM-32 ESP-NOW beacon TX
└── tools/ota_server.py     # Local OTA server
```

## Hardware

- **Receiver**: ESP32-S3-DevKitC-1 (16 MB flash) or similar
- **Broadcaster**: ESP-WROOM-32 DevKit (4 MB flash) or similar
- **LEDs**: GPIO 2 (built-in) or external LED on configurable GPIO
- **BOOT button**: GPIO 0 for early session stop

## License

Apache-2.0
