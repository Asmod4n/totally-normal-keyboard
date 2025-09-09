#!/bin/bash
set -e

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

source /etc/default/keyboard

/usr/bin/ckbcomp -compact "$XKBLAYOUT" "$XKBVARIANT" "$XKBMODEL" "$XKBOPTIONS" > "$SCRIPT_DIR/keymap.kmap"

/usr/bin/loadkeys -u --mktable "$SCRIPT_DIR/keymap.kmap" > "$SCRIPT_DIR/keymap.h"