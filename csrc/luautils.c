
#include "luautils.h"

char *str_ldup(const char *s, const size_t len) {
	char *dup = malloc(sizeof(char) * (len + 1));
	if (!dup) {
		return NULL;
	}
	strncpy(dup, s, len);
	dup[len] = '\0';
	return dup;
}

void table_geti(lua_State *L, int idx, int i) {
#if LUA_VERSION_NUM < 503
	lua_pushnumber(L, i);
	lua_gettable(L, idx);
#else
	lua_geti(L, idx, i);
#endif
}

int table_rawget(lua_State *L, int idx) {
#if LUA_VERSION_NUM < 503
	lua_rawget(L, idx);
	return lua_type(L, -1);
#else
	return lua_rawget(L, idx);
#endif
}

void pushinteger(lua_State *L, int n) {
#if LUA_VERSION_NUM < 503
	lua_pushnumber(L, n);
#else
	lua_pushinteger(L, n);
#endif
}

void setfuncs(lua_State *L, const luaL_Reg l[]) {
	for (; l->name != NULL; ++l) {
		lua_pushcfunction(L, l->func);
		lua_setfield(L, -2, l->name);
	}
}

void create_libtable(lua_State *L, const luaL_Reg l[]) {
	lua_createtable(L, 0, 0);
	setfuncs(L, l);
}

void create_metatable(
	lua_State *L,
	const char *name,
	const luaL_Reg *metamethods,
	const luaL_Reg *index) {
	luaL_newmetatable(L, name);     // metatable
	setfuncs(L, metamethods);       // metatable
	if (index) {
		lua_newtable(L);                // metatable, table
		setfuncs(L, index);             // metatable, table
		lua_setfield(L, -2, "__index"); // metatable
	}

	// lua <=5.2 doesn't set the __name field which we rely upon for the tests to pass
#if LUA_VERSION_NUM < 503
	lua_pushstring(L, name);        // metatable, name
	lua_setfield(L, -2, "__name");  // metatable
#endif
}

int getfield_type(lua_State *L, int idx, const char *field_name) {
	lua_getfield(L, idx, field_name);
	return lua_type(L, -1);
}

// push the field 'field_name' of the object at idx onto the stack and type check it
// (raises an error if the check fails)
// leaves the value on the stack whether or not the type check passed
bool expect_field(lua_State *L, int idx, const char *field_name, int expected_type) {
	if (lua_type(L, idx) != LUA_TTABLE) {
		luaL_error(L, "expected table");
		return false;
	}
	const int actual_type = getfield_type(L, idx, field_name);
	if (actual_type != expected_type) {
		luaL_error(
			L,
			"expected field `%s' to be of type %s (got %s)",
			field_name,
			lua_typename(L, expected_type),
			lua_typename(L, actual_type));
		return false;
	}
	return true;
}

bool expect_nested_field(lua_State *L, int idx, const char *parent_name, const char *field_name, int expected_type) {
	const int actual_type = getfield_type(L, idx, field_name);
	if (actual_type != expected_type) {
		luaL_error(
			L,
			"expected field `%s.%s' to be of type %s (got %s)",
			parent_name,
			field_name,
			lua_typename(L, expected_type),
			lua_typename(L, actual_type));
		return false;
	}
	return true;
}

// This should only be used for only true indexes, i.e. no lua_upvalueindex, registry stuff, etc.
int absindex(lua_State *L, int idx) {
	return idx > 0 ? idx : lua_gettop(L) + 1 + idx;
}

void setmetatable(lua_State *L, const char *mt_name) {
	luaL_getmetatable(L, mt_name);
	lua_setmetatable(L, -2);
}

static const char ltreesitter_registry_index = 'k';

void setup_registry_index(lua_State *L) {
	lua_pushvalue(L, LUA_REGISTRYINDEX);                           // registry
	lua_pushlightuserdata(L, (void *)&ltreesitter_registry_index); // registry, void *
	lua_newtable(L);                                               // registry, void *, {}
	lua_rawset(L, -3);                                             // registry
}

int push_registry_table(lua_State *L) {
	lua_pushvalue(L, LUA_REGISTRYINDEX);                           // { <Registry> }
	lua_pushlightuserdata(L, (void *)&ltreesitter_registry_index); // { <Registry> }, <void *>
	lua_rawget(L, -2);                                             //  { <Registry> }, { <ltreesitter Registry> }
	lua_remove(L, -2);                                             // { <ltreesitter Registry> }
	return 1;
}

void push_registry_field(lua_State *L, const char *f) {
	push_registry_table(L);
	lua_getfield(L, -1, f);
	lua_remove(L, -2);
}

void set_registry_field(lua_State *L, const char *f) {
	push_registry_table(L);
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, f);
	lua_pop(L, 1);
}

void newtable_with_mode(lua_State *L, const char *mode) {
	lua_newtable(L);
	lua_newtable(L); // {}, {}
	lua_pushstring(L, mode);
	lua_setfield(L, -2, "__mode"); // {}, { __mode = mode }
	lua_setmetatable(L, -2);       // { <metatable> = { __mode = mode } }
}

size_t length_of(lua_State *L, int index) {
#if LUA_VERSION_NUM == 501
	return lua_objlen(L, index);
#else
	lua_len(L, index);
	size_t len = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return len;
#endif
}

#if LUA_VERSION_NUM > 501
void dump_stack(lua_State *L, int from) {
	int top = lua_gettop(L);
	fprintf(stderr, "==============\n");
	fprintf(stderr, "Lua Stack:\n");
	for (int i = from; i <= top; ++i) {
		fprintf(stderr, "   %d: %s\n", i, luaL_tolstring(L, i, NULL));
		lua_pop(L, 1);
	}
	fprintf(stderr, "==============\n");
}
#endif

bool sb_ensure_cap(StringBuilder *sb, size_t n) {
	if (sb->capacity >= n) {
		return true;
	}
	size_t new_cap = sb->capacity * 2;
	if (n > new_cap)
		new_cap = n;
	char *new_data = realloc(sb->data, new_cap);
	if (new_data) {
		sb->capacity = new_cap;
		sb->data = new_data;
		return true;
	}
	return false;
}

void sb_push_char(StringBuilder *sb, char c) {
	sb_ensure_cap(sb, sb->length + 1);
	sb->data[sb->length] = c;
	sb->length += 1;
}

void sb_push_str(StringBuilder *sb, const char *str) {
	size_t len = strlen(str);
	sb_push_lstr(sb, len, str);
}

void sb_push_lstr(StringBuilder *sb, size_t len, const char *str) {
	sb_ensure_cap(sb, sb->length + len);
	memcpy(sb->data + sb->length, str, len);
	sb->length += len;
}

void sb_push_fmt(StringBuilder *sb, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n;
	{
		va_list copy;
		va_copy(copy, args);
		n = vsnprintf(NULL, 0, fmt, copy);
		va_end(copy);
	}
	sb_ensure_cap(sb, sb->length + n + 1);
	sb->length += vsnprintf(sb->data + sb->length, n + 1, fmt, args);
	va_end(args);
}

void sb_free(StringBuilder *sb) {
	free(sb->data);
	*sb = (StringBuilder){0};
}

void sb_push_to_lua(lua_State *L, StringBuilder *sb) {
	lua_pushlstring(L, sb->data, sb->length);
}
