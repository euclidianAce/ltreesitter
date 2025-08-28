#ifndef LTREESITTER_LANGUAGE_H
#define LTREESITTER_LANGUAGE_H

#include "types.h"
#include "dynamiclib.h"
#include <lua.h>

// ( -- table )
void language_init_metatable(lua_State *);

def_check_assert(TSLanguage const *, language, LTREESITTER_LANGUAGE_METATABLE_NAME)

TSLanguage const *language_load_from(Dynlib dl, size_t lang_name_len, char const *language_name);

// ( string ?string -- language string )
int language_load(lua_State *L);

// ( string ?string -- language string )
int language_require(lua_State *L);

#endif
