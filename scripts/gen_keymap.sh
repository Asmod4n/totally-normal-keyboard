#!/bin/bash
set -e
source /etc/default/keyboard

if [ -n "$XKBVARIANT" ]; then
  KEYMAP_SEARCH="${XKBLAYOUT}*${XKBVARIANT}"
else
  KEYMAP_SEARCH="${XKBLAYOUT}"
fi

# Verzeichnis des Skripts + ein Verzeichnis hoch = Gem-Wurzel
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTDIR="${ROOT_DIR}/../src"
mkdir -p "$OUTDIR"

# Suche nach passender .map oder .map.gz
KEYMAP_FILE=$(find /usr/share/keymaps \
  -type f \( -name "*.kmap.gz" \) \
  -iname "${KEYMAP_SEARCH}*" 2>/dev/null | head -n1)

if [ -z "$KEYMAP_FILE" ]; then
  echo "❌ Could not find keymap file matching '${KEYMAP_SEARCH}'" >&2
  exit 1
fi

echo "ℹ Using keymap file: $KEYMAP_FILE"
loadkeys -u -C /dev/tty1 --mktable "$KEYMAP_FILE" > "${OUTDIR}/keymap.h"
