MRuby::Gem::Specification.new('totally-normal-keyboard') do |spec|
  spec.license = 'AGPL-3.0'
  spec.authors = 'Hendrik Beskow'
  spec.add_dependency 'mruby-io-uring'
  spec.add_dependency 'mruby-signal'
  spec.cxx.flags << '-std=c++20'

  spec.bins = %w(tnk)
end
