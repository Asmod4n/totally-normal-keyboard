#!/bin/bash
set -e

# Absoluten Pfad zum ../src relativ zum Skript ermitteln
SCRIPT_DIR="$(dirname "$(realpath "$0")")"
OUTPUT="$SCRIPT_DIR/../src"

# /etc/default/keyboard einlesen
source /etc/default/keyboard

# .kmap erzeugen
ckbcomp -compact "$XKBLAYOUT" "$XKBVARIANT" "$XKBMODEL" "$XKBOPTIONS" > "$SCRIPT_DIR/keymap.kmap"

# C-Header aus der .kmap erzeugen
loadkeys -u --mktable "$SCRIPT_DIR/keymap.kmap" > "$OUTPUT/keymap.h"

echo "Fertig: keymap.kmap erstellt, geladen und $OUTPUT/keymap.h geschrieben."
