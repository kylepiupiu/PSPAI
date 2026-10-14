# PSPAI — Security and privacy notes

PSPAI v0.3.1 is an **experimental local-network prototype**, not a security-audited or production-ready service.

- The PSP access point `wifi4psp` is intentionally **unencrypted/open** so older PSP Wi-Fi hardware can connect. Nearby devices may be able to join it.
- The PSP ↔ ESP32-S3 ↔ relay connection uses plaintext TCP/HTTP. Do **not** send credentials, sensitive prompts, private files, company data, or personal records through it.
- The Python relay forwards submitted questions to the **ModelScope cloud API**. The model does not run locally; consult the provider's terms, privacy policy, access permissions and quota.
- The relay's IP allowlist and basic request limits are **not a replacement for network isolation or authentication**.
- Provide your ModelScope token only to your local relay process. Never include API tokens, Wi-Fi passwords, generated admin keys, private `config.h`, or raw sensitive logs in issues or commits.
- Use a trusted laboratory/home test network, keep the relay host behind an appropriate firewall, and stop the relay when not in use.
- The project has limited real-hardware coverage; unexpected hangs or network failures are possible.

When reporting a security concern, **do not post secrets or exploit details in a public issue**. Contact the repository owner privately using a suitable GitHub contact or security-reporting channel if one is available.

[中文安全说明](SECURITY.md) · [Installation guide](INSTALL_en.md)
