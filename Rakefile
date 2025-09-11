require 'rake'
require_relative 'common'

MRUBY_CONFIG_PATH = File.expand_path(ENV["MRUBY_CONFIG"] || "build_config.rb")
PREFIX = ENV['PREFIX'] || '/usr/local'

file :mruby do
  sh "git clone --depth=1 https://github.com/mruby/mruby.git" unless File.directory?('mruby')
end

task :compile => :mruby do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake all"
  end
end

task :test => :mruby do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake all test"
  end
end

task :clean do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake clean"
  end
end

task :deep_clean do
  Dir.chdir("mruby") do
    ENV["MRUBY_CONFIG"] = MRUBY_CONFIG_PATH
    sh "rake deep_clean"
  end
  FileUtils.rm_rf(TOOLCHAIN_OUTPUT)
  makefile_path = File.join(TOOLCHAIN_DIR, "Makefile")
  sh "make -C #{TOOLCHAIN_DIR} distclean" if File.exist?(makefile_path)
end

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

task :uninstall do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')
  %w[tnk tnk-debug].each { |bin| FileUtils.rm_f(File.join(bindir, bin)) }
  FileUtils.rm_rf(sharedir)
end

task :default => :test
