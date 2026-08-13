class Tnk
  ENTER_USAGE = 0x28

  def initialize
    @hidraw_to_hidg = {}
    @empty_report = {}
    @event_devices = {}
    @needs_report_id_prefix = {}
    @report_length = {}
    @is_boot_keyboard = {}
    @descriptor_by_hidraw = {}
    @hidg_by_descriptor = {}
    @report_length_by_descriptor = {}
    @mode = :passthrough
    @recording_id = nil
    @recording_buf = nil
    @passphrase_buf = nil
    @passphrase_offset = 0
  end

  def setup_root
    Hidg.setup
    Hidg.hid_map.each do |hidraw_path, hidg_path|
      hidraw_file = File.open(hidraw_path, 'r+')
      hidg_file   = File.open(hidg_path, 'r+')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @event_devices[hidraw_file]  = EventDevices.new(hidraw_path)
      len = Hidraw.calc_report_length_smart(hidraw_path)
      @report_length[hidraw_file] = len
      @empty_report[hidraw_file]   = "\x00" * len
      desc = "/sys/class/hidraw/#{File.basename(hidraw_path)}/device/report_descriptor"
      _page, _usage, has_ids = DeviceFilter.inspect_descriptor(desc)
      @needs_report_id_prefix[hidraw_file] = !has_ids
      @is_boot_keyboard[hidraw_file] = DeviceFilter.boot_keyboard?(desc)

      descriptor = File.open(desc, 'rb') { |f| f.read }
      @descriptor_by_hidraw[hidraw_file] = descriptor
      @hidg_by_descriptor[descriptor] = hidg_file
      @report_length_by_descriptor[descriptor] = len
    end
  end

  def setup_user
    Tnk.gen_keymap
    open_vault
    @io_uring = IO::Uring.new
    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        buf = read_op.buf
        raw = Hotkeys.handle_hid_report(buf)
        debug_puts raw unless raw.nil?
        forward = handle_report(hidraw, hidg, buf, hotkey_command(raw))

        if forward
          len = @report_length[hidraw]
          if buf.bytesize < len
            @io_uring.prep_write(hidg, buf + "\x00" * (len - buf.bytesize), 0)
            @io_uring.return_used_buffer(read_op)
          else
            @io_uring.prep_write_fixed(hidg, read_op) { |op| @io_uring.return_used_buffer(op) }
          end
        else
          @io_uring.return_used_buffer(read_op)
        end

        hid_proc.call(hidraw, hidg)
      end
    end

    led_proc = Proc.new do |hidg, hidraw|
      @io_uring.prep_read_fixed(hidg) do |read_op|
        data = read_op.buf
        data = "\x00" + data if @needs_report_id_prefix[hidraw]
        @io_uring.prep_write(hidraw, data, 0) if data.bytesize > 1
        @io_uring.return_used_buffer(read_op)
        led_proc.call(hidg, hidraw)
      end
    end

    @hidraw_to_hidg.each do |hidraw, hidg|
      hid_proc.call(hidraw, hidg)
      led_proc.call(hidg, hidraw)
    end

    debug_puts "✅ setup complete"
  end

  def run
    while true
      @io_uring.wait do |op|
        if op.errno
          if op.errno.is_a?(Errno::EIO)
            return
          else
            debug_puts op.inspect
            raise op.errno
          end
        end
      end
    end
  end

  def close
    Vault.close
    @hidraw_to_hidg.each do |hidraw_file, hidg_file|
      3.times { hidg_file.write(@empty_report[hidraw_file]) }
      hidraw_file.close
      hidg_file.close
    end

    @event_devices.each_value(&:close)
    Hidg.stop
  end

  private

  def open_vault
    exe_path = File.realpath("/proc/self/exe")
    base_dir = File.realpath(File.dirname(exe_path))
    share_dir = File.realpath(File.join(base_dir, "../share/totally-normal-keyboard"))
    Vault.open(share_dir)
  rescue => e
    debug_puts "⚠️  Vault unavailable: #{e.message}"
  end

  # Sandbox return values only ever cross the CBOR boundary as plain data,
  # so this is a schema check, not a security boundary in itself - but it
  # keeps a malformed/unrelated return value (e.g. a hotkey that just
  # returns a String for logging, like share/user.rb's demo) from ever
  # being misread as a vault command.
  def hotkey_command(cmd)
    return nil unless cmd.is_a?(Array) && cmd[0].is_a?(Symbol)
    case cmd[0]
    when :start_recording, :end_recording, :replay
      cmd.size == 2 && cmd[1].is_a?(Integer) ? cmd : nil
    when :unlock, :lock
      cmd.size == 1 ? cmd : nil
    else
      nil
    end
  end

  # Returns true if `buf` should be forwarded to the host as usual.
  # Recording spans every connected device at once (so a mouse dragged
  # mid-recording ends up in the same macro as the keystrokes around it)
  # and suppresses forwarding for all of them while active, including the
  # report that ends the mode. Unlock-input is scoped to the boot keyboard
  # only - anything else (e.g. a connected mouse) keeps forwarding
  # normally, since it's neither part of a passphrase nor a secret.
  def handle_report(hidraw, hidg, buf, cmd)
    case @mode
    when :recording
      if cmd == [:end_recording, @recording_id]
        Vault.store_macro(@recording_id, @recording_buf)
        @mode = :passthrough
        @recording_id = nil
        @recording_buf = nil
      else
        @recording_buf << [@descriptor_by_hidraw[hidraw], buf.dup]
      end
      false

    when :unlock_input
      if @is_boot_keyboard[hidraw]
        if enter_pressed?(buf)
          ok = Vault.unlock(@passphrase_buf)
          debug_puts(ok ? "🔓 vault unlocked" : "🔒 wrong passphrase")
          @mode = :passthrough
          @passphrase_buf = nil
          @passphrase_offset = 0
        else
          append_passphrase_report(buf)
        end
        false
      else
        true
      end

    else
      if cmd
        case cmd[0]
        when :start_recording
          @mode = :recording
          @recording_id = cmd[1]
          @recording_buf = []
        when :unlock
          @mode = :unlock_input
          @passphrase_buf = "\x00" * Vault::PASSPHRASE_BYTES
          @passphrase_offset = 0
        when :replay
          replay_macro(cmd[1])
        when :lock
          Vault.lock
        end
      end
      true
    end
  end

  def enter_pressed?(buf)
    i = 2
    while i < buf.bytesize
      return true if buf.getbyte(i) == ENTER_USAGE
      i += 1
    end
    false
  end

  # Fixed 8-byte-per-report write into the pre-sized passphrase buffer -
  # never grows it, so its backing storage never reallocates/moves and
  # secure_wipe_memory always wipes the right memory later. Extra
  # keystrokes past PASSPHRASE_BYTES are dropped, not appended.
  def append_passphrase_report(buf)
    return if @passphrase_offset >= Vault::PASSPHRASE_BYTES
    8.times { |i| @passphrase_buf.setbyte(@passphrase_offset + i, buf.getbyte(i) || 0) }
    @passphrase_offset += 8
  ensure
    secure_wipe_memory(buf)
  end

  # Each recorded entry is [descriptor, report] - routed by matching the
  # exact descriptor bytes against whatever's currently connected, not by
  # a fixed hidg path. That also survives the recorded device having been
  # unplugged and replugged into a different port (new hid_index, same
  # descriptor) between recording and replay. No match currently
  # connected -> that report is skipped, not the whole replay.
  def replay_macro(id)
    reports = Vault.load_macro(id)
    return unless reports

    write_next = nil
    write_next = Proc.new do |remaining|
      unless remaining.empty?
        descriptor, report = remaining[0]
        target = @hidg_by_descriptor[descriptor]
        unless target
          debug_puts "⚠️  replay: no connected device matches this recording's descriptor, skipping report"
          next write_next.call(remaining[1..-1])
        end
        len = @report_length_by_descriptor[descriptor]
        padded = report.bytesize < len ? report + "\x00" * (len - report.bytesize) : report
        @io_uring.prep_write(target, padded, 0) do
          write_next.call(remaining[1..-1])
        end
      end
    end
    write_next.call(reports)
  rescue VaultError => e
    debug_puts "⚠️  replay failed: #{e.message}"
  end
end
