# totally-normal-keyboard
What
----
This, currently, turns a arm computer into a usb HID forwarder for a connected host system, as long as its got a usb gadget dwc2 chip.
Like a Raspberry pi 400 or 500 for example.

How
---
Linux has a config fs which allows you to create usb devices for a host to connect to, like hid devices, mass storage, network, serial ports and more.

When you are on a raspberry pi 400 or 500 you have to add this to your /boot/firmware/config.txt to enanble the gadget mode of your usb-c port
```
dtoverlay=dwc2,dr_mode=peripheral
usb_max_current_enable=1
```
before any line starting with [
then reboot and connect your rpi to a usb port on another device.
It is recommended to connect your rpi to a powered usb hub which you then connect to your pc.
Tests with an old nintendo switch usb-c charger where sucessfull to run and compile this app on a raspberry pi 500 while connected to a powered usb hub which was connected to a pc.

Why
---
I've always wanted to carry around a secure password manager in a pocket i can just connect to any device.
Or paste something into a device which has no clipboard of its own, like in a BIOS or during OS Setup


TODO
====
- ~~forward all USB Hid devices to the conntected host~~
- ~~drop root privs as soon as possible~~
- ~~allow users to register hotkey handlers~~
- ~~run usercode in a isolated sandbox vm~~
- ~~cross compile for aarch64 with musl-cross~~
- ~~use msgpack to communicate with the isolated vm~~
- ~~write install and uninstall tasks~~
- ~~create a systemd unit file~~
- learn QT
- learn Windows GUI Programming
- write host side app for Windows
- learn macOS GUI Programmming
- write host side app for macOS
- implement a encrypted clipboard manager
. implement a encrypted password manager
- create a ecosystem for plugins which run on the host side and communicate with a sandboxed vm on the gadget side

LICENSE
=======
AGPL-3