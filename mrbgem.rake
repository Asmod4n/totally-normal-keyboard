MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-pack'
  spec.add_dependency 'mruby-simplemsgpack' do |dep|
    dep.cc.defines  << 'MRB_MSGPACK_SYMBOLS'
    dep.cxx.defines << 'MRB_MSGPACK_SYMBOLS'
  end

  spec.bins = %w(tnk)
end
