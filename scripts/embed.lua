-- Generates a C source file with a 0 terminated string containing text file
-- data. Usage: SCRIPT VARNAME FILE OUTPUT
local varname = arg[1]
local input = io.open(arg[2], 'rb'):read('*a')
local output = io.open(arg[3], 'wb')

output:write([[
const char ]] .. varname .. [[[] = {]])

output:write('\n  ')
for i = 1, #input do
  if i == 0 then
    error('file contains 0')
  end
  output:write(string.byte(input, i)..', ')
  -- wrap on every 10 bytes
  if i % 10 == 0 then
    output:write('\n  ')
  end
end

output:write('\n0};')
