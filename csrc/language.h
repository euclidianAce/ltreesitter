#ifndef LTREESITTER_LANGUAGE_H
#define LTREESITTER_LANGUAGE_H

// 260731 OIS - Modified to work with Emscripten, where no dynlib is available.

#ifndef __EMSCRIPTEN__
#include "dynamiclib.h"
#endif
#include "types.h"
#include <lua.h>

// ( -- table )
void language_init_metatable(lua_State *);

def_check_assert(TSLanguage const *, language, LTREESITTER_LANGUAGE_METATABLE_NAME)

	TSLanguage const *language_load_from(
#ifndef __EMSCRIPTEN__
		Dynlib dl,
#else
		lua_State *L,
#endif
		size_t lang_name_len, char const *language_name);

// ( string ?string -- language string )
int language_load(lua_State *L);

// ( string ?string -- language string )
int language_require(lua_State *L);

void setup_dynlib_cache(lua_State *L);
void dynlib_init_metatable(lua_State *L);

#ifdef __EMSCRIPTEN__

// OIS: New function to register tree-sitter languages.
void ltreesitter_register_static_language(
	lua_State *L,
	char const *name,
	TSLanguage const *language);
#endif

#endif
