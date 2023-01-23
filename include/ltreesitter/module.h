#ifndef LTREESITTER_LTREESITTER_H
#define LTREESITTER_LTREESITTER_H

#include <lua.h>

#ifdef _WIN32
#define LTREESITTER_EXPORT __declspec (dllexport)
#else
#define LTREESITTER_EXPORT __attribute__ ((visibility("default")))
#endif

LTREESITTER_EXPORT int luaopen_ltreesitter(lua_State *L);

#endif // LTREESITTER_LTREESITTER_H
