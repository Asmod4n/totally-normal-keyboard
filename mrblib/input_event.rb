class Tnk
  class EventDevices
    EVENT_SIZE = 24

    def initialize(io_uring, hidraw_device)
      @io_uring = io_uring
      @hidraw_device = hidraw_device
      @event_devices = []
      @shift = @ctrl = @alt = @altgr = false
      @pressed_keys = Set.new

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

    def read_events(&block)
      @event_devices.each do |file|
        @io_uring.prep_read_fixed(file) do |read_op|
          buf = read_op.buf
          0.step(buf.bytesize - EVENT_SIZE, EVENT_SIZE) do |off|
            sec, usec, type, code, value = buf.byteslice(off, EVENT_SIZE)
                                           .unpack("qqSSl")
            next unless type == 1 # EV_KEY

            update_modifiers(code, value)

            time   = Time.at(sec, usec)
            action = value == 1 ? :down : (value == 0 ? :up : :hold)

            idx  = current_map_index
            name = Tnk::KeyMaps[idx] && Tnk::KeyMaps[idx][code]
            name ||= Tnk::KeyMaps[0] && Tnk::KeyMaps[0][code]
            name ||= :"KEY_#{code}"

            # gedrückte Tasten pflegen
            if action == :down
              @pressed_keys << name
            elsif action == :up
              @pressed_keys.delete(name)
            end

            block.call(time, name, action, @pressed_keys.dup)
          end
          @io_uring.return_used_buffer(read_op)
        end
      end
    end

    # Hilfsmethode für Kombinationsabfragen
    def keycombo?(*keys)
      keys.all? { |k| @pressed_keys.include?(k) }
    end

    private

    def update_modifiers(code, value)
      pressed = (value != 0)
      case code
      when 42, 54   then @shift = pressed   # KEY_LEFTSHIFT/KEY_RIGHTSHIFT
      when 29, 97   then @ctrl  = pressed   # KEY_LEFTCTRL/KEY_RIGHTCTRL
      when 56       then @alt   = pressed   # KEY_LEFTALT
      when 100      then @altgr = pressed   # KEY_RIGHTALT
      end
    end

    def current_map_index
      idx = 0
      idx |= 1 if @shift
      idx |= 2 if @altgr
      idx |= 4 if @ctrl
      idx |= 8 if @alt
      idx
    end
  end
end
