class Tnk
  module Hotkeys
    @@hotkeys = []
    def self.on_hotkey(*args, &blk)
      raise "Kein Block übergeben" unless blk
      report = build_report(*args)
      @@hotkeys << {report:report,callback:blk}
    end

    def self.handle_hid_report(buf)
      triggered = false
      @@hotkeys.each do |hk|
        if hk[:report] == buf
          hk[:callback].call
          triggered = true
        end
      end
      triggered
    end
  end
end
