require 'rake'
require 'fileutils'

MRUBY_CONFIG_PATH = File.expand_path(ENV["MRUBY_CONFIG"] || "build_config.rb")
PREFIX = ENV['PREFIX'] || '/usr/local'
file :mruby do
  unless File.directory?('mruby')
    sh "git clone --depth=1 https://github.com/mruby/mruby.git"
  end
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
end

desc "install release and debug binaries plus shared data"
task :install => :compile do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')

  FileUtils.mkdir_p(bindir)
  FileUtils.mkdir_p(sharedir)

  # Install release build as 'tnk'
  release_bin = File.join('mruby', 'build', 'release', 'bin', 'tnk')
  if File.exist?(release_bin)
    FileUtils.install(release_bin, File.join(bindir, 'tnk'), mode: 0755)
  else
    warn "⚠️ Release binary not found at #{release_bin}"
  end

  # Install debug build as 'tnk-debug'
  debug_bin = File.join('mruby', 'build', 'debug', 'bin', 'tnk')
  if File.exist?(debug_bin)
    FileUtils.install(debug_bin, File.join(bindir, 'tnk-debug'), mode: 0755)
  else
    warn "⚠️ Debug binary not found at #{debug_bin}"
  end

  # Install shared files
  FileUtils.cp_r('share/.', sharedir)

  puts "✅ Installed release as 'tnk' and debug as 'tnk-debug' to #{bindir}"
end

desc "uninstall binaries and shared data"
task :uninstall do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')

  %w[tnk tnk-debug].each do |bin|
    path = File.join(bindir, bin)
    if File.exist?(path)
      FileUtils.rm_f(path)
      puts "🗑️ Removed #{path}"
    end
  end

  if Dir.exist?(sharedir)
    FileUtils.rm_rf(sharedir)
    puts "🗑️ Removed #{sharedir}"
  end
end


task :default => :test
