require 'rake'
require_relative 'common'

MRUBY_CONFIG_PATH = File.expand_path(ENV["MRUBY_CONFIG"] || "build_config.rb")
PREFIX = ENV['PREFIX'] || '/usr/local'

file TOOLCHAIN_DIR do
  unless File.directory?(TOOLCHAIN_DIR)
    sh "git submodule update --init --recursive #{TOOLCHAIN_DIR}"
  end
end

desc "Build and install musl cross toolchain"
task :toolchain => TOOLCHAIN_DIR do
  sh "make -C #{TOOLCHAIN_DIR} TARGET=#{TOOLCHAIN_TARGET} OUTPUT=#{TOOLCHAIN_OUTPUT} -j$(nproc)"
  build_dir = File.join(TOOLCHAIN_DIR, "build", TOOLCHAIN_TARGET)
  if Dir.exist?(build_dir)
    Dir.chdir(build_dir) do
      sh "make OUTPUT=#{TOOLCHAIN_OUTPUT} -j$(nproc) install"
    end
  else
    abort "❌ Expected build directory #{build_dir} not found"
  end
end

file :mruby do
  sh "git clone --depth=1 https://github.com/mruby/mruby.git" unless File.directory?('mruby')
end

desc "compile binary"
task :compile => :mruby do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake all"
  end
end

desc "test"
task :test => :mruby do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake all test"
  end
end

desc "cleanup"
task :clean do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake deep_clean"
  end
  FileUtils.rm_rf(TOOLCHAIN_OUTPUT)
end

desc "install release and debug binaries plus shared data"
task :install => :compile do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')
  FileUtils.mkdir_p(bindir)
  FileUtils.mkdir_p(sharedir)

  release_bin = File.join('mruby', 'build', 'release', 'bin', 'tnk')
  debug_bin   = File.join('mruby', 'build', 'debug', 'bin', 'tnk')

  FileUtils.install(release_bin, File.join(bindir, 'tnk'), mode: 0755) if File.exist?(release_bin)
  FileUtils.install(debug_bin,   File.join(bindir, 'tnk-debug'), mode: 0755) if File.exist?(debug_bin)

  FileUtils.cp_r('share/.', sharedir)
end

desc "uninstall binaries and shared data"
task :uninstall do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')
  %w[tnk tnk-debug].each { |bin| FileUtils.rm_f(File.join(bindir, bin)) }
  FileUtils.rm_rf(sharedir)
end

task :default => :test
