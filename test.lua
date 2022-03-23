local base64 = require('base64')
local msgpack = require('msgpack')

local x = assert(msgpack.encode(nil, true, false, 'helloäöü😀 valid UTF8', math.pi, 2.5, {1, 2, 3}, {foo=1, bar=2}))
print(msgpack.decode(x, 1, 2))


local a = base64.encode('Ma')
print(a, base64.decode(a))
