# PSPAI v0.3.1 安装与联网说明（当前以 Mac 测试环境为准）

## 1. 必要设备

- 能运行 Homebrew 的 PSP（已实测 PSP 3006），并有记忆棒和 USB 复制方式；PSP 必须将 WLAN 开关打开。
- ESP32-S3 N16R8（16MB Flash / 8MB OPI PSRAM）；已实测的 Arduino ESP32 包版本为 3.3.12。
- MacBook Air（负责运行 AI Relay）与互联网连接；Python 3 环境。
- 一个自己申请的 ModelScope（魔搭）API Token；请勿把 Token 提交到公开仓库。

## 2. ESP32 固件

Arduino IDE 选择 `ESP32S3 Dev Module`；Flash 16MB、PSRAM OPI、Upload 115200，按开发板配置正确的分区设置（已测试 `16M Flash (2MB APP/12.5MB FATFS)`）。

先将 `esp32/ESP32S3_Control_V1_0/config.example.h` **复制**为同目录的 `config.h`，再编辑本机 `config.h`：

```c
#define UPSTREAM_SSID "YOUR_2_4GHZ_WIFI"
#define UPSTREAM_PASSWORD "YOUR_PRIVATE_PASSWORD"
#define MAC_RELAY_HOST "192.168.1.2"  // 示例！请改成你的 Mac 实际局域网 IP
```

模板的 Wi-Fi 密码为空，个人 `config.h` 已在 `.gitignore` 中排除，但**仍请不要公开带密码的配置文件**。编译并烧录 `ESP32S3_Control_V1_0.ino`。ESP32 将提供无密码的 `wifi4psp`，地址 `192.168.4.1`，并通过本地 Wi-Fi 把端口 8088 转给 Mac。

## 3. Mac Relay

```bash
cd mac_relay
python3 -m venv .venv
./.venv/bin/python -m pip install certifi
export SSL_CERT_FILE="$(./.venv/bin/python -c 'import certifi; print(certifi.where())')"
export PSPAI_ALLOWED_CLIENTS="127.0.0.1,192.168.1.10"
./.venv/bin/python start_relay.py
```

将 `192.168.1.10` **替换为 ESP32 在家庭 Wi-Fi 上的实际 STA IP**（网页控制台或路由器中可查询）。在程序提示时手动输入自己的 ModelScope Token；输入不会显示在终端。Mac Relay 必须持续保持运行。

新终端可验证：

```bash
curl -i http://127.0.0.1:8088/health
```

上述命令只检测 Relay 服务正常，不代表模型 API 额度可用；模型真实调用可在有额度时执行 `./.venv/bin/python test_ask.py`。

## 4. PSP 安装

在 [GitHub Releases](https://github.com/kylepiupiu/PSPAI/releases) 下载 PSPAI v0.3.1 PSP 安装压缩包并解压，将其中的 `PSPAI_V031` 整个文件夹复制到记忆棒 `PSP/GAME/`，最终路径为：

```text
PSP/GAME/PSPAI_V031/EBOOT.PBP
```

PSP「设定 → 网络设定 → 基础设施模式」建立/保存 `wifi4psp`，在游戏 → Memory Stick 中启动 PSPAI。选择这个网络配置并连接。按 **△** 发送测试问题；按 **×** 进入英文字符选择器；方向键选字符，× 输入，□ 删除，START 发送，○ 取消；主界面上/下翻页，○ 退出。

如果 PSP 需要强制关机：先等待 20 秒以上；仍卡死才长按电源约 10 秒。不要在写入记忆棒时直接断电。

## 5. 常见失败

- `Network ...`：核对 PSP 的 WLAN 开关和已保存 `wifi4psp` 配置。
- `Connect failed`：核对 ESP32 的热点与 TCP 8088 桥接状态、Mac Relay 是否在运行。
- `HTTP 403`：设置 `PSPAI_ALLOWED_CLIENTS`，加入 ESP32 STA 地址，而不是 PSP 192.168.4.x 地址（ESP32 桥接连接的来源 IP 是 ESP32）。
- `HTTP 502/429`：检查 ModelScope Token、模型 ID、可用配额、联网/TLS。
- 请求没响应：PSP 根目录 `ms0:/PSPAI_NET.LOG` 包含分阶段诊断日志，发问题报告前请删去个人信息与局域网敏感数据。
- Mac Wi-Fi 未连接/切网：Mac 局域网 IP 变化，需要更新 ESP32 固件里的 `MAC_RELAY_HOST` 并重新烧录；当前没有自动发现。

**限制：** V0.3.1 以英文 ASCII 输入与回答为主，中文字符显示尚不支持；单次实机成功不等于稳定性完全通过。
