# PSPAI v0.3.1 — Installation and network setup

**Tested configuration:** Sony PSP-3006, ESP32-S3 N16R8, MacBook Air, Python Relay, ModelScope/Qwen. Other environments have not been validated. The client is experimental and currently supports ASCII text on the PSP.

## 1. Prepare the hardware

You need:
- A homebrew-capable Sony PSP, a memory stick, USB file transfer and the PSP WLAN switch enabled.
- An ESP32-S3 N16R8 board (16 MB flash, 8 MB OPI PSRAM) and a USB cable for flashing.
- A computer running Python 3 that can reach the internet and your ESP32-S3 on the same upstream network.
- Your own ModelScope API token, with access to the configured model.

## 2. Flash the ESP32-S3 bridge

1. Install Arduino IDE and the Espressif Arduino-ESP32 board package. Version **3.3.12** was used in the tested setup.
2. In `esp32/ESP32S3_Control_V1_0/`, copy `config.example.h` to `config.h`. Never commit this private file.
3. Edit the following values for your environment:

```c
#define UPSTREAM_SSID "YOUR_2_4_GHZ_SSID"
#define UPSTREAM_PASSWORD "YOUR_PRIVATE_WIFI_PASSWORD"
#define MAC_RELAY_HOST "192.168.1.2"   // Example only; use the host's actual IP
```

4. Choose **ESP32S3 Dev Module**, **16MB flash**, **OPI PSRAM**, upload speed **115200**, and an appropriate 16 MB partition layout; the tested layout was `16M Flash (2MB APP/12.5MB FATFS)`.
5. Compile/upload `ESP32S3_Control_V1_0.ino`. The AP `wifi4psp` should appear, deliberately **without a password**. ESP32 AP address is `192.168.4.1`; its TCP bridge listens on port `8088`.

The ESP32 is **not an internet router** for the PSP. It forwards the PSP TCP connection to the relay computer over upstream Wi-Fi.

## 3. Start the computer relay (tested on macOS)

```sh
cd mac_relay
python3 -m venv .venv
./.venv/bin/python -m pip install certifi
export SSL_CERT_FILE="$(./.venv/bin/python -c 'import certifi; print(certifi.where())')"
export PSPAI_ALLOWED_CLIENTS="127.0.0.1,192.168.1.10"
./.venv/bin/python start_relay.py
```

**Replace `192.168.1.10` with your ESP32's actual STA IP** on the upstream router. The ESP32 bridge connects to the relay using that source IP. The relay asks for your ModelScope token in a hidden input prompt. Leave it running while using PSPAI.

Verify the HTTP relay in a second terminal:

```sh
curl -i http://127.0.0.1:8088/health
```

A `200 OK` health response means the HTTP relay responds; it does not verify your token, model availability or available quota. When you are ready for an actual request, `./.venv/bin/python test_ask.py` exercises the model endpoint.

## 4. Install and launch the native PSP client

1. Download the **experimental v0.3.1 ZIP** from [GitHub Releases](https://github.com/kylepiupiu/PSPAI/releases/tag/v0.3.1).
2. Extract it and copy the whole `PSPAI_V031` directory to the memory stick.
3. Verify that the file path is `PSP/GAME/PSPAI_V031/EBOOT.PBP` (not a double-nested folder).
4. On the PSP, open **Settings → Network Settings → Infrastructure Mode** and create/select `wifi4psp`.
5. Launch PSPAI from **Game → Memory Stick**, select the saved Wi-Fi profile, and wait for connection.
6. Press **Triangle** for a preset AI test. Press **Cross** to compose an ASCII question with the on-screen character picker. Use the D-pad and Cross to enter characters, Square to delete, START to send, and Circle to cancel/exit.

## 5. Diagnose common problems

| Symptom | First check |
| --- | --- |
| No Wi-Fi connection | PSP WLAN switch and saved `wifi4psp` infrastructure profile |
| `Connect failed` | ESP32 AP/STA status, relay host LAN IP, relay listening on TCP 8088 |
| HTTP 403 | `PSPAI_ALLOWED_CLIENTS` must include ESP32 STA IP |
| HTTP 429 | ModelScope quota/rate limits or relay's local request limit |
| HTTP 502 | Token, account permissions, model ID, HTTPS/TLS or network access |
| Question hangs | After a safe reboot, inspect `ms0:/PSPAI_NET.LOG` for the last completed stage |

If PSPAI locks up, avoid repeated tests. Wait before holding the PSP power switch to force shutdown; never interrupt an active memory-stick write. Remove private Wi-Fi or IP details before sharing logs.

## 6. Security

The PSP AP is **open**, and PSP-to-relay traffic is **not encrypted**. Keep it on an isolated/trusted testing network. Do not send passwords, company data, private documents or other sensitive material. Requests are processed by a **cloud** AI provider, not an offline model.

For design background and remaining limitations, see [README.en.md](../README.en.md) and [SECURITY_en.md](SECURITY_en.md).
