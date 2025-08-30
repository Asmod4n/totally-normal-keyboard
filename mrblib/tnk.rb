class Tnk
  def initialize
    @io_uring = IO::Uring.new
    @hidg = Tnk::Hidg.new
    @hidraw_to_hidg = {}
    @hidraw_report_length = {}
    @event_devices = {}
    Tnk::Hidraw.list_hidraw_devices.each do |hidraw_device|
      hidg_device = "/dev/hidg#{hidraw_device[-1]}"
      hidraw_file = File.open(hidraw_device, 'rb')
      hidg_file = File.open(hidg_device, 'wb')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @event_devices[hidraw_file] = Tnk::EventDevices.new(@io_uring, hidraw_device)
      @hidraw_report_length[hidraw_file] = Tnk::Hidraw.calc_report_length_smart(hidraw_device)
    end
    @running = true
  end

  def close
    if @running
      @hidraw_to_hidg.each do |hidraw_file, hidg_file|
        empty_report = "\x00" * @hidraw_report_length[hidraw_file]

        3.times { hidg_file.write(empty_report) }
      end

      @hidg.stop
      @event_devices.each_value(&:close)
      @running = false
    end
  end

  def run
    hid_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        @io_uring.prep_write(hidg, read_op.buf, 0) do |_write_op|
          @io_uring.return_used_buffer(read_op)
        end
        hid_proc.call(hidraw, hidg)
      end
    end

    @hidraw_to_hidg.each do |hidraw, hidg|
      hid_proc.call(hidraw, hidg)
    end

    puts "✅ setup complete"


    while @running
      @io_uring.wait do |op|
        if op.errno
          puts op.inspect
          raise op.errno
        end
      end
    end
  end
end
