#!/bin/bash
set -e

# Absoluten Pfad zum ../src relativ zum Skript ermitteln
SCRIPT_DIR="$(dirname "$(realpath "$0")")"

# /etc/default/keyboard einlesen
source /etc/default/keyboard

# .kmap erzeugen
/usr/bin/ckbcomp -compact "$XKBLAYOUT" "$XKBVARIANT" "$XKBMODEL" "$XKBOPTIONS" > "$SCRIPT_DIR/keymap.kmap"

# C-Header aus der .kmap erzeugen
/usr/bin/loadkeys -u --mktable "$SCRIPT_DIR/keymap.kmap" > "$SCRIPT_DIR/keymap.h"