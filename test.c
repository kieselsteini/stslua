/*
================================================================================

    Simple Code to run "test.lua" with alle modules built
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
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


LUALIB_API int luaopen_msgpack(lua_State *L);
LUALIB_API int luaopen_base64(lua_State *L);


static int run_code(lua_State *L) {
    if (luaL_loadfile(L, "test.lua") != LUA_OK)
        lua_error(L);
    lua_call(L, 0, 0);
    return 0;
}


int main() {
    lua_State               *L;

    L = luaL_newstate();
    luaL_openlibs(L);

    luaL_requiref(L, "base64", luaopen_base64, 1);
    luaL_requiref(L, "msgpack", luaopen_msgpack, 1);
    lua_pop(L, 2);

    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);

    lua_pushcfunction(L, run_code);
    if (lua_pcall(L, 0, 0, -2) != LUA_OK)
        fprintf(stderr, "%s\n", lua_tostring(L, -1));

    lua_close(L);
    return 0;
}
