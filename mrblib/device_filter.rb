class Tnk
  # Decides which /sys/class/hidraw/* nodes get forwarded to a hidg function.
  #
  # Classification is done purely from the HID report descriptor - no VID/PID
  # table, no interface numbers, nothing to maintain per model. A device is
  # what its descriptor says it is.
  #
  # dwc2 allows at most 4 HID gadget functions, and the Pi 500+ alone exposes
  # three interfaces (boot keyboard, Vial config channel, system control), so
  # forwarding everything leaves no room for user devices.
  module DeviceFilter
    extend self

    PAGE_GENERIC_DESKTOP = 0x01
    PAGE_CONSUMER        = 0x0C
    PAGE_VENDOR_MIN      = 0xFF00

    USAGE_MOUSE          = 0x02
    USAGE_KEYBOARD       = 0x06
    USAGE_SYSTEM_CONTROL = 0x80

    # Parses a HID report descriptor. Returns:
    #   [usage_page, usage, has_report_ids]
    # usage_page/usage are the first top-level pair, which is what identifies
    # the device type. has_report_ids distinguishes a boot-protocol interface
    # (no Report IDs, fixed 8-byte layout) from a multi-report one.
    def inspect_descriptor(path)
      data = File.open(path, 'rb') { |f| f.read }
      bytes = data.bytes
      i = 0
      usage_page = nil
      usage = nil
      has_report_ids = false

      while i < bytes.size
        b = bytes[i]; i += 1

        if b == 0xFE # long item
          break if i + 2 > bytes.size
          i += 2 + bytes[i]
          next
        end

        size = [0, 1, 2, 4][b & 0x03]
        type = (b >> 2) & 0x03
        tag  = (b >> 4) & 0x0F

        value = 0
        if size > 0
          size.times { |j| value |= (bytes[i + j] || 0) << (8 * j) }
          i += size
        end

        if type == 1 # Global
          usage_page = value if tag == 0x00 && usage_page.nil?
          has_report_ids = true if tag == 0x08
        elsif type == 2 # Local
          usage = value if tag == 0x00 && usage.nil?
        end
      end

      [usage_page, usage, has_report_ids]
    end

    # Boot-protocol keyboard: Generic Desktop / Keyboard with no Report IDs.
    # This is the interface a BIOS/UEFI binds, and the only keyboard interface
    # worth forwarding - an NKRO interface carries Report IDs and is skipped.
    def boot_keyboard?(path)
      page, usage, has_ids = inspect_descriptor(path)
      page == PAGE_GENERIC_DESKTOP && usage == USAGE_KEYBOARD && !has_ids
    end

    def mouse?(path)
      page, usage, _ids = inspect_descriptor(path)
      page == PAGE_GENERIC_DESKTOP && usage == USAGE_MOUSE
    end

    # Vendor-defined page: config and firmware channels (Vial, mouse vendor
    # software, RGB tooling). No host OS binds these without vendor software,
    # and forwarding them exposes keymap/EEPROM writes to whatever you plug into.
    def vendor_defined?(path)
      page, _usage, _ids = inspect_descriptor(path)
      !!(page && page >= PAGE_VENDOR_MIN)
    end

    # Raspberry Pi Ltd. Only devices from this vendor get the strict filter -
    # they're the built-in keyboards that burn HID gadget slots on interfaces
    # the host has no use for. Everything a user plugs in is forwarded as-is.
    VID_RASPBERRY_PI = 0x2E8A

    def vendor_id(path)
      uevent_value(path, "HID_ID").to_s.split(":")[1].to_s.to_i(16)
    end

    def raspberry_pi?(path)
      vendor_id(path) == VID_RASPBERRY_PI
    end

  def forward?(path)
    return false unless File.exist?(path)
    return false if vendor_defined?(path)
    return true unless raspberry_pi?(path)
    boot_keyboard?(path)
  end

    def classify(path)
      page, usage, has_ids = inspect_descriptor(path)
      return "boot keyboard"  if boot_keyboard?(path)
      return "mouse"          if mouse?(path)
      return "vendor-defined" if vendor_defined?(path)
      if page == PAGE_GENERIC_DESKTOP && usage == USAGE_SYSTEM_CONTROL
        return "system control"
      end
      return "consumer" if page == PAGE_CONSUMER
      if page == PAGE_GENERIC_DESKTOP && usage == USAGE_KEYBOARD && has_ids
        return "keyboard (NKRO)"
      end
      page_s  = page  ? page.to_s(16)  : "?"
      usage_s = usage ? usage.to_s(16) : "?"
      "page=0x#{page_s} usage=0x#{usage_s}"
    end

    # Human-readable line for debug output. HID_NAME comes from the hidraw
    # uevent and needs no directory walking.
    def describe(path)
      name = hid_name(path) || "?"
      "#{name} - #{classify(path)}"
    end

    def hid_name(path)
      uevent_value(path, "HID_NAME")
    end

    def uevent_value(path, key)
      uevent = File.join(File.dirname(path), "uevent")
      return nil unless File.exist?(uevent)
      File.open(uevent) do |f|
        while line = f.gets
          k, v = line.chomp.split("=", 2)
          return v if k == key
        end
      end
      nil
    end
  end
end
