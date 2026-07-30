MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-pack'
  spec.add_dependency 'mruby-cbor'
  spec.add_dependency 'mruby-lmdb'
  spec.add_dependency 'mruby-argon2'
  spec.add_dependency 'mruby-libhydrogen'
  spec.add_dependency 'mruby-secure-wipe-memory'

  spec.bins = %w(tnk)
end
