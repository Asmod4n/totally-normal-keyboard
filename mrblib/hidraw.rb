class Tnk
  module Hidraw
    # Liste aller /dev/hidraw*-Geräte
    def self.list_hidraw_devices
      sys_class = "/sys/class/hidraw"

      Dir.entries(sys_class)
        .select { |entry| entry.start_with?("hidraw") }
        .sort
        .map { |name| "/dev/#{name}" }
    end

    def self.hidraw_to_by_id_names(hidraw_dev)
      sys_class   = "/sys/class/hidraw"
      hidraw_name = File.basename(hidraw_dev)
      sys_path    = File.join(sys_class, hidraw_name, "device")

      device_dir = File.realpath(sys_path)

      # Alle eventX-Nodes ermitteln
      event_nodes = []
      Dir.entries(device_dir).each do |entry|
        next if entry.start_with?(".")
        path = File.join(device_dir, entry)

        if entry == "input" && File.directory?(path)
          Dir.entries(path).each do |subentry|
            subpath = File.join(path, subentry)
            if subentry.start_with?("input") && File.directory?(subpath)
              event_nodes.concat(find_event_nodes(subpath))
            end
          end
        elsif entry.start_with?("input") && File.directory?(path)
          event_nodes.concat(find_event_nodes(path))
        end
      end
      event_nodes.map! { |p| File.realpath(p) rescue p }

      # by-id Symlinks finden, die auf diese eventX zeigen
      by_id_dir = "/dev/input/by-id"

      Dir.entries(by_id_dir).filter_map do |name|
        link_path = File.join(by_id_dir, name)
        next unless File.symlink?(link_path)
        next unless name.include?("-kbd")
        target = File.realpath(link_path) rescue nil
        event_nodes.include?(target) ? name : nil
      end.sort
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

      input_size_bits = 0
      input_count = 0

      byte_length = ->(bits) { (bits + 7) / 8 }

      update_max = lambda do
        if report_bits > 0
          length = byte_length.call(report_bits)
          length += 1 if current_report_id != 0 || has_report_ids
          max_report_size = length if length > max_report_size
        end
      end

      while i < bytes.size
        b = bytes[i]; i += 1

        if b == 0xFE
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
        puts "[INFO] Using sysfs report descriptor: #{sysfs_path}"
        calc_report_length(sysfs_path)
      else
        calc_report_length(path)
      end
    end

    private

    # Helper: Find /dev/input/event* nodes in a directory
    def self.find_event_nodes(dir)
      Dir.entries(dir)
        .select { |name| name.start_with?("event") }
        .map { |event| "/dev/input/#{event}" }
    end
  end
end
