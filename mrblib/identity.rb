class Tnk
  # Randomizes the USB serial number and the NCM MAC addresses on first
  # start and persists them, so every unit gets its own identity instead
  # of every tnk build shipping identical hardcoded values.
  module Identity
    extend self

    HEX = "0123456789abcdef"

    def load_or_create(share_dir)
      path = File.join(share_dir, "identity")
      if File.exist?(path)
        parse(File.open(path, "r") { |f| f.read })
      else
        identity = generate
        File.open(path, "w") { |f| f.write(serialize(identity)) }
        identity
      end
    end

    private

    def parse(data)
      serial, dev_addr, host_addr = data.split("\n")
      { serial: serial, dev_addr: dev_addr, host_addr: host_addr }
    end

    def serialize(identity)
      "#{identity[:serial]}\n#{identity[:dev_addr]}\n#{identity[:host_addr]}\n"
    end

    def generate
      { serial: random_serial, dev_addr: random_mac, host_addr: random_mac }
    end

    def random_serial
      random_bytes(8).bytes.map { |b| hex_byte(b) }.join
    end

    # Locally administered, unicast: first byte's U/L bit set, I/G bit clear.
    def random_mac
      bytes = random_bytes(6).bytes
      bytes[0] = (bytes[0] & 0xFE) | 0x02
      bytes.map { |b| hex_byte(b) }.join(":")
    end

    def random_bytes(n)
      File.open("/dev/urandom", "rb") { |f| f.read(n) }
    end

    def hex_byte(b)
      HEX[(b >> 4) & 0xF] + HEX[b & 0xF]
    end
  end
end
