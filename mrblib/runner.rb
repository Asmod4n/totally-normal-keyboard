class Tnk
  class << self
    attr_accessor :instance
    def runner
      self.instance ||= new
    end

    def run(&block)
      runner.run(&block)
    end

    def close
      runner.close
      self.instance = nil
    end
  end
end
