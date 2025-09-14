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

  unitdir = File.join(PREFIX, 'lib', 'systemd', 'system')
  FileUtils.mkdir_p(unitdir)
  unit_content = <<~UNIT
    [Unit]
    Description=Totally Normal Keyboard
    After=multi-user.target
    Wants=multi-user.target

    [Service]
    Type=simple
    ExecStart=#{File.join(PREFIX, 'sbin', 'tnk')}
    Restart=on-failure
    RestartSec=3s
    SuccessExitStatus=143

    [Install]
    WantedBy=multi-user.target
  UNIT
  File.write(File.join(unitdir, 'tnk.service'), unit_content)
  sh 'systemctl daemon-reload'

  udev_rule_path = '/etc/udev/rules.d/99-tnk-hidraw.rules'
  udev_rule_content = <<~RULE
    SUBSYSTEM=="hidraw", ACTION=="add", RUN+="/usr/bin/systemctl restart tnk.service"
    SUBSYSTEM=="hidraw", ACTION=="remove", RUN+="/usr/bin/systemctl restart tnk.service"
  RULE
  File.write(udev_rule_path, udev_rule_content)

  sh 'udevadm control --reload-rules'
  sh 'udevadm trigger --subsystem-match=hidraw'
end

task :uninstall do
  bindir   = File.join(PREFIX, 'sbin')
  sharedir = File.join(PREFIX, 'share', 'totally-normal-keyboard')
  unitfile = File.join(PREFIX, 'lib', 'systemd', 'system', 'tnk.service')
  udev_rule_path = '/etc/udev/rules.d/99-tnk-hidraw.rules'

  %w[tnk tnk-debug].each { |bin| FileUtils.rm_f(File.join(bindir, bin)) }
  FileUtils.rm_rf(sharedir)

  sh 'systemctl disable tnk.service' rescue nil
  FileUtils.rm_f(unitfile)
  sh 'systemctl daemon-reload'

  FileUtils.rm_f(udev_rule_path)
  sh 'udevadm control --reload-rules'
  sh 'udevadm trigger --subsystem-match=hidraw'
end


task :default => :test
