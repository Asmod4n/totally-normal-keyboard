MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-pack'
  spec.add_dependency 'mruby-time'
  spec.add_dependency 'mruby-set'

  spec.bins = %w(tnk)
end
