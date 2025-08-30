class Tnk
  class EventDevices
    def initialize(io_uring, hidraw_device)
      @io_uring = io_uring
      @hidraw_device = hidraw_device
      @event_devices = []

      Tnk::Hidraw.hidraw_to_by_id_names(hidraw_device).each do |by_id_name|
        path = File.join("/dev/input/by-id", by_id_name)
        file = File.open(path, 'rb')
        Tnk.grab(file.fileno)
        @event_devices << file
      end
    end

    def close
      @event_devices.each do |file|
        Tnk.ungrab(file.fileno) rescue nil
        file.close rescue nil
      end
      @event_devices.clear
      nil
    end
  end
end
