# Various Lua Modules

## Overview

| File | Description |
| --- | --- |
| sts_base64.c | Base64 encoder/decoder |
| sts_msgpack.c | MessagePack encoder/decoder |

## sts_msgpack.c
```lua
local msgpack = require('msgpack')

-- packs all arguments to a valid messagepack binary
local binary = assert(msgpack.encode(1, math.pi, 2.5, 'Hello World', { 1, 2, 3 }, { foo = 'bar', bar = 'foo'}))

-- unpack all values from the binary
print(msgpack.decode(binary))
```
