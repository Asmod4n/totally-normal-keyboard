class Tnk
  module Hidg
    extend self
    @@hid_map = []

    GADGET = "/sys/kernel/config/usb_gadget/tnk"

    def hid_map
      @@hid_map
    end

    def setup
      @@hid_map.clear
      if File.exist?("#{GADGET}/UDC")
        udc = read_first_line("#{GADGET}/UDC")
        if udc.delete(" \t\r\n\f\v") != ""
          stop
        end
      end

      sh_silent "modprobe -r dwc2"
      sh_silent "modprobe dwc2"
      sh_silent "modprobe libcomposite"

      exe_path = File.realpath("/proc/self/exe")
      base_dir = File.realpath(File.dirname(exe_path))
      share_dir = File.realpath(File.join(base_dir, "../share/totally-normal-keyboard"))
      mkdir_p(share_dir)
      identity = Tnk::Identity.load_or_create(share_dir)

      mkdir_p(GADGET)
      Dir.chdir(GADGET) do
        file_write("idVendor",     "0x1d6b")
        file_write("idProduct",    "0x0104")
        file_write("bcdDevice",    "0x0100")
        file_write("bcdUSB",       "0x0200")

        mkdir_p("strings/0x409")
        file_write("strings/0x409/serialnumber",  identity[:serial])
        file_write("strings/0x409/manufacturer",  "Hendrik")
        file_write("strings/0x409/product",       "Totally Normal Keyboard")

        mkdir_p("configs/c.1/strings/0x409")
        file_write("configs/c.1/strings/0x409/configuration", "tnk")
        file_write("configs/c.1/MaxPower", "250")

        mkdir_p("functions/mass_storage.usb0")

        disk_img = File.join(share_dir, "disk.img")
        unless File.exist?(disk_img)
          bytes = half_of_available(share_dir)
          debug_puts "📦 Creating disk.img (#{bytes / 1048576} MiB)..."
          sh "truncate -s #{bytes} #{disk_img}"
          sh "mkfs.vfat #{disk_img}"
        else
          debug_puts "✅ disk.img exists – skipping creation."
        end
        file_write("functions/mass_storage.usb0/stall", "0")
        file_write("functions/mass_storage.usb0/lun.0/file", disk_img)
        file_write("functions/mass_storage.usb0/lun.0/removable", "1")
        ln_s("functions/mass_storage.usb0", "configs/c.1/mass_storage.usb0")

        mkdir_p("functions/ncm.usb0")
        file_write("functions/ncm.usb0/dev_addr", identity[:dev_addr])
        file_write("functions/ncm.usb0/host_addr", identity[:host_addr])
        ln_s("functions/ncm.usb0", "configs/c.1/ncm.usb0")

        debug_puts "🧠 Scanning for HID report descriptors..."
        hid_index = 0
        each_hidraw_report_descriptor do |hidraw|
          length = Tnk::Hidraw.calc_report_length_smart(hidraw)
          debug_puts "🔧 Adding HID function #{hid_index} (report_length=#{length})..."
          func_dir = "functions/hid.usb#{hid_index}"
          mkdir_p(func_dir)
          file_write("#{func_dir}/protocol", "0")
          file_write("#{func_dir}/subclass", "0")
          file_write("#{func_dir}/report_length", length.to_s)
          File.open("#{func_dir}/report_desc", "wb") do |out|
            File.open(hidraw, "rb") { |inp| out.write(inp.read) }
          end
          ln_s(func_dir, "configs/c.1/hid.usb#{hid_index}")
          hidraw_name = File.basename(File.dirname(File.dirname(hidraw)))
          hidraw_dev  = "/dev/#{hidraw_name}"
          @@hid_map << [hidraw_dev, "/dev/hidg#{hid_index}"]
          hid_index += 1
        end

        udc_name = sh_capture("ls /sys/class/udc").split("\n").first
        file_write("UDC", udc_name)

        unless sh_silent("ip link set usb0 up")
          debug_puts "⚠️  Could not bring up usb0 (ip failed)"
        end
        sh_silent("ip -6 addr add fe80::1 dev usb0")
      end
    end

    def stop
      original_pwd = Dir.pwd
      debug_puts "🛑 Cleaning up USB gadget tnk..."
      if File.exist?("#{GADGET}/UDC")
        file_write("#{GADGET}/UDC", "")
      end

      Dir.chdir(GADGET)
      debug_puts "🧹 Removing config symlinks..."
      remove_symlinks("configs/c.1")
      Dir.rmdir("configs/c.1/strings/0x409")
      Dir.rmdir("configs/c.1")

      debug_puts "🧹 Removing functions..."
      remove_directories("functions")

      Dir.rmdir("strings/0x409")
      Dir.chdir("..")
      Dir.rmdir("tnk")
      debug_puts "✅ Gadget tnk removed."
    ensure
      Dir.chdir(original_pwd)
    end

    private

    def read_first_line(path)
      File.open(path) { |f| f.gets.to_s.chomp }
    end

    def file_write(path, content, mode = "w")
      File.open(path, mode) { |f| f.write(content) }
    end

    def mkdir_p(path)
      Dir.mkdir(path) unless File.directory?(path)
    rescue Errno::ENOENT
      parent = File.dirname(path)
      mkdir_p(parent)
      retry
    rescue Errno::EEXIST
    end

    def ln_s(target, link)
      File.symlink(target, link)
    rescue Errno::EEXIST
    end

    def rm_rf(path)
      if File.symlink?(path) || File.file?(path)
        File.delete(path)
      elsif File.directory?(path)
        Dir.open(path) do |d|
          while entry = d.read
            next if entry == "." || entry == ".."
            rm_rf(File.join(path, entry))
          end
        end
        Dir.rmdir(path)
      end
    end

    def sh(cmd)
      debug_puts "→ #{cmd}"
      IO.popen(cmd) do |io|
        while line = io.gets
          debug_puts line
        end
      end
      status = $?
      raise GadgetError, "Command failed: #{cmd}" unless status == 0
    end

    def sh_silent(cmd)
      IO.popen(cmd) { |io| io.read }
      $? == 0
    end

    def sh_capture(cmd)
      IO.popen(cmd) { |io| io.read }.chomp
    end

    # Half the free space on the volume the image sits on, in bytes.
    #
    # truncate(1) is ftruncate(2) with a name on it: it sets the length
    # and writes nothing, so the image is SPARSE and occupies only the
    # blocks that end up holding data. The old `dd if=/dev/zero bs=128M
    # count=1` wrote 128 MiB of actual zeroes to the card - slow, a whole
    # card's worth of wear for an empty filesystem, and a size fixed at
    # 128 MiB whatever the card was.
    #
    # So the number here is a CEILING the host is shown, not space taken
    # from the Pi. Half rather than all of it because the host must not
    # be able to fill the root filesystem by copying files onto what
    # looks like a USB stick.
    #
    # df -Pk, not df --output=avail: -P is POSIX, one line per
    # filesystem in fixed columns, and parses identically under the
    # busybox df on a minimal Pi image, where --output does not exist.
    # Column 4 is available 1024-byte blocks.
    #
    # Rounded down to a MiB so mkfs.vfat gets a whole number of clusters
    # to work with. Note FAT32 tops out at 2 TiB and mkfs.vfat will
    # refuse past that - not reachable on an SD card, but it is the limit
    # if this ever runs somewhere with a real disk under it.
    def half_of_available(dir)
      line = sh_capture("df -Pk #{dir}").split("\n").last.to_s
      avail_kb = line.split(" ")[3].to_i
      raise GadgetError, "cannot read free space for #{dir}" if avail_kb <= 0
      bytes = (avail_kb / 2) * 1024
      bytes - (bytes % 1048576)
    end

    def each_hidraw_report_descriptor
      base = "/sys/class/hidraw"
      Dir.open(base) do |d|
        while entry = d.read
          next if entry == "." || entry == ".."
          next unless entry.start_with?("hidraw")
          path = "#{base}/#{entry}/device/report_descriptor"
          next unless File.exist?(path)
          unless Tnk::DeviceFilter.forward?(path)
            debug_puts "⛔ #{Tnk::DeviceFilter.describe(path)}"
            next
          end
          debug_puts "✅ #{Tnk::DeviceFilter.describe(path)}"
          yield path
        end
      end
    end

    def remove_symlinks(dir)
      Dir.open(dir) do |d|
        while entry = d.read
          next if entry == "." || entry == ".."
          full = "#{dir}/#{entry}"
          File.delete(full) if File.symlink?(full)
        end
      end
    end

    def remove_directories(dir)
      Dir.open(dir) do |d|
        while entry = d.read
          next if entry == "." || entry == ".."
          full = "#{dir}/#{entry}"
          Dir.rmdir(full) if File.directory?(full)
        end
      end
    end
  end
end
