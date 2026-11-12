# encoding: ascii-8bit

# Create the overall gemspec
Gem::Specification.new do |s|
  s.name = 'openc3-cosmos-att-app'
  s.summary = 'OpenC3 COSMOS plugin for the ATT_APP cFS application'
  s.description = <<-EOF
    openc3-cosmos-att-app plugin for deployment to OpenC3
  EOF
  s.license = 'Apache-2.0'
  s.authors = ["muta4613"]
  s.email = []
  s.homepage = 'https://github.com/muta4613/cFS'
  s.platform = Gem::Platform::RUBY

  if ENV['VERSION']
    s.version = ENV['VERSION'].dup
  else
    time = Time.now.strftime("%Y%m%d%H%M%S")
    s.version = '1.0.0' + ".#{time}"
  end
  s.files = Dir.glob("{targets,lib,tools,microservices}/**/*") + %w(Rakefile README.md plugin.txt)
end
