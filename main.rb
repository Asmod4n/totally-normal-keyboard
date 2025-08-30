Tnk.run do |timestamp, name, action, pressed|
  if pressed.include?(:leftctrl) && pressed.include?("c")
    puts "CTRL + C wird gerade gehalten!"
  end
  true
end
