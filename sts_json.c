/*
================================================================================

    JSON encoder / decoder
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
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"


#define JSON_AUTHOR         "Sebastian Steinhauer <s.steinhauer@yahoo.de>"
#define JSON_VERSION        "1.0.0"


typedef struct json_t {
    lua_State               *L;
    jmp_buf                 jmp;

    /* decoder variables */
    const char              *input;

    /* encoder variables */
    char                    output[8];
    size_t                  position;
    int                     table, index;
} json_t;


static void decode_value(json_t *json);
static void encode_value(json_t *json);


static int valid_array(lua_State *L) {
    int index;
    lua_pushnil(L);
    for (index = 1; lua_next(L, -2); ++index) {
        lua_pop(L, 1);
        if (lua_tointeger(L, -1) != index) {
            lua_pop(L, 1);
            return 0;
        }
    }
    return 1;
}


static void json_error(json_t *json, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    luaL_pushfail(json->L);
    lua_pushvfstring(json->L, fmt, va);
    va_end(va);
    longjmp(json->jmp, 1);
}


static void json_flush(json_t *json) {
    if (json->position > 0) {
        lua_pushlstring(json->L, json->output, json->position);
        lua_rawseti(json->L, json->table, json->index++);
        json->position = 0;
    }
}


static void json_write(json_t *json, char ch) {
    if (json->position >= sizeof(json->output))
        json_flush(json);
    json->output[json->position++] = ch;
}


static void json_write_str(json_t *json, const char *str) {
    for (; *str; ++str)
        json_write(json, *str);
}


static void json_write_strf(json_t *json, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    lua_pushvfstring(json->L, fmt, va);
    va_end(va);
    json_write_str(json, lua_tostring(json->L, -1));
    lua_pop(json->L, 1);
}


static void parse_whitespace(json_t *json) {
    while (*json->input && isspace(*json->input))
        ++json->input;
}


static void parse_token(json_t *json, const char *token) {
    size_t i;
    parse_whitespace(json);
    for (i = 0; token[i]; ++i)
        if (json->input[i] != token[i])
            json_error(json, "expected token '%s'", token);
    json->input += i;
}


static void decode_number(json_t *json) {
    int isnum;
    char buffer[256];
    size_t i;
    lua_Number num;

    parse_whitespace(json);
    for (i = 0; *json->input && i < sizeof(buffer); ++json->input, ++i) {
        switch (*json->input) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
            case '.': case 'e': case 'E':
                buffer[i] = *json->input;
                break;
            default:
                goto push_number;
        }
    }
    
push_number:
    /* let Lua do the work :) */
    lua_pushlstring(json->L, buffer, i);
    num = lua_tonumberx(json->L, -1, &isnum);
    lua_pop(json->L, 1);
    if (!isnum)
        json_error(json, "number expected");
    lua_pushnumber(json->L, num);
}


static void decode_string(json_t *json) {
    luaL_Buffer buffer;
    char code;

    parse_token(json, "\"");
    luaL_buffinit(json->L, &buffer);
    for (; *json->input; json->input++) {
        code = *json->input;
        if (code == '"') {
            break;
        } else if (code == '\\') {
            json->input++;
            switch (code = *json->input) {
                case '"': luaL_addchar(&buffer, '"'); break;
                case '\\': luaL_addchar(&buffer, '\\'); break;
                case '/': luaL_addchar(&buffer, '/'); break;
                case 'b': luaL_addchar(&buffer, '\b'); break;
                case 'f': luaL_addchar(&buffer, '\f'); break;
                case 'n': luaL_addchar(&buffer, '\n'); break;
                case 'r': luaL_addchar(&buffer, '\r'); break;
                case 't': luaL_addchar(&buffer, '\t'); break;
                default: json_error(json, "invalid string escape '%c'", code);
            }
        } else {
            luaL_addchar(&buffer, code);
        }
    }
    parse_token(json, "\"");
    luaL_pushresult(&buffer);
}


static void decode_array(json_t *json) {
    int i;

    parse_token(json, "[");
    lua_newtable(json->L);

    /* check empty array */
    parse_whitespace(json);
    if (*json->input == ']') {
        parse_token(json, "]");
        return;
    }

    /* parse values */
    for (i = 1; *json->input; ++i) {
        decode_value(json);
        lua_rawseti(json->L, -2, i);

        parse_whitespace(json);
        if (*json->input == ']')
            break;
        parse_token(json, ",");
    }
    parse_token(json, "]");
}


static void decode_object(json_t *json) {
    parse_token(json, "{");
    lua_newtable(json->L);

    /* check empty object */
    parse_whitespace(json);
    if (*json->input == '}') {
        parse_token(json, "}");
        return;
    }

    /* parse values */
    while (*json->input) {
        decode_string(json);
        parse_token(json, ":");
        decode_value(json);
        lua_rawset(json->L, -3);

        parse_whitespace(json);
        if (*json->input == '}')
            break;
        parse_token(json, ",");
    }
    parse_token(json, "}");
}


static void decode_value(json_t *json) {
    luaL_checkstack(json->L, 1, "not enough stack space");
    parse_whitespace(json);
    switch (*json->input) {
        case 'n': /* null */
            parse_token(json, "null");
            lua_pushnil(json->L);
            break;
        case 'f': /* false */
            parse_token(json, "false");
            lua_pushboolean(json->L, 0);
            break;
        case 't': /* true */
            parse_token(json, "true");
            lua_pushboolean(json->L, 1);
            break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': case '-': /* number */
            decode_number(json);
            break;
        case '"': /* string */
            decode_string(json);
            break;
        case '[': /* array */
            decode_array(json);
            break;
        case '{': /* object */
            decode_object(json);
            break;
        default:
            json_error(json, "invalid character '%c' found", *json->input);
    }
}


static void encode_number(json_t *json) {
    if (lua_isinteger(json->L, -1)) {
        json_write_strf(json, "%I", lua_tointeger(json->L, -1));
    } else {
        json_write_strf(json, "%f", lua_tonumber(json->L, -1));
    }
}


static void encode_string(json_t *json) {
    const char *str = lua_tostring(json->L, -1);
    json_write(json, '"');
    for (; *str; ++str) {
        switch (*str) {
            case '\\':  json_write_str(json, "\\\\"); break;
            case '"':   json_write_str(json, "\\\""); break;
            case '\b':  json_write_str(json, "\\b"); break;
            case '\f':  json_write_str(json, "\\f"); break;
            case '\n':  json_write_str(json, "\\n"); break;
            case '\r':  json_write_str(json, "\\r"); break;
            case '\t':  json_write_str(json, "\\t"); break;
            default:    json_write(json, *str); break;
        }
    }
    json_write(json, '"');
}


static void encode_table(json_t *json) {
    int more = 0;
    luaL_checkstack(json->L, 4, "not enough stack space");

    if (valid_array(json->L)) {
        json_write(json, '[');

        lua_pushnil(json->L);
        while (lua_next(json->L, -2)) {
            if (more)
                json_write(json, ',');
            else
                more = 1;
            encode_value(json);
        }

        json_write(json, ']');
    } else {
        json_write(json, '{');

        lua_pushnil(json->L);
        while (lua_next(json->L, -2)) {
            if (more)
                json_write(json, ',');
            else
                more = 1;

            /* encode key */
            lua_pushvalue(json->L, -2);
            if (lua_type(json->L, -1) != LUA_TSTRING)
                json_error(json, "cannot encode non-string keys for object");
            encode_value(json);
            json_write(json, ':');
            encode_value(json);
        }

        json_write(json, '}');
    }
}


static void encode_value(json_t *json) {
    int type = lua_type(json->L, -1);
    switch (type) {
        case LUA_TNIL:
            json_write_str(json, "null");
            break;
        case LUA_TBOOLEAN:
            json_write_str(json, lua_toboolean(json->L, -1) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            encode_number(json);
            break;
        case LUA_TSTRING:
            encode_string(json);
            break;
        case LUA_TTABLE:
            encode_table(json);
            break;
        default:
            json_error(json, "cannot encode Lua type '%s'", lua_typename(json->L, type));
    }
    lua_pop(json->L, 1);
}


static int f_encode(lua_State *L) {
    json_t                  json;
    luaL_Buffer             buffer;
    int                     i;

    /* prepare state */
    luaL_checkany(L, 1);
    lua_newtable(L);
    json.L = L;
    json.position = 0;
    json.table = lua_absindex(L, -1);
    json.index = 1;

    /* handle errors */
    if (setjmp(json.jmp))
        return 2;

    /* encode value */
    lua_pushvalue(L, 1);
    encode_value(&json);

    /* write output */
    json_flush(&json);
    luaL_buffinit(L, &buffer);
    for (i = 1; i < json.index; ++i) {
        lua_rawgeti(L, json.table, i);
        luaL_addvalue(&buffer);
    }
    luaL_pushresult(&buffer);
    return 1;
}


static int f_decode(lua_State *L) {
    json_t                  json;

    /* prepare state */
    json.L = L;
    json.input = luaL_checkstring(L, 1);
    if (setjmp(json.jmp))
        return 2;

    /* decode value */
    decode_value(&json);
    return 1;
}


static const luaL_Reg       funcs[] = {
    { "encode",             f_encode        },
    { "decode",             f_decode        },
    { "_VERSION",           NULL            },
    { "_AUTHOR",            NULL            },
    { NULL,                 NULL            }
};


LUALIB_API int luaopen_json(lua_State *L) {
    luaL_newlib(L, funcs);
    lua_pushstring(L, JSON_VERSION);
    lua_setfield(L, -2, "_VERSION");
    lua_pushstring(L, JSON_AUTHOR);
    lua_setfield(L, -2, "_AUTHOR");
    return 1;
}
