#!/bin/bash
set -e

# Verzeichnis des Skripts + ein Verzeichnis hoch = Gem-Wurzel
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTDIR="${ROOT_DIR}/../src"
mkdir -p "$OUTDIR"


sudo dumpkeys -C /dev/tty1|sudo loadkeys -C /dev/tty1 -u -m >  "${OUTDIR}/keymap.h"