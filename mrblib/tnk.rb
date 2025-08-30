class Tnk
  def initialize
    @io_uring = IO::Uring.new
    @hidg = Tnk::Hidg.new
    @hidraw_to_hidg = {}
    @event_devices = {}
    @forward = {}
    @running = true
    Tnk::Hidraw.list_hidraw_devices.each do |hidraw_device|
      hidg_device = "/dev/hidg#{hidraw_device[-1]}"
      hidraw_file = File.open(hidraw_device, 'r')
      hidg_file = File.open(hidg_device, 'a')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @event_devices[hidraw_file] = Tnk::EventDevices.new(@io_uring, hidraw_device)
      @forward[hidraw_file] = true
    end
  end

  def close
    if @running
      @hidraw_to_hidg.each do |hidraw_file, hidg_file|
        report_len = Tnk::Hidraw.calc_report_length_smart(hidraw_file.path)
        empty_report = "\x00" * report_len

        3.times { hidg_file.write(empty_report) }
      end

      @hidg.stop rescue nil
      @event_devices.each_value(&:close) rescue nil
      @running = false
    end
  end

  def run(&block)
    event_proc = Proc.new do |hidraw, event_devices|
      event_devices.read_events do |timestamp, name, action, pressed|
        @forward[hidraw] = block.call(timestamp, name, action, pressed)
        event_proc.call(hidraw, event_devices)
      end
    end

    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        if @forward[hidraw]
          @io_uring.prep_write(hidg, read_op.buf, -1) do |_write_op|
            @io_uring.return_used_buffer(read_op)
          end
        else
          @io_uring.return_used_buffer(read_op)
          @forward[hidraw] = true
        end
        hid_proc.call(hidraw, hidg)
      end
    end

    @event_devices.each do |hidraw, event_devices|
      event_proc.call(hidraw, event_devices)
    end

    @hidraw_to_hidg.each do |hidraw, hidg|
      hid_proc.call(hidraw, hidg)
    end

    Signal.trap(:INT) {close}
    Signal.trap(:TERM) {close}

    while @running
      @io_uring.wait do |op|
        if op.errno
          puts op.inspect
          raise op.errno
        end
      end
    end
  ensure
    close
  end
end
