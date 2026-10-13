#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/psp"
command -v psp-config >/dev/null 2>&1 || { echo "PSPDEV/psp-config not installed; use GitHub Actions" >&2; exit 2; }
make
test -s EBOOT.PBP
echo "Built psp/EBOOT.PBP"
