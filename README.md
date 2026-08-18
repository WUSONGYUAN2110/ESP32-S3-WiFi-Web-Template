# ESP32-S3 Wi-Fi Web Template

English | [中文](README.zh-CN.md)

> A local Wi-Fi web-interaction template for `ESP32-S3-WROOM-1-N16R8` and ESP-IDF `v6.0.2`, featuring captive-portal provisioning, a web console, REST APIs, and real-time WebSocket status updates.

## Project Overview

This template uses a temporary setup access point for web-based Wi-Fi provisioning. After connecting to a phone hotspot or router, the device exposes a web console, REST API, and real-time WebSocket status on the local network.

The example drives the onboard single WS2812B through GPIO48 to demonstrate web control of power, color, and brightness. See [`doc/HARDWARE.md`](doc/HARDWARE.md) for GPIO, power, Flash, and PSRAM constraints; read it before changing hardware-related code.

## Fixed Environment

- Chip: ESP32-S3-WROOM-1-N16R8
- Flash: 16 MB QSPI
- New projects should use the full onboard 16 MB Flash and configure partitions for the actual feature set.
- PSRAM: 8 MB Octal
- ESP-IDF: `v6.0.2`
- Build target: `esp32s3`
- ESP-IDF path: resolved by `tools/idf.ps1` for the local machine
- Tool path: resolved by `tools/idf.ps1` for the local machine

AI tasks, automation, and ordinary PowerShell commands must invoke ESP-IDF through the project-local `tools/idf.ps1`. It prefers the environment specified by `ESP_IDF_POWERSHELL_PROFILE` or `IDF_TOOLS_PATH` and switches to the project root; do not depend on VS Code terminal state or a manually activated shell.

```powershell
.\tools\idf.ps1 --version                 # Should print ESP-IDF v6.0.2
.\tools\idf.ps1 set-target esp32s3
.\tools\idf.ps1 menuconfig
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COMx flash monitor     # Replace COMx with the CH340 port; press Ctrl+] to exit
```

Running `.\tools\idf.ps1` without arguments builds the project by default.

Project settings are under `Component config > WiFi Web Template`:

- The default mDNS hostname is `esp32s3-web`.
- The default setup-AP password is `esp32setup` and must be 8–63 characters.
- The setup AP opens after 30 seconds if a saved network cannot connect.
- New-network verification times out after 20 seconds by default.

## Project Structure

- `components/main/app_main.c`: application entry point; starts NVS, business components, and callbacks.
- `components/wifi_manager/`: Wi-Fi STA/AP state machine, reconnection, mDNS, and scanning; `wifi_credentials.c/.h` privately handles NVS credential transactions.
- `components/web_ui/web_server.c`: HTTP lifecycle, embedded resources, captive-portal routes, and WebSocket broadcasts.
- `components/web_ui/web_api.c`: REST handlers, input validation, and unified status JSON.
- `components/web_ui/web/`: Chinese provisioning pages and business-console resources.
- `components/ws2812_led/`: public GPIO48 WS2812B control API and private RMT encoder.
- `components/provision_button/`: long-press detection for the GPIO0 BOOT button, triggering reprovisioning through a callback.
- `components/dns_server/`: DNS redirection component based on the official ESP-IDF captive-portal example.
- `components/main/Kconfig.projbuild`: hostname, setup-AP password, and timeout parameters.
- `sdkconfig.defaults`: ESP32-S3 Flash, Octal PSRAM, and WebSocket defaults.

All project-owned code is under `components/`; the application entry component is `components/main/`, and there is no separate root-level `main/` directory. C implementations and private headers stay in each component root; only stable public headers for other components belong in `include/`. Web resources remain under `web/`. Do not move private headers into public `include/`. Component dependencies must be declared explicitly through `PRIV_REQUIRES`. Managed dependencies are pinned by each component's `idf_component.yml` and the root `dependencies.lock`, currently including Espressif cJSON and mDNS. Do not edit `managed_components/` directly.

## Provisioning and Access Flow

1. Connect a phone to `ESP32S3-Setup-XXXX`; the default password is `esp32setup`.
2. The provisioning page usually opens automatically. If it does not, visit <http://192.168.4.1/>.
3. Select or enter a Wi-Fi network and submit its password. The device tests the candidate in RAM and saves it only after obtaining an IP address.
4. After the target IP is shown, the setup AP closes after about five seconds.
5. Switch the phone to the target network and visit the IP from the serial log or <http://esp32s3-web.local/>.

When the phone itself is the hotspot, some systems cannot resolve mDNS for hotspot clients. Use the phone's hotspot-client list or the serial log to find the ESP32 IP.

While online, reprovisioning can be started from the business page. If the web UI is unavailable, hold BOOT for about five seconds while the firmware is running. Do not hold BOOT during power-on or reset, or the device will enter download mode.

## HTTP and WebSocket APIs

| Method | Path | Purpose |
| --- | --- | --- |
| `GET` | `/api/wifi/scan` | Scan nearby networks |
| `POST` | `/api/wifi/connect` | Submit `{"ssid":"...","password":"..."}` and verify asynchronously |
| `GET` | `/api/wifi/state` | Query provisioning, verification, and network state |
| `POST` | `/api/wifi/reprovision` | Clear credentials and enter provisioning mode |
| `GET` | `/api/status` | Get complete device status |
| `PUT` | `/api/led` | Set `{"on":true,"color":"#2A7CFF","brightness":60}` |
| `PUT` | `/api/message` | Set `{"message":"text"}`, up to 96 bytes |
| `GET` | `/ws` | Upgrade to WebSocket and receive status updates |

When changing status JSON, also check the business-page fields, `/api/status`, `/api/wifi/state`, and WebSocket consumers to prevent interface drift.

## Development and Acceptance Requirements

Run at least the following after every change:

```powershell
.\tools\idf.ps1 build
```

For web-script changes, additionally run JavaScript syntax checks and verify that `/`, `/app.css`, `/app.js`, and `/portal.js` load correctly.

For network-state-machine changes, real-device testing must cover at least:

- First boot without credentials, setup AP, and captive-portal access
- Open, WPA2, hidden-SSID, and incorrect-password networks
- Failed candidate credentials not replacing a usable saved network
- Automatic fallback to provisioning after the target AP disappears, followed by reconnection after recovery
- NVS persistence after successful provisioning and power-cycle restart
- Reprovisioning from both the web button and a long BOOT press
- Phone-hotspot mode and phone/device-on-the-same-router mode
- Real-time synchronization of lights, text, and status in multiple browsers
- HTTP 4xx responses for malformed JSON, invalid colors, out-of-range brightness, and oversized text

Before flashing, confirm that the serial port belongs to the CH340 development board; do not select a Bluetooth virtual COM port.

## Security Notes

- Wi-Fi credentials are stored in ordinary NVS and are not encrypted at rest unless Flash encryption is enabled.
- The setup AP uses WPA2, but business APIs are unauthenticated plaintext HTTP.
- The first version does not include HTTPS, cloud services, OTA, BLE provisioning, or static IP. Do not add these features without explicitly expanding the requirements.
