class Tnk
  module Hotkeys
    @@hotkeys = {}
    def self.on_hotkey(*args, &blk)
      raise "Kein Block übergeben" unless blk
      report = build_report(*args)
      @@hotkeys[report] = blk
    end

    def self.handle_hid_report(buf)
      @@hotkeys[buf]&.call
    end
  end
end
