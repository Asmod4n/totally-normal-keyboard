MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.cc.include_paths << 'include'
  spec.cxx.include_paths << 'include'
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-pack'
  spec.add_dependency 'mruby-simplemsgpack'

  spec.bins = %w(tnk)
end
