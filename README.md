# totally-normal-keyboard

Because sometimes a computer just isn’t enough—you need it to *pretend* it’s a keyboard too.

---

## What
This turns an ARM computer into a USB HID forwarder for a connected host system—as long as it’s got a USB gadget `dwc2` chip.
Think Raspberry Pi 400 or 500. (Yes, the Pi can now cosplay as your keyboard.)

---

## How
Linux has a `configfs` that lets you create USB devices for a host to connect to: HID devices, mass storage, network, serial ports, and more.

On a Raspberry Pi 400 or 500 you’ll need to add this to your `/boot/firmware/config.txt` to enable gadget mode on your USB-C port:
```
dtoverlay=dwc2,dr_mode=peripheral
usb_max_current_enable=1
```
(Add it before any line starting with `[`)

Then reboot, connect your Pi to another device.
Pro tip: use a powered USB hub. Tests with an old Nintendo Switch charger to power a hub were successful.
---

## Why
Ever wanted to carry around a secure password manager that doubles as a keyboard in your pocket?
Or paste something into a BIOS setup screen, or during OS Setup?
Or copy a barcode and paste it as text?
Or copy from your mac, hit a key combo and then paste to your windows pc?


All of that and more with no proprietary code at all, powered by an plugin eco system to enhance its functionality in a secure way.
---

## Build deps
You’ll need:
- Linux (sorry macOS, i tried, but it got unmaintainable fast)
- Ruby
- GCC toolchain
- Linux headers
- Some other packages, see below.

On Debian/Raspberry Pi OS, install these:
```
udev
systemd
ruby
rake
console-setup
kbd
build-essential
linux-headers-$(uname -r)
```

And don’t forget to run:
```sh
sudo raspi-config
```
to set the correct keyboard layout (option 5).

---

## Getting the source
```sh
git clone --recurse-submodules https://github.com/Asmod4n/totally-normal-keyboard
```

---

## Compiling
```sh
cd totally-normal-keyboard
rake
```

---

## Installing
```sh
sudo rake install
```

---

## Running
```sh
sudo service tnk start
```

---

## Autostart at boot
```sh
sudo systemctl enable tnk.service
```

---

## Uninstalling
```sh
sudo rake uninstall
```

---

### Notes
- All rake commands accept a `PREFIX` env var.
- Use `TNK_DROP_USER` to tell the app which user it should drop down to after root setup is complete.

---

## Barebones hotkey support
Currently supports registering hotkeys and running code when they’re pressed.
See `share/user.rb` — it runs inside a tiny `mruby` core VM with no gems.
It’s not super useful *yet*, please come back later for updates.

---

## USB hotplug
You can hotplug USB HID devices.
The app will restart itself automatically.

---

## Limitations
- You can’t connect more than one USB HID device besides the built-in keyboard.
- The kernel doesn't allow more, most likely a hardware or USB HID Spec limitation.

---

## TODO

### ✅ Foundations (done)
- Forward all USB HID devices
- Drop root privileges ASAP
- User‑registerable hotkey handlers
- Sandbox user code in a VM
- Cross‑compile for aarch64 from other archs
- Msgpack communication with sandboxed VM
- Install/uninstall tasks
- Systemd unit file
- USB hotplug support

### ⏳ Next up: usability & trust
- Make hotkey mapping actually useful
- Signed build audit log
- Self‑extracting installer with signature checking for updates.

### ⏳ Connectivity & UI
- Bluetooth support
- Learn Qt
- Learn Windows GUI programming
- Write host‑side app for Windows
- Learn macOS GUI programming
- Write host‑side app for macOS

### ⏳ Secure features
- Encrypted clipboard manager
- Decoding of barcodes to send as keystrokes to a host
- Encrypted password manager

### ⏳ Long‑term vision
- Plugin ecosystem


---

## License
AGPL-3
