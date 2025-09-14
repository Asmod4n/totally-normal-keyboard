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


build deps
----------
To build this you need to have ruby installed and the gcc toolchain and linux headers and some other packages, see below.
You also need to run this on linux, an attempt has been made to build this on macOS with musl-cross but it would require forking and patching too many tools and libs so it was abandoned.
This has only been tested to run on Raspberry pi os (aka Debian), it might work on other distros but you are on your own there.

here is a list of what packages you need for debian
```
- udev
- systemd
- ruby
- rake
- console-setup
- kbd
- build-essential
- linux-headers-$(uname -r)
```

You also need to run
```sh
sudo raspi-config
```

and set a correct keyboard layout and keymap, you find that under option 5.

getting the source
------------------
```sh
git clone --recurse-submodules https://github.com/Asmod4n/totally-normal-keyboard
```

compiling
---------
```sh
cd totally-normal-keyboard
rake
```

installing
----------
```sh
sudo rake install
```

running
-------
```sh
sudo service tnk start
```

enabling it to run at boot
--------------------------
```sh
sudo systemctl enable tnk.service
```

uninstalling
------------
```sh
sudo rake uninstall
```

You can run all rake commands with a PREFIX env var, which will set the app up to run and install from that prefix.
When you start this app you can set an env var named TNK_DROP_USER to which user to drop to after the setup phase as root is done.

Barebones hotkey support
------------------------
At the moment we got barebones hotkey registering and code running when set hotkey was pressed.
take a look at ```share/user.rb``` this is running a mruby core vm with no gems loaded, its currently being explored how to turn that into something usefull.

USB hotplug
-----------
You can hotplug usb hid devices, the app will restart automatically then.

Limitations
-----------
Via testing it was found out you cant connect more than one usb hid device besides the build in keyboard before the kernel errors out and wont allow you to create more hid gadgets.


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
- make hotkey mapping usefull
- learn QT
- learn Windows GUI Programming
- write host side app for Windows
- learn macOS GUI Programmming
- write host side app for macOS
- implement a encrypted clipboard manager
- implement a encrypted password manager
- create a ecosystem for plugins which run on the host side and communicate with a sandboxed vm on the gadget side

LICENSE
=======
AGPL-3