MRuby::Build.new('debug') do |conf|
  conf.toolchain :clang
  conf.enable_debug
  #conf.enable_test
  conf.gembox 'full-core'
  conf.enable_sanitizer "address,leak,undefined"

  conf.cc.flags << '-Og' << '-g' << '-fno-omit-frame-pointer' << '-march=armv8-a'
  conf.cxx.flags << '-Og' << '-g' << '-std=c++20' << '-fno-omit-frame-pointer' << '-march=armv8-a'

  conf.gem File.expand_path(File.dirname(__FILE__))
  conf.gem mgem: 'mruby-io-uring'
  conf.gem mgem: 'mruby-c-ext-helpers'
end

MRuby::Build.new('release') do |conf|
  conf.toolchain :clang
  conf.gembox 'full-core'

  conf.cc.flags << '-Os' << '-flto' << '-ffunction-sections' << '-fdata-sections' << '-march=armv8-a'
  conf.cxx.flags << '-Os' << '-std=c++20' << '-flto' << '-ffunction-sections' << '-fdata-sections' << '-march=armv8-a'
  conf.linker.flags << '-Wl,--gc-sections' << '-Wl,--threads=4' << '-flto' << '-fuse-ld=lld'

  conf.gem File.expand_path(File.dirname(__FILE__))
  conf.gem mgem: 'mruby-io-uring'
  conf.gem mgem: 'mruby-c-ext-helpers'
end
