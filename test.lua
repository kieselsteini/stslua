--------------------------------------------------------------------------------
local function base64_test()
    local base64 = require('base64')

    -- simple test
    do
        local str = 'light work'
        local a = base64.encode(str)
        local b = assert(base64.decode(a))
        assert(b == str)
    end

    -- invalid characters
    assert(not base64.decode('?$%&[]'))

    -- do some test with premade values
    local function test(d, e)
        local a = base64.encode(d)
        local b = assert(base64.decode(a))
        assert(a == e)
        assert(b == d)
    end

    test('Man', 'TWFu')
    test('Ma',  'TWE=')
    test('M',   'TQ==')
end


--------------------------------------------------------------------------------
local function msgpack_test()
    local msgpack = require('msgpack')

    -- simple test
    do
        local values = { true, false, 1, 128, -1, math.pi, 2.5, 'Hello äÖü ß 😀 valid UTF-8' }
        local a = assert(msgpack.encode(table.unpack(values)))
        local b = table.pack(assert(msgpack.decode(a)))
        for i = 1, #values do
            assert(values[i] == b[i], string.format('value: %d', i))
        end
    end

    -- continue decoding
    do
        local a = assert(msgpack.encode(1, 2, 3, 4, 5, 6, 7, 8, 9))
        local position = 1
        for i = 1, 9 do
            local value
            value, position = assert(msgpack.decode(a, position, 1))
            assert(value == i)
        end
    end

    -- test string / binary encodings
    do
        -- fixstr
        for i = 0, 31 do
            local a = assert(msgpack.encode(string.rep('x', i)))
            assert(string.byte(a) == 0xa0 + i)
        end

        -- helper function to encode / decode messagepack
        local function test(code, size, length, str)
            str = string.rep(str, length)
            local a = assert(msgpack.encode(str))
            local mc, ms = string.unpack(string.format('>Bs%d', size), a)
            assert(mc == code)
            assert(ms == str)
            local b = assert(msgpack.decode(a))
            assert(b == str)
        end

        test(0xd9, 1, 0xff, 'x') -- str8
        test(0xda, 2, 0xffff, 'x') -- str16
        test(0xdb, 4, 0x10000, 'x') -- str32
        test(0xc4, 1, 0xff, string.char(255)) -- bin8
        test(0xc5, 2, 0xffff, string.char(255)) -- bin16
        test(0xc6, 4, 0x10000, string.char(255)) -- bin32
    end
end


--------------------------------------------------------------------------------
local function json_test()
    local json = require('json')
    print(assert(json.encode({int = 1, pi = math.pi, str = 'Hello World!', array = {1,2,3}, obj = { test = 'Test!'}})))
    for k,v in pairs(assert(json.decode('{"str":"Hello World!","obj":{"test":"Test!"},"int":1,"pi":3.1415926535898,"array":[1,2,3]}'))) do
        print(k, v)
    end
end


--------------------------------------------------------------------------------
base64_test()
msgpack_test()
json_test()
