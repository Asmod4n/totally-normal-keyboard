class Tnk
  class InputEvent
    def initialize(hidraw_dev)
      @hidraw_dev = hidraw_dev
      @event_devices = []
      Tnk::Hidraw.hidraw_to_event_devices(hidraw_dev).each do |event_dev|
        file = File.open(event_dev, 'rb')
        Tnk.grab(file.fileno)
        @event_devices << file
      end

    end

    def read_events(&block)
      @event_devices.each do |file|
        @io_uring.prep_read_fixed(file) do |read_op|
          if read_op.errno
            raise "Failed to read from #{file}: #{read_op.errno}"
          end
          block.call(read_op.buf) if block_given?
        end
      end
    end

    def close
      @event_devices.each do |file|
        Tnk.ungrab(file.fileno) rescue nil
        file.close rescue nil
      end
      @event_devices.clear
    end
  end
end
