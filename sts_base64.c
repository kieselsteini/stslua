/*
================================================================================

    Base64 encoder / decoder
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
#include <stdarg.h>
#include <stdint.h>

#include "lua.h"
#include "lauxlib.h"


#define BASE64_AUTHOR       "Sebastian Steinhauer <s.steinhauer@yahoo.de>"
#define BASE64_VERSION      "1.0.0"


static int f_encode(lua_State *L) {
    static const char       *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    luaL_Buffer buffer;
    int value, bits;
    size_t length, written;
    const uint8_t *data = (const uint8_t*)luaL_checklstring(L, 1, &length);

    luaL_buffinit(L, &buffer);

    for (value = bits = 0, written = 0; length > 0; --length, ++data) {
        value = (value << 8) | *data; bits += 8;
        for (; bits >= 6; bits -= 6) {
            luaL_addchar(&buffer, table[(value >> (bits - 6)) & 63]);
            ++written;
        }
    }
    if (bits > 0) {
        value <<= 8; bits += 8;
        luaL_addchar(&buffer, table[(value >> (bits - 6)) & 63]);
        ++written;
    }
    while (written % 4) {
        luaL_addchar(&buffer, '=');
        ++written;
    }

    luaL_pushresult(&buffer);
    return 1;
}


static int f_decode(lua_State *L) {
    static const uint8_t    table[256] = {
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 62, 65, 65, 65, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 65, 65, 65, 64, 65, 65,
        65,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 65, 65, 65, 65, 65,
        65, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 65, 65, 65, 65, 65,
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
        65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65
    };
    luaL_Buffer buffer;
    size_t length;
    int value, bits, code;
    const uint8_t *data = (const uint8_t*)luaL_checklstring(L, 1, &length);

    luaL_buffinit(L, &buffer);
    for (value = bits = 0; length > 0; --length, ++data) {
        code = table[*data];
        if (code < 64) {
            value = (value << 6) | code; bits += 6;
        } else if (code == 64) {
            break;
        } else {
            luaL_pushfail(L);
            lua_pushliteral(L, "invalid base64 string");
            return 2;
        }
        for (; bits >= 8; bits -= 8)
            luaL_addchar(&buffer, (value >> (bits - 8)) & 255);
    }
    for (; bits >= 8; bits -= 8)
        luaL_addchar(&buffer, (value >> (bits - 8)) & 255);
    luaL_pushresult(&buffer);
    return 1;
}


static const luaL_Reg       funcs[] = {
    { "encode",             f_encode        },
    { "decode",             f_decode        },
    { "_VERSION",           NULL            },
    { "_AUTHOR",            NULL            },
    { NULL,                 NULL            }
};


LUALIB_API int luaopen_base64(lua_State *L) {
    luaL_newlib(L, funcs);
    lua_pushstring(L, BASE64_VERSION);
    lua_setfield(L, -2, "_VERSION");
    lua_pushstring(L, BASE64_AUTHOR);
    lua_setfield(L, -2, "_AUTHOR");
    return 1;
}
