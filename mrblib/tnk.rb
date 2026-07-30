class Tnk
  def initialize
    @hidraw_to_hidg = {}
    @empty_report = {}
    @event_devices = {}
    @needs_report_id_prefix = {}
    @report_length = {}
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
    end
  end

  def setup_user
    Tnk.gen_keymap
    @io_uring = IO::Uring.new
    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        debug_puts Hotkeys.handle_hid_report(read_op.buf)
        len = @report_length[hidraw]
        buf = read_op.buf
        if buf.bytesize < len
          @io_uring.prep_write(hidg, buf + "\x00" * (len - buf.bytesize), 0)
          @io_uring.return_used_buffer(read_op)
        else
          @io_uring.prep_write_fixed(hidg, read_op) { |op| @io_uring.return_used_buffer(op) }
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
    @hidraw_to_hidg.each do |hidraw_file, hidg_file|
      3.times { hidg_file.write(@empty_report[hidraw_file]) }
      hidraw_file.close
      hidg_file.close
    end

    @event_devices.each_value(&:close)
    Hidg.stop
  end
end
