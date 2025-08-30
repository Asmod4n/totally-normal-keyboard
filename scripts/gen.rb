#!/usr/bin/env ruby

header = "/usr/include/linux/input-event-codes.h"
out    = File.join(__dir__, "..", "src", "lookup_key_name.h")

lines = []
lines << "static const char *lookup_key_name(int code) {"
lines << "  switch (code) {"

File.foreach(header) do |line|
  if line =~ /^#define\s+KEY_([A-Z0-9_]+)\s+(\d+)/
    name = Regexp.last_match(1)
    next if name =~ /\A[A-Z]\z/ || name =~ /\A[0-9]+\z/
    lines << "    case KEY_#{name}: return \"#{name.downcase}\";"
  end
end

lines << "    default: return NULL;"
lines << "  }"
lines << "}"

File.write(out, lines.join("\n"))
puts "✅ generated #{out}"
