# Various Lua Modules

## Overview
- written in C99 (can be compiled with ANSI-C compilers as well, as long as they have ```stdint.h```)
- public domain license (UNLICENSE)
- just copy the C-file to your Lua project


| File | Version | Description |
| --- | :---: | --- |
| sts_base64.c | 1.0.0 | Base64 encoder/decoder |
| sts_msgpack.c | 1.0.1 | MessagePack encoder/decoder |
| test.c | - | Creates a Lua state, load all the modules and execute ```test.lua``` |


### How to include into your project
- copy the C-file to you project
- add it to the compilation process
- add a "prototype" like ```LUALIB_API int luaopen_xxx(lua_State *L)``` to your code
- load the library with ```luaL_requiref(L, "xxx", luaopen_xxx, 1)```


## sts_base64.c
[Base64](https://en.wikipedia.org/wiki/Base64) encoder / decoder.

### Example
```lua
local base64 = require('base64')
print(base64.encode('light work')) -- > bGlnaHQgd29yaw==
print(assert(base64.decode('bGlnaHQgd29yaw=='))) -- > light work
```

### API

#### base64.encode(data)
Encode the given Lua string *data* to proper Base64.

Returns a Lua string containing Base64 encoded *data*. This will never fail except if Lua cannot allocate enough memory.

#### base64.decode(data)
Decode the given Lua string *data* to binary.

Returns the decoded Base64 data as a string or *nil* plus an error message if decoding failed. This can happen if the given *data* string is not a valid Base64 string.

#### Implementation Details
Nothing special. Heavy use of ```luaL_Buffer```.


#### History
- **1.0.0**
    - initial version


## sts_msgpack.c
A very simple but performant [MessagePack](https://msgpack.org) encoder / decoder written in plain C (uses ```stdint.h``` so it might require a proper C99 compiler).

### Example
```lua
local msgpack = require('msgpack')

-- packs all arguments to a valid messagepack binary
local binary = assert(msgpack.encode(1, math.pi, 2.5, 'Hello World', { 1, 2, 3 }, { foo = 'bar', bar = 'foo'}))

-- unpack all values from the binary
print(msgpack.decode(binary))
```

### API

#### msgpack.encode(...)
Encodes all given arguments to a single messagepack binary string.

Returns a Lua string containing the messagepack or *nil* plus an error message if encoding failed.

The following Lua types will be encoded:
- **nil**
- **boolean**
- **number**
    - if it is an integer value, the implementation will choose the appropriate uint / int encoding
    - for numbers it will check if the number can be represented as 32-bit / 64-bit IEEE float value
    - 64-bit integers cannot be encoded as unsigned as there is no proper way to determine if the value is really unsigned from Lua :(
- **string**
    - will do a basic UTF-8 check and encode it as *strX* objects in messagepack when valid
    - other (non UTF-8 strings) will be encoded as *binX* objects
- **table**
    - when a "proper" Lua array (index starting at 1 etc.) it will be encoded as an array object otherwise as a map
    - empty tables will be encoded as empty arrays
- other Lua types cause an error

#### msgpack.decode(binary [, start, count])
Decode the given messagepack binary string to Lua values. If *start* is given it will start at this position (starting at 1). When *count* is given, it will only decode that amount of values. Per default the decoder will start at position 1 and decode all values from the given binary.

Returns all decoded values plus the position. This can be used to decode values in a loop. In case of an error it will return *nil* plus an error message.

**Notes:**
- arrays containing *nil* values will render the Lua array "broken" as the index will not "continue". This is how Lua works :/

```lua
-- a simple loop decoding all values
local binary = msgpack.encode(1, 2, 3, 4, 5, 6)
local position = 1
while position < #binary do
    local value
    value, position = msgpack.decode(binary, position, 1)
    print(value)
end
```

### Implementation Details
The decoder works pretty straight forward and ensures by calling ```luaL_checkstack``` that there's always enough "space" to unpack values.

The encoder uses an internal buffer of 16KiB to store the binary values. If this internal buffer is full, it will append the contents as a Lua string to a table. At the end it will use the ```luaL_Buffer``` mechanics to "concat" the table of binary strings.

### History
- **1.0.1**
    - improved UTF-8 validation (overlong encodings are still not recognized)
- **1.0.0**
    - initial version
