class Tnk
  class << self
    attr_accessor :instance
    def runner
      self.instance ||= new
    end

    def setup
      runner.setup
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
