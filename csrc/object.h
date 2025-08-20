#ifndef LTREESITTER_OBJECT_H
#define LTREESITTER_OBJECT_H

#include <lua.h>

// The object table is a table in the registry with __mode = 'k' that we use to represent ownership.
// This allows lua to garbage collect our data structures properly.
//
// The basic idea is just a table with weak keys:
//
//    object_table[keeper] = kept
//
// As long as `keeper` lives, so does `kept`
//
// With this setup, one `kept` object can have multiple `keeper`s, e.g. if a
// tree is copied, its source text needs to be kept alive as long as any copies
// of the tree are alive.
//
// A note on "uservalues":
// Lua 5.3 onward essentially has this built in. Userdata may be allocated with
// what is called a uservalue (or multiple in >=5.4) which is basically just a
// slot for a strong reference to another lua object. But! We want to support
// 5.1, 5.2, and luajit.

void setup_object_table(lua_State *);

void setup_parser_cache(lua_State *);
void push_parser_cache(lua_State *);

// Creates a keeper-kept relationship between two objects
//
// Neither index may be a pseudo-index (relative indexes are fine though)
void bind_lifetimes(lua_State *, int as_long_as_this_object_lives, int so_shall_this_one);

// Pushes the object kept by the keeper at `keeper_idx`
//
// If the object at `keeper_idx` isn't actually a keeper, raises a lua error
void push_kept(lua_State *, int keeper_idx);

#endif
