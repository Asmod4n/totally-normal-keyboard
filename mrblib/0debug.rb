if $DEBUG
  def debug_puts(*args)
    puts(*args)
  end
else
  def debug_puts(*args)
  end
end
