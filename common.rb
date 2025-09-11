require 'fileutils'
require 'rbconfig'

# Always anchor to the project root (directory containing this file)
PROJECT_ROOT     = File.expand_path(__dir__)
TOOLCHAIN_TARGET = 'aarch64-linux-musl'
TOOLCHAIN_DIR    = File.join(PROJECT_ROOT, 'deps', 'musl-cross-make')
TOOLCHAIN_OUTPUT = File.join(PROJECT_ROOT, 'toolchains', TOOLCHAIN_TARGET)

# Cross-platform "find executable in PATH"
def find_executable(cmd)
  exts = ENV['PATHEXT'] ? ENV['PATHEXT'].split(';') : ['']
  ENV['PATH'].split(File::PATH_SEPARATOR).each do |path|
    exts.each do |ext|
      exe = File.join(path, "#{cmd}#{ext}")
      return exe if File.executable?(exe) && !File.directory?(exe)
    end
  end
  nil
end

def toolchain_in_path?(target)
  !!find_executable("#{target}-gcc")
end

def find_toolchain_bin(target, local_path)
  if (exe = find_executable("#{target}-gcc"))
    return File.dirname(exe)
  end
  local_bin = File.join(local_path, 'bin')
  return local_bin if File.exist?(File.join(local_bin, "#{target}-gcc"))
  nil
end

def ensure_toolchain
  return if toolchain_in_path?(TOOLCHAIN_TARGET) ||
            File.exist?(File.join(TOOLCHAIN_OUTPUT, 'bin', "#{TOOLCHAIN_TARGET}-gcc"))

  # Fetch submodule if missing
  unless File.directory?(TOOLCHAIN_DIR)
    system("git -C #{PROJECT_ROOT} submodule update --init --recursive deps/musl-cross-make") \
      or abort "❌ Failed to fetch submodule"
  end

  # Build
  system("make -C #{TOOLCHAIN_DIR} TARGET=#{TOOLCHAIN_TARGET} OUTPUT=#{TOOLCHAIN_OUTPUT} -j$(nproc)") \
    or abort "❌ Toolchain build failed"

  # Install — detect actual build dir
  possible_build_dirs = [
    File.join(TOOLCHAIN_DIR, "build", TOOLCHAIN_TARGET),
    File.join(TOOLCHAIN_DIR, "build", "local", TOOLCHAIN_TARGET)
  ]
  build_dir = possible_build_dirs.find { |d| Dir.exist?(d) }
  abort "❌ Expected build directory not found" unless build_dir

  Dir.chdir(build_dir) do
    system("make OUTPUT=#{TOOLCHAIN_OUTPUT} install") or abort "❌ Toolchain install failed"
  end
end
