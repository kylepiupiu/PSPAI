# PSPAI · Native PSP AI Client

PSPAI connects a PSP 3006 homebrew EBOOT to a cloud Qwen model through an ESP32-S3 Wi-Fi/TCP bridge and a computer running the ModelScope relay. The model does **not** run on the PSP or ESP32.

**v0.3.1 Stability / 2026-10-14.** User-confirmed successful native PSP AI question and answer on real hardware; full regression, multilingual glyph support, and multiple-network setups are not yet verified.

Hardware: homebrew-capable PSP 3006, ESP32-S3 N16R8, Mac/other host running Python Relay, 2.4GHz Wi-Fi, internet access, a ModelScope API token.

Setup: copy `esp32/ESP32S3_Control_V1_0/config.example.h` to `config.h`, then edit your local `config.h`, upload Arduino firmware, configure and start `mac_relay/start_relay.py`, then download the v0.3.1 installer from [GitHub Releases](https://github.com/kylepiupiu/PSPAI/releases) and copy `PSPAI_V031/` to your PSP memory stick `PSP/GAME/`.

`PSPAI_ALLOWED_CLIENTS` controls relay IP access (include ESP32 upstream IP). PSP default bridge is `192.168.4.1:8088`.

- △: send a preset test question.
- ×: open on-screen ASCII character picker.
- D-pad: choose characters / page answers.
- □: delete character while composing.
- START: send composed question.
- ○: cancel/exit.

**Security:** the PSP hotspot is deliberately open, communication PSP↔ESP32↔relay uses plaintext TCP/HTTP, and prompts are sent to ModelScope. Restrict use to a trusted test network. No API keys are bundled.

See [`docs/INSTALL_zh-CN.md`](docs/INSTALL_zh-CN.md) for the currently maintained detailed setup, and [`docs/MILESTONE_2026-10-14.md`](docs/MILESTONE_2026-10-14.md) for verified history. MIT licensed; unofficial homebrew project, not affiliated with Sony.
