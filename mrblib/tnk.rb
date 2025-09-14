class Tnk
  def initialize
    @hidraw_to_hidg = {}
    @empty_report = {}
    @event_devices = {}
  end

  def setup_root
    Hidg.setup
    Tnk::Hidg.hid_map.each do |hidraw_path, hidg_path|
      hidraw_file = File.open(hidraw_path, 'rb')
      hidg_file   = File.open(hidg_path, 'wb')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @event_devices[hidraw_file]  = Tnk::EventDevices.new(hidraw_path)
      @empty_report[hidraw_file]   = "\x00" * Tnk::Hidraw.calc_report_length_smart(hidraw_path)
    end
  end

  def setup_user
    Tnk.gen_keymap
    Tnk.load_keymap
    @io_uring = IO::Uring.new
    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        debug_puts Hotkeys.handle_hid_report(read_op.buf)
        @io_uring.prep_write_fixed(hidg, read_op) do |write_op|
          @io_uring.return_used_buffer(write_op)
        end
        hid_proc.call(hidraw, hidg)
      end
    end

    @hidraw_to_hidg.each do |hidraw, hidg|
      hid_proc.call(hidraw, hidg)
    end

    debug_puts "✅ setup complete"
  end

  def run
    while true
      @io_uring.wait do |op|
        if op.errno
          warn op.inspect
          raise op.errno
        end
      end
    end
  end

  def close
    @hidraw_to_hidg.each do |hidraw_file, hidg_file|
      3.times { hidg_file.write(@empty_report[hidraw_file]) }
    end

    Hidg.stop
    @event_devices.each_value(&:close)
  end
end
