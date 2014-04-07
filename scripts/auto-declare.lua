-- Reads a C file, parses function definitions and emits a header containing
-- public declarations and injects static declarations at the top of the file
-- For lpeg documentation, see http://www.inf.puc-rio.br/~roberto/lpeg/

lpeg = require('lpeg')
re = require('re')

-- grammar
linefeed = lpeg.P('\n')
filler = (linefeed + lpeg.S('\t ')) ^ 0
any = re.compile('.')
not_linefeed = (any - linefeed)
doxy_line = linefeed * lpeg.P('///') * (not_linefeed ^ 1)
doxy_block = doxy_line ^ 1
c_typeid = lpeg.S()
c_static = lpeg.P('static')
c_params = lpeg.P()

c_function = (
  filler *
  (c_static ^ 0) *
  filler *
  (lpeg.alpha ^ 1) 

node = lpeg.C(doxy_block + any)


if table.getn(arg) ~= 2 then
  print('Need exactly two arguments')
  os.exit(1)
end

inputf = assert(io.open(arg[1], 'rb'))
input = inputf:read('*all')
inputf.close()

len = string.len(input)
pos = 1

while pos <= len do
  match = lpeg.match(node, input, pos)
  matchlen = string.len(match)
  if (matchlen > 1) then
    print(match)
  end
  pos = pos + matchlen
end
