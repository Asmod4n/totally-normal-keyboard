#!/bin/bash
# Setup or teardown USB CDC-NCM + HID + Mass Storage gadget
set -e

GADGET=/sys/kernel/config/usb_gadget/tnk

do_start() {
  # Already started? UDC must contain a non-whitespace controller name.
  if [ -f "$GADGET/UDC" ]; then
    local udc
    IFS= read -r udc < "$GADGET/UDC" || true
    if [ -n "${udc//[[:space:]]/}" ]; then
      echo "✅ Gadget already started (UDC=${udc}). Nothing to do."
      exit 0
    fi
  fi
  modprobe libcomposite
  mkdir -p "$GADGET"
  cd "$GADGET"

  # Basic descriptor
  echo 0x1d6b > idVendor       # Linux Foundation
  echo 0x0104 > idProduct      # Composite Device
  echo 0x0100 > bcdDevice
  echo 0x0200 > bcdUSB

  # Strings
  mkdir -p strings/0x409
  echo "1234567890" > strings/0x409/serialnumber
  echo "Hendrik" > strings/0x409/manufacturer
  echo "CDC-NCM Gadget" > strings/0x409/product

  # Configuration
  mkdir -p configs/c.1/strings/0x409
  echo "CDC-NCM" > configs/c.1/strings/0x409/configuration
  echo 250 > configs/c.1/MaxPower

  # Mass Storage
  mkdir -p functions/mass_storage.usb0
  mkdir -p /piusb
  if [ ! -f /piusb/disk.img ]; then
    echo "📦 Creating disk.img (128MB FAT32)..."
    dd if=/dev/zero of=/piusb/disk.img bs=1M count=128
    mkfs.vfat /piusb/disk.img
  else
    echo "✅ disk.img exists – skipping creation."
  fi
  echo 0 > functions/mass_storage.usb0/stall
  echo /piusb/disk.img > functions/mass_storage.usb0/lun.0/file
  echo 1 > functions/mass_storage.usb0/lun.0/removable
  ln -s functions/mass_storage.usb0 configs/c.1/

  # CDC-NCM networking
  mkdir -p functions/ncm.usb0
  echo "02:12:34:56:78:90" > functions/ncm.usb0/dev_addr
  echo "02:98:76:54:32:10" > functions/ncm.usb0/host_addr
  ln -s functions/ncm.usb0 configs/c.1/

  # HID keyboard
  mkdir -p functions/hid.usb0
  echo 1 > functions/hid.usb0/protocol
  echo 1 > functions/hid.usb0/subclass
  echo 8 > functions/hid.usb0/report_length

  # 🧠 Define HID (Human Interface Device) Report Descriptor for a USB Keyboard
  # This descriptor outlines the keyboard's data format for the host system.

  REPORT_DESC="\x05\x01"        # Usage Page (Generic Desktop)
  REPORT_DESC+="\x09\x06"       # Usage (Keyboard)
  REPORT_DESC+="\xa1\x01"       # Collection (Application)
  REPORT_DESC+="\x05\x07"       # Usage Page (Key Codes)
  REPORT_DESC+="\x19\xe0"       # Usage Minimum (Left Control)
  REPORT_DESC+="\x29\xe7"       # Usage Maximum (Right GUI)
  REPORT_DESC+="\x15\x00"       # Logical Minimum (0)
  REPORT_DESC+="\x25\x01"       # Logical Maximum (1)
  REPORT_DESC+="\x75\x01"       # Report Size (1 bit)
  REPORT_DESC+="\x95\x08"       # Report Count (8 bits for modifier keys)
  REPORT_DESC+="\x81\x02"       # Input (Data, Variable, Absolute)
  REPORT_DESC+="\x95\x01"       # Report Count (1)
  REPORT_DESC+="\x75\x08"       # Report Size (8 bits reserved byte)
  REPORT_DESC+="\x81\x01"       # Input (Constant)
  REPORT_DESC+="\x95\x05"       # Report Count (5 bits LED output)
  REPORT_DESC+="\x75\x01"       # Report Size (1 bit per LED)
  REPORT_DESC+="\x05\x08"       # Usage Page (LEDs)
  REPORT_DESC+="\x19\x01"       # Usage Minimum (Num Lock)
  REPORT_DESC+="\x29\x05"       # Usage Maximum (Kana)
  REPORT_DESC+="\x91\x02"       # Output (Data, Variable, Absolute)
  REPORT_DESC+="\x95\x01"       # Report Count (1)
  REPORT_DESC+="\x75\x03"       # Report Size (3 bits padding)
  REPORT_DESC+="\x91\x01"       # Output (Constant)
  REPORT_DESC+="\x95\x06"       # Report Count (6 keys)
  REPORT_DESC+="\x75\x08"       # Report Size (8 bits per key)
  REPORT_DESC+="\x15\x00"       # Logical Minimum (0)
  REPORT_DESC+="\x25\x65"       # Logical Maximum (101 keys)
  REPORT_DESC+="\x05\x07"       # Usage Page (Key Codes)
  REPORT_DESC+="\x19\x00"       # Usage Minimum (0)
  REPORT_DESC+="\x29\x65"       # Usage Maximum (101)
  REPORT_DESC+="\x81\x00"       # Input (Data, Array)
  REPORT_DESC+="\xc0"           # End Collection

  # 📝 Write the keyboard HID descriptor to the gadget function
  echo -ne "$REPORT_DESC" > functions/hid.usb0/report_desc

  # 🔗 Link the HID function into the gadget configuration
  ln -s functions/hid.usb0 configs/c.1/

  # Bind to UDC
  echo "$(ls /sys/class/udc)" > UDC

  # Network up
  sleep 2
  ip link set usb0 up || ifconfig usb0 up
  ip -6 addr add fe80::1 dev usb0
}

do_stop() {
  echo "🛑 Cleaning up USB gadget tnk..."

  # Step 1: Unbind from UDC
  if [ -f "$GADGET/UDC" ]; then
    echo "" > "$GADGET/UDC" 2>/dev/null || echo "⚠️ UDC already unbound or not present"
    echo "🔌 UDC unbound."
    sleep 1
  fi

  cd "$GADGET" || {
    echo "❌ Gadget directory not found: $GADGET"
    return 1
  }

  # Step 2: Remove function symlinks from configuration
  echo "🧹 Removing config symlinks..."
  rm -v configs/c.1/ncm.usb0
  rm -v configs/c.1/hid.usb0
  rm -v configs/c.1/mass_storage.usb0

  # Step 3: Remove config string directories
  echo "🧹 Removing configuration strings..."
  rmdir -v configs/c.1/strings/0x409

  # Step 4: Remove configuration itself
  rmdir -v configs/c.1

  # Step 5: Remove function directories
  echo "🧹 Removing functions..."
  rmdir -v functions/ncm.usb0
  rmdir -v functions/hid.usb0
  rmdir -v functions/mass_storage.usb0

  # Step 6: Remove gadget-level strings
  rmdir -v strings/0x409

  # Step 8: Remove gadget directory
  cd ..
  rmdir -v tnk && echo "✅ Gadget tnk removed completely."
}

# Entry point
case "$1" in
  start) do_start ;;
  stop)  do_stop ;;
  restart) do_stop; sleep 1; do_start ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
    ;;
esac
