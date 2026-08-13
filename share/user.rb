Tnk::Hotkeys.on(:lshift, "z") { "Shift+Z pressed" }

# Vault: record a password once by typing it on the physical keyboard,
# replay it later without ever typing it again. Recording and unlock
# input never reach the connected host - only the final replay does.
Tnk::Hotkeys.on(:lctrl, :lalt, "1") { [:start_recording, 1] }
Tnk::Hotkeys.on(:lctrl, :lalt, "2") { [:end_recording, 1] }
Tnk::Hotkeys.on(:lctrl, :lalt, "3") { [:replay, 1] }
Tnk::Hotkeys.on(:lctrl, :lalt, "u") { [:unlock] }
Tnk::Hotkeys.on(:lctrl, :lalt, "l") { [:lock] }
