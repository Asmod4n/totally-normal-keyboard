require_relative 'common'

prefix   = ENV['PREFIX'] || '/usr/local'
host_cpu = RbConfig::CONFIG['host_cpu']
host_os  = RbConfig::CONFIG['host_os']

if host_cpu == 'aarch64' && host_os =~ /linux/i
  if ENV["TNK_DEBUG"]
    MRuby::Build.new('debug') do |conf|
      conf.toolchain :clang
      conf.enable_debug
      conf.gembox 'full-core'
      conf.enable_sanitizer "address,undefined,leak"
      conf.cc.flags  << '-Og' << '-g' << '-fno-omit-frame-pointer'
      conf.cxx.flags << '-Og' << '-g' << '-std=c++20' << '-fno-omit-frame-pointer'
      conf.cc.defines  << %Q{TNK_PREFIX=\\"#{prefix}\\"}
      conf.cxx.defines << %Q{TNK_PREFIX=\\"#{prefix}\\"}
      conf.gem File.expand_path(File.dirname(__FILE__))
    end
  end

  MRuby::Build.new do |conf|
    conf.toolchain :clang
    conf.gembox 'full-core'
    conf.cc.flags  << '-Os' << '-ffunction-sections' << '-fdata-sections'
    conf.cxx.flags << '-Os' << '-std=c++20' << '-ffunction-sections' << '-fdata-sections'
    conf.cc.flags  << '-g0'
    conf.cxx.flags << '-g0'
    conf.linker.flags << '-Wl,--strip-debug'
    conf.linker.flags << '-static'
    conf.cc.flags     << '-static'
    conf.cxx.flags    << '-static'
    conf.cc.defines  << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.cxx.defines << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.gem File.expand_path(File.dirname(__FILE__))
  end
else
  MRuby::CrossBuild.new('aarch64-musl-gcc') do |conf|
    toolchain :gcc
    next if Rake.application.top_level_tasks.any? { |t| t =~ /\A(clean|deep_clean)\z/ }

    ensure_toolchain
    toolchain_bin_path = find_toolchain_bin(TOOLCHAIN_TARGET, TOOLCHAIN_OUTPUT)
    abort "No #{TOOLCHAIN_TARGET} toolchain found or built" unless toolchain_bin_path

    conf.host_target    = TOOLCHAIN_TARGET
    conf.cc.command     = File.join(toolchain_bin_path, "#{TOOLCHAIN_TARGET}-gcc")
    conf.cxx.command    = File.join(toolchain_bin_path, "#{TOOLCHAIN_TARGET}-g++")
    conf.linker.command = conf.cxx.command

    conf.cc.flags  << '-Os' << '-ffunction-sections' << '-fdata-sections'
    conf.cxx.flags << '-Os' << '-std=c++2a' << '-ffunction-sections' << '-fdata-sections'

    conf.linker.flags << "-static"
    conf.cc.flags     << "-static"
    conf.cxx.flags    << "-static"

    conf.cc.flags  << '-g0'
    conf.cxx.flags << '-g0'
    conf.linker.flags << '-Wl,--strip-debug'


    conf.gembox 'full-core'
    conf.cc.defines  << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.cxx.defines << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.gem File.expand_path(File.dirname(__FILE__))
  end

  MRuby::CrossBuild.new('aarch64-musl-gcc-debug') do |conf|
    toolchain :gcc
    next if Rake.application.top_level_tasks.any? { |t| t =~ /\A(clean|deep_clean)\z/ }

    ensure_toolchain
    toolchain_bin_path = find_toolchain_bin(TOOLCHAIN_TARGET, TOOLCHAIN_OUTPUT)
    abort "No #{TOOLCHAIN_TARGET} toolchain found or built" unless toolchain_bin_path

    conf.host_target    = TOOLCHAIN_TARGET
    conf.cc.command     = File.join(toolchain_bin_path, "#{TOOLCHAIN_TARGET}-gcc")
    conf.cxx.command    = File.join(toolchain_bin_path, "#{TOOLCHAIN_TARGET}-g++")
    conf.linker.command = conf.cxx.command

    conf.cc.flags  << '-Og' << '-g' << '-fno-omit-frame-pointer' << '-static'
    conf.cxx.flags << '-Og' << '-g' << '-std=c++2a' << '-fno-omit-frame-pointer' << '-static'
    conf.linker.flags << '-static'

    conf.gembox 'full-core'
    conf.cc.defines  << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.cxx.defines << %Q{TNK_PREFIX=\\"#{prefix}\\"}
    conf.gem File.expand_path(File.dirname(__FILE__))
  end
end
