#ifndef LTREESITTER_LANGUAGE_H
#define LTREESITTER_LANGUAGE_H

#include "dynamiclib.h"
#include "types.h"
#include <lua.h>

// ( -- table )
void language_init_metatable(lua_State *);

def_check_assert(TSLanguage const *, language, LTREESITTER_LANGUAGE_METATABLE_NAME)

TSLanguage const *language_load_from(Dynlib dl, size_t lang_name_len, char const *language_name);

// ( -- ?Language )
void language_get_by_ptr(lua_State *L, TSLanguage const *);

// ( string ?string -- language string )
int language_load(lua_State *L);

// ( string ?string -- language string )
int language_require(lua_State *L);

void setup_dynlib_cache(lua_State *L);
void dynlib_init_metatable(lua_State *L);
void language_setup_registry_table(lua_State *);

#endif
