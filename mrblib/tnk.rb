class Tnk
  def initialize
    @hidraw_to_hidg = {}
    @empty_report = {}
    @event_devices = {}
  end

  def setup
    Hidg.setup
    Tnk.gen_keymap
    Tnk.load_keymap
    Tnk::Hidraw.list_hidraw_devices.each do |hidraw_device|
      hidg_device = "/dev/hidg#{hidraw_device[-1]}"
      hidraw_file = File.open(hidraw_device, 'rb')
      hidg_file = File.open(hidg_device, 'wb')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @event_devices[hidraw_file] = Tnk::EventDevices.new(hidraw_device)
      @empty_report[hidraw_file] = "\x00" * Tnk::Hidraw.calc_report_length_smart(hidraw_device)
    end
  end

  def run
    @io_uring = IO::Uring.new
    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        Hotkeys.handle_hid_report(read_op.buf)
        @io_uring.prep_write_fixed(hidg, read_op) do |write_op|
          @io_uring.return_used_buffer(write_op)
        end
        hid_proc.call(hidraw, hidg)
      end
    end

    @hidraw_to_hidg.each do |hidraw, hidg|
      hid_proc.call(hidraw, hidg)
    end

    puts "✅ setup complete"

    while true
      @io_uring.wait do |op|
        if op.errno
          puts op.inspect
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
