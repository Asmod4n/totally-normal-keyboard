MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-pack'
  spec.add_dependency 'mruby-time'
  spec.add_dependency 'mruby-set'

  sh File.join(__dir__, 'scripts', 'gen_keymap_h.sh')

  spec.bins = %w(tnk)
end
