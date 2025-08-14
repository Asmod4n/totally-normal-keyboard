#!/bin/bash
set -e

GADGET=/sys/kernel/config/usb_gadget/tnk

calc_report_length() {
    local hidraw="$1"
    if [ -z "$hidraw" ]; then
        echo "Usage: calc_report_length <hidraw report descriptor file>" >&2
        return 1
    fi

    python3 - "$hidraw" <<'EOF'
import sys, math

hidraw_path = sys.argv[1]
data = open(hidraw_path, "rb").read()

i = 0
rep_size_bits = rep_count = 0
current_id = 0
has_ids = False
max_bytes = 0
cur_bits = 0

def flush():
    global cur_bits, max_bytes, current_id, has_ids
    if cur_bits:
        length = math.ceil(cur_bits / 8)
        if current_id != 0 or has_ids:
            length += 1
        max_bytes = max(max_bytes, length)

while i < len(data):
    b = data[i]; i += 1
    if b == 0xFE:  # long item
        if i+2 > len(data): break
        size = data[i]; i += 2 + size
        continue
    size_code = b & 0x03
    size = [0,1,2,4][size_code]
    typ = (b >> 2) & 0x03
    tag = (b >> 4) & 0x0F
    payload = data[i:i+size]; i += size
    val = int.from_bytes(payload, "little") if size else 0

    if typ == 1:  # Global
        if tag == 0x07: rep_size_bits = val
        elif tag == 0x09: rep_count = val
        elif tag == 0x08:
            flush()
            current_id = val
            has_ids = True
            cur_bits = 0
    elif typ == 0 and tag == 0x08:  # Input
        cur_bits += rep_size_bits * rep_count

flush()
if max_bytes == 0: max_bytes = 8
print(max_bytes)
EOF
}


do_start() {
  # Schon gestartet?
  if [ -f "$GADGET/UDC" ]; then
    local udc
    IFS= read -r udc < "$GADGET/UDC" || true
    if [ -n "${udc//[[:space:]]/}" ]; then
      echo "✅ Gadget already started (UDC=${udc})."
      exit 0
    fi
  fi

  modprobe libcomposite
  mkdir -p "$GADGET"
  cd "$GADGET"

  # Basis-Deskriptor
  echo 0x1d6b > idVendor
  echo 0x0104 > idProduct
  echo 0x0100 > bcdDevice
  echo 0x0200 > bcdUSB

  # Strings
  mkdir -p strings/0x409
  echo "1234567890" > strings/0x409/serialnumber
  echo "Hendrik" > strings/0x409/manufacturer
  echo "CDC-NCM Gadget" > strings/0x409/product

  # Konfiguration
  mkdir -p configs/c.1/strings/0x409
  echo "CDC-NCM" > configs/c.1/strings/0x409/configuration
  echo 250 > configs/c.1/MaxPower

  # Mass Storage
  mkdir -p functions/mass_storage.usb0
  mkdir -p /piusb
  if [ ! -f /piusb/disk.img ]; then
    echo "📦 Creating disk.img..."
    dd if=/dev/zero of=/piusb/disk.img bs=1M count=128
    mkfs.vfat /piusb/disk.img
  else
    echo "✅ disk.img exists – skipping creation."
  fi
  echo 0 > functions/mass_storage.usb0/stall
  echo /piusb/disk.img > functions/mass_storage.usb0/lun.0/file
  echo 1 > functions/mass_storage.usb0/lun.0/removable
  ln -s functions/mass_storage.usb0 configs/c.1/

  # CDC-NCM Netzwerk
  mkdir -p functions/ncm.usb0
  echo "02:12:34:56:78:90" > functions/ncm.usb0/dev_addr
  echo "02:98:76:54:32:10" > functions/ncm.usb0/host_addr
  ln -s functions/ncm.usb0 configs/c.1/

  # Dynamische HID-Funktionen
  echo "🧠 Scanning for HID report descriptors..."
  hid_index=0
  for hidraw in /sys/class/hidraw/hidraw*/device/report_descriptor; do
    if [ -r "$hidraw" ]; then
      length=$(calc_report_length "$hidraw")
      echo "🔧 Adding HID function $hid_index (report_length=$length)..."
      mkdir -p "functions/hid.usb${hid_index}"
      echo 0 > "functions/hid.usb${hid_index}/protocol"
      echo 0 > "functions/hid.usb${hid_index}/subclass"
      echo "$length" > "functions/hid.usb${hid_index}/report_length"
      cat "$hidraw" > "functions/hid.usb${hid_index}/report_desc"
      ln -s "functions/hid.usb${hid_index}" configs/c.1/
      hid_index=$((hid_index + 1))
    fi
  done

  # An Controller binden
  echo "$(ls /sys/class/udc)" > UDC

  # Netzwerk hochfahren
  sleep 2
  ip link set usb0 up || ifconfig usb0 up
  ip -6 addr add fe80::1 dev usb0
}

do_stop() {
  echo "🛑 Cleaning up USB gadget tnk..."
  if [ -f "$GADGET/UDC" ]; then
    echo "" > "$GADGET/UDC" 2>/dev/null || echo "⚠️ UDC already unbound"
    sleep 1
  fi

  cd "$GADGET" || {
    echo "❌ Gadget directory not found: $GADGET"
    return 1
  }

  echo "🧹 Removing config symlinks..."
  for link in configs/c.1/*; do
    [ -L "$link" ] && rm -v "$link"
  done

  rmdir -v configs/c.1/strings/0x409 2>/dev/null || true
  rmdir -v configs/c.1 2>/dev/null || true

  echo "🧹 Removing functions..."
  for func in functions/*; do
    rmdir -v "$func" 2>/dev/null || true
  done

  rmdir -v strings/0x409 2>/dev/null || true
  cd ..
  rmdir -v tnk && echo "✅ Gadget tnk removed."
}

case "$1" in
  start) do_start ;;
  stop) do_stop ;;
  restart) do_stop; sleep 1; do_start ;;
  *) echo "Usage: $0 {start|stop|restart}"; exit 1 ;;
esac
