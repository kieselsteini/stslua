/*
================================================================================

    MessagePack encoder / decoder
    written by Sebastian Steinhauer <s.steinhauer@yahoo.de>

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or
    distribute this software, either in source code form or as a compiled
    binary, for any purpose, commercial or non-commercial, and by any
    means.

    In jurisdictions that recognize copyright laws, the author or authors
    of this software dedicate any and all copyright interest in the
    software to the public domain. We make this dedication for the benefit
    of the public at large and to the detriment of our heirs and
    successors. We intend this dedication to be an overt act of
    relinquishment in perpetuity of all present and future rights to this
    software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
    OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.

    For more information, please refer to <http://unlicense.org/>

================================================================================
*/
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <setjmp.h>

#include "lua.h"
#include "lauxlib.h"


#define MSGPACK_AUTHOR      "Sebastian Steinhauer <s.steinhauer@yahoo.de>"
#define MSGPACK_VERSION     "1.0.2"


typedef struct msg_t {
    lua_State               *L;
    jmp_buf                 jmp;
    size_t                  position;

    /* variables for input */
    const uint8_t           *input;
    size_t                  length;

    /* variables for output */
    uint8_t                 buffer[1024 * 16];
    int                     table, index;
} msg_t;


static int valid_utf8(const uint8_t *str, size_t length) {
    static const uint8_t    table[256] = {
        /* 0x00 - 0x7f -> ASCII */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        /* 0x80 - 0xbf -> continuation bytes */
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        /* 0xc0 - 0xdf -> 2 byte encodings */
        9, 9, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        /* 0xe0 - 0xef -> 3 byte encodings */
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        /* 0xf0 - 0xff -> 4 byte encodings */
        3, 3, 3, 3, 3, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9
    };
    while (length > 0) {
        size_t code = table[*str++]; --length;
        if ((code > 3) || (code > length))
            return 0;
        for (; code > 0; --code, --length, ++str)
            if (table[*str] != 8)
                return 0;
    }
    return 1;
}


static int count_table(lua_State *L) {
    int index, array;
    lua_pushnil(L);
    for (index = array = 1; lua_next(L, -2); ++index) {
        lua_pop(L, 1); /* remove value */
        if (array && (lua_tointeger(L, -1) != index))
            array = 0;
    }
    return array ? (index - 1) : -(index - 1);
}


/* prototypes */
static void msg_encode(msg_t *msg);
static void msg_decode(msg_t *msg);


static void msg_error(msg_t *msg, const char *fmt, ...) {
    va_list                 va;
    va_start(va, fmt);
    lua_pushnil(msg->L);
    lua_pushvfstring(msg->L, fmt, va);
    va_end(va);
    longjmp(msg->jmp, 1);
}


static void msg_flush(msg_t *msg) {
    if (msg->position > 0) {
        lua_pushlstring(msg->L, (const char*)msg->buffer, msg->position);
        lua_rawseti(msg->L, msg->table, msg->index++);
        msg->position = 0;
    }
}


static void msg_write(msg_t *msg, const uint8_t value) {
    if (msg->position >= sizeof(msg->buffer))
        msg_flush(msg);
    msg->buffer[msg->position++] = value;
}


static void msg_write_str(msg_t *msg, const uint8_t *str, size_t length) {
    for (; length > 0; --length)
        msg_write(msg, *str++);
}


static void msg_write_int(msg_t *msg, const uint64_t value, size_t length) {
    for (; length > 0; --length)
        msg_write(msg, (value >> ((length - 1) * 8)) & 255);
}


static uint8_t msg_read(msg_t *msg) {
    if (msg->position >= msg->length)
        msg_error(msg, "required more bytes to decode messagepack");
    return msg->input[msg->position++];
}


static void msg_read_bin(msg_t *msg, uint8_t *ptr, const size_t length) {
    size_t                  i;
    for (i = 0; i < length; ++i)
        ptr[i] = msg_read(msg);
}


static void msg_read_str(msg_t *msg, const size_t length) {
    luaL_Buffer buffer;
    size_t i;
    char *data = luaL_buffinitsize(msg->L, &buffer, length);
    for (i = 0; i < length; ++i)
        data[i] = (char)msg_read(msg);
    luaL_pushresultsize(&buffer, length);
}


static uint64_t msg_read_int(msg_t *msg, size_t length) {
    uint64_t value;
    for (value = 0; length > 0; --length)
        value = (value << 8) | msg_read(msg);
    return value;
}


static void msg_encode_number(msg_t *msg) {
    if (lua_isinteger(msg->L, -1)) {
        /* there is no clever way to distinguish between singed/unsigned */
        lua_Integer i = lua_tointeger(msg->L, -1);
        if (i >= 0) {
            if (i <= 0x7f) {
                msg_write(msg, (uint8_t)i);
            } else if (i <= 0xff) {
                msg_write(msg, 0xcc);
                msg_write_int(msg, (uint64_t)i, sizeof(uint8_t));
            } else if (i <= 0xffff) {
                msg_write(msg, 0xcd);
                msg_write_int(msg, (uint64_t)i, sizeof(uint16_t));
            } else if (i <= 0xffffffff) {
                msg_write(msg, 0xce);
                msg_write_int(msg, (uint64_t)i, sizeof(uint32_t));
            } else {
                msg_write(msg, 0xcf);
                msg_write_int(msg, (uint64_t)i, sizeof(uint64_t));
            }
        } else {
            if (i >= -32) {
                msg_write(msg, (uint8_t)i);
            } else if (i >= -128) {
                msg_write(msg, 0xd0);
                msg_write_int(msg, (uint8_t)i, sizeof(uint8_t));
            } else if (i >= -32768) {
                msg_write(msg, 0xd1);
                msg_write_int(msg, (uint16_t)i, sizeof(uint16_t));
            } else if (i >= -2147483648) {
                msg_write(msg, 0xd2);
                msg_write_int(msg, (uint32_t)i, sizeof(uint32_t));
            } else {
                msg_write(msg, 0xd3);
                msg_write_int(msg, (uint64_t)i, sizeof(uint64_t));
            }
        }
    } else {
        double f64 = (double)lua_tonumber(msg->L, -1);
        float f32 = (float)f64;
        if (f64 == f32) {
            msg_write(msg, 0xca);
            msg_write_str(msg, (const uint8_t*)&f32, sizeof(f32));
        } else {
            msg_write(msg, 0xcb);
            msg_write_str(msg, (const uint8_t*)&f64, sizeof(f64));
        }
    }
}


static void msg_encode_string(msg_t *msg) {
    size_t                  length;
    const uint8_t           *str = (const uint8_t*)luaL_checklstring(msg->L, -1, &length);

    if (valid_utf8(str, length)) {
        if (length <= 0x1f) {
            msg_write(msg, 0xa0 + length);
        } else if (length <= 0xff) {
            msg_write(msg, 0xd9);
            msg_write_int(msg, length, sizeof(uint8_t));
        } else if (length <= 0xffff) {
            msg_write(msg, 0xda);
            msg_write_int(msg, length, sizeof(uint16_t));
        } else {
            msg_write(msg, 0xdb);
            msg_write_int(msg, length, sizeof(uint32_t));
        }
    } else {
        if (length <= 0xff) {
            msg_write(msg, 0xc4);
            msg_write_int(msg, length, sizeof(uint8_t));
        } else if (length <= 0xffff) {
            msg_write(msg, 0xc5);
            msg_write_int(msg, length, sizeof(uint16_t));
        } else {
            msg_write(msg, 0xc6);
            msg_write_int(msg, length, sizeof(uint32_t));
        }
    }
    msg_write_str(msg, str, length);
}


static void msg_encode_table(msg_t *msg) {
    int items = count_table(msg->L);
    if (items >= 0) {
        if (items <= 0x0f) {
            msg_write(msg, 0x90 + items);
        } else if (items <= 0xffff) {
            msg_write(msg, 0xdc);
            msg_write_int(msg, items, sizeof(uint16_t));
        } else {
            msg_write(msg, 0xdd);
            msg_write_int(msg, items, sizeof(uint32_t));
        }
        lua_pushnil(msg->L);
        while (lua_next(msg->L, -2))
            msg_encode(msg); /* encode value */
    } else {
        items = -items;
        if (items <= 0x0f) {
            msg_write(msg, 0x80 + items);
        } else if (items <= 0xffff) {
            msg_write(msg, 0xde);
            msg_write_int(msg, items, sizeof(uint16_t));
        } else {
            msg_write(msg, 0xdf);
            msg_write_int(msg, items, sizeof(uint32_t));
        }
        lua_pushnil(msg->L);
        while (lua_next(msg->L, -2)) {
            lua_pushvalue(msg->L, -2);
            msg_encode(msg); /* encode key */
            msg_encode(msg); /* encode value */
        }
    }
}


static void msg_encode(msg_t *msg) {
    int t = lua_type(msg->L, -1);
    switch (t) {
        case LUA_TNIL:
            msg_write(msg, 0xc0);
            break;
        case LUA_TBOOLEAN:
            msg_write(msg, lua_toboolean(msg->L, -1) ? 0xc3 : 0xc2);
            break;
        case LUA_TNUMBER:
            msg_encode_number(msg);
            break;
        case LUA_TSTRING:
            msg_encode_string(msg);
            break;
        case LUA_TTABLE:
            msg_encode_table(msg);
            break;
        default:
            msg_error(msg, "cannot encode Lua value of type '%s'", lua_typename(msg->L, t));
    }
    lua_pop(msg->L, 1); /* pop encoded value */
}


static void decode_array(msg_t *msg, int items) {
    int                     i;
    lua_createtable(msg->L, items, 0);
    for (i = 1; i <= items; ++i) {
        msg_decode(msg);
        lua_rawseti(msg->L, -2, i);
    }
}


static void decode_map(msg_t *msg, int items) {
    lua_createtable(msg->L, 0, items);
    for (; items > 0; --items) {
        msg_decode(msg);
        msg_decode(msg);
        lua_rawset(msg->L, -3);
    }
}


static void msg_decode(msg_t *msg) {
    const uint8_t code = msg_read(msg);
    luaL_checkstack(msg->L, 1, "too many values to unpack on stack");
    switch (code) {
        case 0xc0:
            lua_pushnil(msg->L);
            break;
        case 0xc2:
            lua_pushboolean(msg->L, 0);
            break;
        case 0xc3:
            lua_pushboolean(msg->L, 1);
            break;
        case 0xc4: case 0xd9:
            msg_read_str(msg, msg_read_int(msg, sizeof(uint8_t)));
            break;
        case 0xc5: case 0xda:
            msg_read_str(msg, msg_read_int(msg, sizeof(uint16_t)));
            break;
        case 0xc6: case 0xdb:
            msg_read_str(msg, msg_read_int(msg, sizeof(uint32_t)));
            break;
        case 0xca: {
            float f32;
            msg_read_bin(msg, (uint8_t*)&f32, sizeof(f32));
            lua_pushnumber(msg->L, f32);
            break;
        }
        case 0xcb: {
            double f64;
            msg_read_bin(msg, (uint8_t*)&f64, sizeof(f64));
            lua_pushnumber(msg->L, f64);
            break;
        }
        case 0xcc:
            lua_pushinteger(msg->L, msg_read_int(msg, sizeof(uint8_t)));
            break;
        case 0xcd:
            lua_pushinteger(msg->L, msg_read_int(msg, sizeof(uint16_t)));
            break;
        case 0xce:
            lua_pushinteger(msg->L, msg_read_int(msg, sizeof(uint32_t)));
            break;
        case 0xcf:
            lua_pushinteger(msg->L, msg_read_int(msg, sizeof(uint64_t)));
            break;
        case 0xd0:
            lua_pushinteger(msg->L, (int8_t)msg_read_int(msg, sizeof(int8_t)));
            break;
        case 0xd1:
            lua_pushinteger(msg->L, (int16_t)msg_read_int(msg, sizeof(int16_t)));
            break;
        case 0xd2:
            lua_pushinteger(msg->L, (int32_t)msg_read_int(msg, sizeof(int32_t)));
            break;
        case 0xd3:
            lua_pushinteger(msg->L, (int64_t)msg_read_int(msg, sizeof(int64_t)));
            break;
        case 0xdc:
            decode_array(msg, (uint16_t)msg_read_int(msg, sizeof(uint16_t)));
            break;
        case 0xdd:
            decode_array(msg, (uint32_t)msg_read_int(msg, sizeof(uint32_t)));
            break;
        case 0xde:
            decode_map(msg, (uint16_t)msg_read_int(msg, sizeof(uint16_t)));
            break;
        case 0xdf:
            decode_map(msg, (uint32_t)msg_read_int(msg, sizeof(uint32_t)));
            break;
        default:
            if (code <= 0x7f) {
                lua_pushinteger(msg->L, code);
            } else if (code <= 0x8f) {
                decode_map(msg, code - 0x80);
            } else if (code <= 0x9f) {
                decode_array(msg, code - 0x90);
            } else if (code <= 0xbf) {
                msg_read_str(msg, code - 0xa0);
            } else if (code >= 0xe0) {
                lua_pushinteger(msg->L, (int8_t)code);
            } else {
                msg_error(msg, "invalid messagepack code: 0x%x", code);
            }
            break;
    }
}


static int f_encode(lua_State *L) {
    int                     i, n;
    luaL_Buffer             buffer;
    msg_t                   msg;

    /* init msgpack state */
    lua_newtable(L);
    msg.L = L;
    msg.position = 0;
    msg.index = 1;
    msg.table = lua_absindex(L, -1);

    /* handle errors */
    if (setjmp(msg.jmp))
        return 2;

    /* encode all arguments (-1 because of the table) */
    for (i = 1, n = lua_gettop(L); i < n; ++i) {
        lua_pushvalue(L, i);
        msg_encode(&msg);
    }

    /* write output */
    msg_flush(&msg);
    luaL_buffinit(L, &buffer);
    for (i = 1; i <= msg.index; ++i) {
        lua_rawgeti(L, msg.table, i);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);
    return 1;
}


static int f_decode(lua_State *L) {
    msg_t                   msg;
    int                     items, count;

    /* prepare state */
    msg.L = L;
    msg.input = (const uint8_t*)luaL_checklstring(L, 1, &msg.length);
    msg.position = (size_t)luaL_optinteger(L, 2, 1);
    luaL_argcheck(L, (msg.position >= 1) && (msg.position <= msg.length), 2, "invalid starting position");
    --msg.position;
    count = (int)luaL_optinteger(L, 3, 1024 * 64);

    /* handle errors */
    if (setjmp(msg.jmp))
        return 2;

    /* decode items */
    for (items = 0; (items < count) && (msg.position < msg.length); ++items)
        msg_decode(&msg);
    lua_pushinteger(L, msg.position + 1);
    return items + 1;
}


static const luaL_Reg       funcs[] = {
    { "encode",             f_encode        },
    { "decode",             f_decode        },
    { "_VERSION",           NULL            },
    { "_AUTHOR",            NULL            },
    { NULL,                 NULL            }
};


LUALIB_API int luaopen_msgpack(lua_State *L) {
    luaL_newlib(L, funcs);
    lua_pushstring(L, MSGPACK_VERSION);
    lua_setfield(L, -2, "_VERSION");
    lua_pushstring(L, MSGPACK_AUTHOR);
    lua_setfield(L, -2, "_AUTHOR");
    return 1;
}
