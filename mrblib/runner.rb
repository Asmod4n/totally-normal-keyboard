class Tnk
  class << self
    attr_accessor :instance
    def runner
      self.instance ||= new
    end

    def setup_root
      runner.setup_root
    end

    def setup_user
      runner.setup_user
    end

    def after_setup
      runner.after_setup
    end

    def run
      runner.run
    end

    def close
      runner.close
      self.instance = nil
    end
  end
end
