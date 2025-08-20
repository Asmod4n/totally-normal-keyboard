class Tnk
  module Hidraw
    # hidraw_event_resolution.rb
    #
    # Enumerate /dev/hidraw* devices and resolve their corresponding /dev/input/event* nodes.
    # Written for mruby, using idiomatic Ruby code and minimal dependencies.

    # List all /dev/hidraw* device paths discovered via /sys/class/hidraw
    def self.list_hidraw_devices
      sys_class = "/sys/class/hidraw"
      return [] unless File.directory?(sys_class)

      Dir.entries(sys_class)
        .select { |entry| entry.start_with?("hidraw") }
        .sort
        .map { |name| "/dev/#{name}" }
    end

    # Given a /dev/hidrawN device, return its associated /dev/input/event* nodes.
    def self.hidraw_to_event_devices(hidraw_dev)
      sys_class = "/sys/class/hidraw"
      hidraw_name = File.basename(hidraw_dev)
      sys_path = File.join(sys_class, hidraw_name, "device")
      return [] unless File.exist?(sys_path)

      begin
        device_dir = File.realpath(sys_path)
      rescue
        return []
      end

      events = []

      Dir.entries(device_dir).each do |entry|
        next if entry == "." || entry == ".."
        entry_path = File.join(device_dir, entry)

        # If entry is "input", check its subdirectories for input*/event* nodes
        if entry == "input" && File.directory?(entry_path)
          Dir.entries(entry_path).each do |subentry|
            subentry_path = File.join(entry_path, subentry)
            if subentry.start_with?("input") && File.directory?(subentry_path)
              events.concat(find_event_nodes(subentry_path))
            end
          end
        elsif entry.start_with?("input") && File.directory?(entry_path)
          events.concat(find_event_nodes(entry_path))
        end
      end

      events.uniq.sort
    end

    def self.calc_report_length(path)
      raise ReportDescriptorError, "No path provided" if !path || path.empty?

      data =
        begin
          File.open(path, 'rb') { |f| f.read }
        rescue => e
          raise ReportDescriptorError, "Could not open '#{path}': #{e.message}"
        end

      bytes = data.bytes
      i = 0
      max_report_size = 0
      current_report_id = 0
      has_report_ids = false
      report_bits = 0

      # State for Global
      input_size_bits = 0
      input_count = 0

      # Helper for calculating byte length
      def byte_length(bits)
        (bits + 7) / 8
      end

      # Update max_report_size if needed
      update_max = lambda do
        if report_bits > 0
          length = byte_length(report_bits)
          length += 1 if current_report_id != 0 || has_report_ids
          max_report_size = length if length > max_report_size
        end
      end

      while i < bytes.size
        b = bytes[i]; i += 1

        if b == 0xFE # long item
          raise ReportDescriptorError, "Malformed long item" if i + 2 > bytes.size
          size = bytes[i]
          i += 2 + size
          next
        end

        size_code = b & 0x03
        size = [0, 1, 2, 4][size_code]
        type = (b >> 2) & 0x03
        tag = (b >> 4) & 0x0F

        value = 0
        if size > 0
          size.times do |j|
            value |= (bytes[i + j] || 0) << (8 * j)
          end
          i += size
        end

        if type == 1 # Global
          case tag
          when 0x07 then input_size_bits = value
          when 0x09 then input_count = value
          when 0x08
            update_max.call
            current_report_id = value
            has_report_ids = true
            report_bits = 0
          end
        elsif type == 0 && tag == 0x08 # Main Input
          report_bits += input_size_bits * input_count
        end
      end

      update_max.call
      max_report_size = 8 if max_report_size == 0
      max_report_size
    end

    def self.calc_report_length_smart(path)
      if path.start_with?("/dev/hidraw")
        node = path.split("/").last
        sysfs_path = "/sys/class/hidraw/#{node}/device/report_descriptor"
        puts "[INFO] Given device node, using sysfs report descriptor: #{sysfs_path}"
        calc_report_length(sysfs_path)
      else
        calc_report_length(path)
      end
    end


    private

    # Helper: Find /dev/input/event* nodes in a directory
    def self.find_event_nodes(dir)
      return [] unless File.directory?(dir)
      Dir.entries(dir)
        .select { |name| name.start_with?("event") }
        .map { |event| "/dev/input/#{event}" }
    end
  end

# Example usage:
# puts list_hidraw_devices.inspect
# puts hidraw_to_event_devices("/dev/hidraw0").inspect


end
