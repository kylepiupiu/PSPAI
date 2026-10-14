# PSPAI — Native AI Chat for Sony PSP | PSP 原生 AI 助手

[**English documentation**](README.en.md) · [**English installation guide**](docs/INSTALL_en.md) · [**中文说明**](#中文介绍)

[![Build & relay tests](https://github.com/kylepiupiu/PSPAI/actions/workflows/build-pspai.yml/badge.svg)](https://github.com/kylepiupiu/PSPAI/actions/workflows/build-pspai.yml)
[![GitHub release](https://img.shields.io/github/v/release/kylepiupiu/PSPAI?include_prereleases)](https://github.com/kylepiupiu/PSPAI/releases)
[![MIT license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**Bring conversational AI to a classic Sony PSP — with a native homebrew app, not a web browser.** PSPAI sends questions from the PSP through an ESP32-S3 Wi-Fi/TCP bridge to a Python relay on a computer, which calls Qwen through the ModelScope API and returns the answer to the handheld.

**Status:** v0.3.1 experimental prerelease. One real PSP-3006 native question/answer session has been confirmed; broader hardware compatibility, multi-turn reliability, and Chinese font rendering are not yet validated. The PSP and ESP32 do **not** run the AI model locally.

**Get started:** [Download PSP EBOOT installer](https://github.com/kylepiupiu/PSPAI/releases/tag/v0.3.1) · [Setup in English](docs/INSTALL_en.md) · [Report an issue](https://github.com/kylepiupiu/PSPAI/issues) · [Contribute](CONTRIBUTING.md)

**Security notice:** `wifi4psp` is an intentionally open access point and the local PSP-to-relay connection is unencrypted. Use an isolated, trusted test network. No API token is distributed with the project.

---

## 中文介绍

**让 PSP 3006 原生程序通过 ESP32-S3 Wi-Fi 网关与千问模型对话。** 不是在 PSP 上打开网页，也不是 PSP 本地运行大模型。

**版本：v0.3.1 Stability（2026-10-14）** · **实机里程碑：一次真实 AI 问答成功** · **尚非完整稳定版**

> Unofficial community homebrew project. Not affiliated with or endorsed by Sony.

## 系统结构

```text
PSP 3006（PSPAI 原生 EBOOT.PBP）
   | Wi-Fi: wifi4psp / TCP 192.168.4.1:8088
   v
ESP32-S3 N16R8（开放热点 + TCP bridge）
   | 家庭/实验室 2.4GHz 局域网
   v
MacBook Air（Python Relay，监听 8088）
   | HTTPS / ModelScope API
   v
Qwen（千问云端模型）
```

**重要：必须有一台持续运行 Relay 且可访问互联网的电脑。ESP32 只转发 TCP 数据；PSP 不直接连接云端模型。**

## 实测进度

- ✅ PSP 3006 原生客户端编译（PSPDEV GitHub Actions 构建成功）
- ✅ PSP 通过 ESP32-S3 与 Mac Relay 建立连接
- ✅ 2026-10-14 PSP 实机发送问题、显示模型回答（用户反馈）
- ✅ Mac Relay 接口离线测试通过、iPhone 经 ESP 网关真实 AI 问答成功
- ⚠️ v0.3 首次实机曾在请求后卡死；v0.3.1 加入接收超时与退出检查，成功一次真实问答
- ⬜ 多轮稳定性、长文本翻页、HOME 退出、网络错误恢复与中文字体尚待系统验收

## 如何安装

请完整阅读 [中文安装说明](docs/INSTALL_zh-CN.md)。最简流程：

1. **ESP32：** 使用 Arduino IDE 3.x 系列 ESP32 支持包，烧录 `esp32/ESP32S3_Control_V1_0/`。把 `config.example.h` 复制为本机 `config.h`，再修改家庭 Wi-Fi 和 Mac IP（`config.h` 已忽略提交）。
2. **Mac：** 在 `mac_relay/` 创建 Python 虚拟环境，安装 `certifi`，运行 `start_relay.py`，使用你自己的 ModelScope API Token。
3. **PSP：** 在 [GitHub Releases](https://github.com/kylepiupiu/PSPAI/releases) 下载 PSPAI v0.3.1 安装包，将其中的 `PSPAI_V031/` 文件夹复制到 PSP 记忆棒的 `PSP/GAME/`。需要能运行自制软件的 PSP 系统。
4. PSP 网络设定中创建并选择 `wifi4psp`，启动 PSPAI，选网络配置后通过 **△ 发送快速测试问题**；**×** 进入英文字符输入界面。

## 文件结构

| 路径 | 用途 |
| --- | --- |
| `psp/main.c`, `psp/Makefile` | 原生 PSP 客户端源码 |
| [Releases](https://github.com/kylepiupiu/PSPAI/releases) | v0.3.1 的 EBOOT.PBP 和安装 ZIP |
| `mac_relay/` | Python/ModelScope 网关服务和测试 |
| `esp32/` | ESP32-S3 固件和可编辑网络配置 |
| `.github/workflows/` | GitHub Actions 使用 PSPDEV 自动构建 |
| `docs/` | 安装、安全说明和开发里程碑 |

## 开放共享

软件以 [MIT License](LICENSE) 开源，欢迎学习、修改与分享。模型服务条款、平台配额及硬件风险由使用者自行核对。**请勿提交个人 ModelScope Token、家庭 Wi-Fi 密码、企业内部资料和设备管理员密钥。**

目前只在上述特定 PSP/ESP32/Mac 环境完成一次真实问答；其他硬件/系统暂未测试。
