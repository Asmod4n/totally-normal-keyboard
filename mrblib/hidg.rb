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
          original_pwd = Dir.pwd
          stop
          Dir.chdir(original_pwd)
        end
      end

      sh "modprobe -r dwc2"
      sh "modprobe dwc2"
      sh "modprobe libcomposite"
      mkdir_p(GADGET)
      Dir.chdir(GADGET) do
        file_write("idVendor",     "0x1d6b")
        file_write("idProduct",    "0x0104")
        file_write("bcdDevice",    "0x0100")
        file_write("bcdUSB",       "0x0200")

        mkdir_p("strings/0x409")
        file_write("strings/0x409/serialnumber",  "1234567890")
        file_write("strings/0x409/manufacturer",  "Hendrik")
        file_write("strings/0x409/product",       "Totally Normal Keyboard")

        mkdir_p("configs/c.1/strings/0x409")
        file_write("configs/c.1/strings/0x409/configuration", "tnk")
        file_write("configs/c.1/MaxPower", "250")

        mkdir_p("functions/mass_storage.usb0")
        share_dir = File.join(Tnk::PREFIX, "share", "totally-normal-keyboard")
        mkdir_p(share_dir)

        disk_img = File.join(share_dir, "disk.img")
        unless File.exist?(disk_img)
          debug_puts "📦 Creating disk.img..."
          sh "dd if=/dev/zero of=#{disk_img} bs=1M count=128"
          sh "mkfs.vfat #{disk_img}"
        else
          debug_puts "✅ disk.img exists – skipping creation."
        end
        file_write("functions/mass_storage.usb0/stall", "0")
        file_write("functions/mass_storage.usb0/lun.0/file", disk_img)
        file_write("functions/mass_storage.usb0/lun.0/removable", "1")
        ln_s("functions/mass_storage.usb0", "configs/c.1/mass_storage.usb0")

        mkdir_p("functions/ncm.usb0")
        file_write("functions/ncm.usb0/dev_addr", "02:12:34:56:78:90")
        file_write("functions/ncm.usb0/host_addr", "02:98:76:54:32:10")
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

        unless sh_silent("ip link set usb0 up") || sh_silent("ifconfig usb0 up")
          debug_puts "⚠️  Could not bring up usb0 (ip/ifconfig failed)"
        end
        sh_silent("ip -6 addr add fe80::1 dev usb0")
      end
    end

    def stop
      debug_puts "🛑 Cleaning up USB gadget tnk..."
      if File.exist?("#{GADGET}/UDC")
        file_write("#{GADGET}/UDC", "")
      end

      unless File.directory?(GADGET)
        raise GadgetError, "Gadget directory not found: #{GADGET}"
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

    def each_hidraw_report_descriptor
      base = "/sys/class/hidraw"
      Dir.open(base) do |d|
        while entry = d.read
          next if entry == "." || entry == ".."
          next unless entry.start_with?("hidraw")
          path = "#{base}/#{entry}/device/report_descriptor"
          yield path if File.exist?(path)
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
