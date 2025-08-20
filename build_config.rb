MRuby::Build.new do |conf|
  require 'rbconfig'

  host_os   = RbConfig::CONFIG['host_os']
  is_windows = host_os =~ /mswin|mingw|cygwin/
  is_macos   = host_os =~ /darwin/

  conf.toolchain :clang

  # Common flags
  conf.enable_debug
  #conf.enable_test
  conf.cxx.flags << '-fno-omit-frame-pointer' << '-g' << '-ggdb'
  conf.cc.flags  << '-fno-omit-frame-pointer' << '-g' << '-ggdb'

  unless is_windows
    #conf.enable_sanitizer "address,undefined"
  end

  if is_macos
    clang_bin = `which clang`.strip
    clang_ver = `#{clang_bin} --version`.lines.first.strip
    if clang_ver.include?("Apple clang")
      puts "🍏 Detected Apple Clang — using macOS SDK headers"
      sdk_path    = `xcrun --show-sdk-path`.strip
      std_include = "#{sdk_path}/usr/include/c++/v1"
      if File.directory?(std_include)
        conf.cxx.include_paths << std_include
        conf.cxx.flags << "-isystem" << std_include
        conf.cxx.flags << "-isysroot" << sdk_path
      else
        puts "⚠️ Missing macOS libc++ headers at #{std_include}"
      end
    else
      puts "🍺 Detected Homebrew/LLVM Clang — adjusting include paths"
      llvm_include = `llvm-config --includedir 2>/dev/null`.strip
      if !llvm_include.empty? && File.directory?(llvm_include)
        conf.cxx.include_paths << llvm_include
        conf.cxx.flags << "-isystem" << llvm_include
      else
        puts "⚠️ Could not locate LLVM headers automatically"
      end
    end
  end

  conf.gembox 'full-core'
  conf.gem File.expand_path(File.dirname(__FILE__))
end
