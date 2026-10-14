# Contributing to PSPAI

PSPAI is an experimental community homebrew project. Contributions are welcome, especially reproducible fixes and real PSP hardware tests.

## Before opening an issue

1. Check [open issues](https://github.com/kylepiupiu/PSPAI/issues) and the [English setup guide](docs/INSTALL_en.md).
2. State your exact hardware (PSP model and firmware, ESP32 variant), operating system, PSPAI version and steps to reproduce.
3. Describe **expected vs. actual** results and whether the PSP could connect to `wifi4psp`, reach the relay and display any response.
4. If available, attach a **sanitized** excerpt from `ms0:/PSPAI_NET.LOG` or relay output. Remove tokens, passwords, IPs you do not wish to disclose, and private questions.

## Pull requests

- Open an issue for significant behavior changes before implementing them.
- Keep PRs focused, explain the reason for each change, and distinguish **compiled**, **emulator-tested** and **real-device-tested** behavior.
- Never claim PSP hardware compatibility based only on a successful CI build.
- Run the relay tests before submitting:

```sh
cd mac_relay
python3 -m unittest -v test_local test_v03
```

- PSP code must compile with PSPDEV/PSPSDK. The workflow at `.github/workflows/build-pspai.yml` builds the native `EBOOT.PBP`.
- If network code changes, document timeouts, cleanup paths, and how failures appear on the PSP.
- Avoid adding dependencies or external assets without clear license and maintenance information.

## Project boundaries

The ESP32-S3 is a Wi-Fi/TCP bridge and diagnostic console; AI execution remains on the relay/provider side. Do not embed private API credentials or upstream Wi-Fi secrets in the firmware or any files tracked by Git. Keep `config.h` private.

## Code of conduct

Be respectful, keep reports technical and reproducible, and do not publish other people's personal data or secrets. Maintainers may close unsafe or unrelated contributions.

## License

By contributing code, you agree to make that contribution available under the project's [MIT License](LICENSE), unless a different license is explicitly discussed and approved.
