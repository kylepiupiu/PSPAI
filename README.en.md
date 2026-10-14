# PSPAI — Native AI Chat on Sony PSP

[![Build & relay tests](https://github.com/kylepiupiu/PSPAI/actions/workflows/build-pspai.yml/badge.svg)](https://github.com/kylepiupiu/PSPAI/actions/workflows/build-pspai.yml)
[![Latest prerelease](https://img.shields.io/github/v/release/kylepiupiu/PSPAI?include_prereleases)](https://github.com/kylepiupiu/PSPAI/releases)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Give a classic PSP the ability to talk to modern AI.** PSPAI is an open-source **native PSP homebrew application** that sends a question over Wi-Fi to an ESP32-S3 bridge and a Python relay, then displays the cloud model's answer directly on the PSP screen. No browser or AI model on the PSP is required.

[简体中文](README.md#中文介绍) · [Installation guide (English)](docs/INSTALL_en.md) · [Download v0.3.1](https://github.com/kylepiupiu/PSPAI/releases/tag/v0.3.1)

> **Project status — experimental v0.3.1 (October 14, 2026):** A real PSP-3006 successfully sent a question and displayed a Qwen response through the native EBOOT. That is a verified milestone, **not** proof of full stability. Continuous conversations, exit behavior, error recovery, non-3006 PSP models and Chinese text rendering need further testing.

## How it works

```text
Sony PSP-3006
  Native PSPAI EBOOT.PBP / Wi-Fi client
        |  "wifi4psp" open AP / HTTP over TCP
        v
ESP32-S3 N16R8 (192.168.4.1:8088)
  Access point + TCP forwarding bridge
        |  2.4 GHz upstream Wi-Fi
        v
MacBook Air / Python Relay (port 8088)
  Allowlist + HTTP request handling
        |  Internet / HTTPS
        v
ModelScope API -> Qwen cloud model
        |
        +-- response travels back to the PSP screen
```

**Important:** the PSP and ESP32-S3 do not run Qwen or another large model. A computer must remain powered on, running the Python relay, with access to the internet and a working ModelScope API token. ESP32-S3 is a transport bridge, not a general internet router/NAT gateway.

## What is implemented

- Native PSP `EBOOT.PBP` compiled with PSPDEV/PSPSDK.
- PSP connection to a dedicated `wifi4psp` network.
- On-screen ASCII question composer, preset test question and answer pagination.
- ESP32-S3 N16R8 access point, TCP forwarding and browser-based **device monitoring** console.
- Python relay providing `GET /health` and `POST /ask`, with ModelScope/Qwen integration.
- Local relay tests and automatic PSP binary build using GitHub Actions.

**Limitations:** ASCII-only PSP text in this release; full CJK glyph rendering is not implemented. The text entry is a controller-operated character picker, not a hardware keyboard. The program has been user-tested on **PSP-3006**; other PSP models, network environments, multi-turn reliability and failure/exit recovery are not yet fully verified.

## Requirements

| Component | Tested setup / requirement |
| --- | --- |
| Handheld | PSP-3006 capable of running homebrew; memory stick and enabled WLAN |
| Bridge | ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM), Arduino-ESP32 3.3.12 used during testing |
| Host | MacBook Air with Python 3 and an upstream 2.4 GHz Wi-Fi network; other host OSs unverified |
| AI access | Internet access, your own ModelScope API token, and access to the configured Qwen model |
| Optional build | PSPDEV/PSPSDK to compile locally; GitHub Actions can build EBOOT automatically |

## Quick start

**1. Flash the ESP32-S3.** In `esp32/ESP32S3_Control_V1_0/`, copy `config.example.h` to **`config.h`**. Set your 2.4 GHz router SSID/password and `MAC_RELAY_HOST` to your relay computer's LAN IP. Use the `ESP32S3 Dev Module` board, 16 MB flash and OPI PSRAM settings in Arduino IDE, then upload the sketch. The ESP32 AP is **`wifi4psp`** at `192.168.4.1`.

**2. Run the relay on the computer.**

```sh
cd mac_relay
python3 -m venv .venv
./.venv/bin/python -m pip install certifi
export SSL_CERT_FILE="$(./.venv/bin/python -c 'import certifi; print(certifi.where())')"
# Replace the address with the ESP32 STA IP on your upstream network:
export PSPAI_ALLOWED_CLIENTS="127.0.0.1,192.168.1.10"
./.venv/bin/python start_relay.py
```

Enter your **own** ModelScope token in the hidden terminal prompt. Keep this terminal running. The relay's HTTP health check is `http://127.0.0.1:8088/health`. A successful health check does not guarantee cloud model quota or availability.

**3. Install the PSP client.** Download [PSPAI v0.3.1 (experimental)](https://github.com/kylepiupiu/PSPAI/releases/tag/v0.3.1), unzip it and copy the whole `PSPAI_V031` directory to the PSP memory stick:

```text
PSP/GAME/PSPAI_V031/EBOOT.PBP
```

On the PSP, create/select the `wifi4psp` infrastructure Wi-Fi profile, launch PSPAI from **Game → Memory Stick**, then select the Wi-Fi profile.

**4. Ask a question.**

| Control | Action |
| --- | --- |
| Triangle (△) | Send a preset test question |
| Cross (×) | Open the ASCII on-screen composer; select a character |
| D-pad | Navigate the composer, or page through answers |
| Square (□) | Delete a character in the composer |
| START | Submit the composed question |
| Circle (○) | Cancel or leave the current screen |

For a detailed walkthrough, see [INSTALL_en.md](docs/INSTALL_en.md) or the [Chinese guide](docs/INSTALL_zh-CN.md).

## Troubleshooting

- **The PSP cannot connect:** verify its physical WLAN switch and its saved `wifi4psp` infrastructure profile.
- **Connection refused / timeout:** check the relay computer's current LAN IP, ESP32 upstream Wi-Fi connection and port `8088`.
- **HTTP 403:** include the ESP32 **upstream STA IP** (not the PSP's `192.168.4.x` IP) in `PSPAI_ALLOWED_CLIENTS`.
- **HTTP 429/502:** investigate token/account access, ModelScope quota, model availability and HTTPS connectivity.
- **PSP freezes:** stop repeated tests and collect `ms0:/PSPAI_NET.LOG` after a safe reboot. This is experimental firmware.

## Security and privacy

The `wifi4psp` access point intentionally has **no password**. PSP ↔ ESP32 ↔ relay traffic uses **unencrypted HTTP/TCP**, so nearby users could potentially connect or observe traffic. The relay forwards your prompts to ModelScope. **Do not use this on public networks or for private, sensitive or confidential material.** Never commit API tokens, passwords, your private ESP32 `config.h` or device admin keys.

See [Security notes (English)](docs/SECURITY_en.md) and [安全说明](docs/SECURITY.md).

## Development and contributions

- `psp/` — native PSP source code and Makefile.
- `esp32/` — ESP32-S3 Arduino firmware and **configuration template**.
- `mac_relay/` — Python/ModelScope relay and offline tests.
- `.github/workflows/` — CI for relay tests and PSPDEV EBOOT compilation.
- `docs/` — installation, safety and [first native AI chat milestone](docs/MILESTONE_2026-10-14.md).

Run local relay tests with:

```sh
cd mac_relay
python3 -m unittest -v test_local test_v03
```

See [CONTRIBUTING.md](CONTRIBUTING.md) to report issues or submit changes. Source code is available under the [MIT License](LICENSE).

**Disclaimer:** This is an unofficial community homebrew project, not affiliated with, sponsored by or endorsed by Sony, Qwen or ModelScope. Respective names and trademarks belong to their owners.
