class Tnk
  def initialize
    @io_uring = IO::Uring.new
    @hidg = Tnk::Hidg.new
    @hidraw_to_hidg = {}
    @input_events = []

    Tnk::Hidraw.list_hidraw_devices.each do |hidraw_device|
      hidg_device = "/dev/hidg#{hidraw_device[-1]}"
      hidraw_file = File.open(hidraw_device, 'rb')
      hidg_file = File.open(hidg_device, 'wb')
      @hidraw_to_hidg[hidraw_file] = hidg_file
      @input_events << Tnk::InputEvent.new(hidraw_device)
    end
    @running = true
    Signal.trap(:INT) { @running = false;  }
  end

  def run
    loop_proc = Proc.new do |hidraw, hidg|
      @io_uring.prep_read_fixed(hidraw) do |read_op|
        @io_uring.prep_write(hidg, read_op.buf) do |write_op|
          @io_uring.return_used_buffer(read_op)
        end
      end
    end
    @hidraw_to_hidg.each do |hidraw, hidg|
      loop_proc.call(hidraw, hidg)
    end
    while @running
      @io_uring.wait do |op|
        if op.errno
          raise "IOUring operation failed: #{op.errno}"
        else
          case op.type
          when :read_fixed
            loop_proc.call(op.file, @hidraw_to_hidg[op.file])
          end
        end
      end
    end
  ensure
    @hidg.stop;
    @input_events.each(&:close)
  end
end
