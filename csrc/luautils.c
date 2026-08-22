#include "luautils.h"
#include "pave.h"
#include <stdio.h>
#include <inttypes.h>

char *str_ldup(char const *s, const size_t len) {
	char *dup = malloc(sizeof(char) * (len + 1));
	if pave_unlikely(!dup)
		return NULL;
	memcpy(dup, s, len);
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
	char const *name,
	luaL_Reg const *metamethods,
	luaL_Reg const *index) {
	luaL_newmetatable(L, name); // metatable
	setfuncs(L, metamethods);   // metatable
	if (index) {
		lua_newtable(L);                // metatable, table
		setfuncs(L, index);             // metatable, table
		lua_setfield(L, -2, "__index"); // metatable
	}

	// lua <=5.2 doesn't set the __name field which we rely upon for the tests to pass
#if LUA_VERSION_NUM < 503
	lua_pushstring(L, name);       // metatable, name
	lua_setfield(L, -2, "__name"); // metatable
#endif
}

int getfield_type(lua_State *L, int idx, char const *field_name) {
	lua_getfield(L, idx, field_name);
	return lua_type(L, -1);
}

// push the field 'field_name' of the object at idx onto the stack and type check it
// (raises an error if the check fails)
// leaves the value on the stack whether or not the type check passed
bool expect_field(lua_State *L, int idx, char const *field_name, int expected_type) {
	if pave_unlikely(lua_type(L, idx) != LUA_TTABLE) {
		luaL_error(L, "expected table");
		return false;
	}
	int const actual_type = getfield_type(L, idx, field_name);
	if pave_unlikely(actual_type != expected_type) {
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

bool expect_nested_field(lua_State *L, int idx, char const *parent_name, char const *field_name, int expected_type) {
	int const actual_type = getfield_type(L, idx, field_name);
	if pave_unlikely(actual_type != expected_type) {
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

void setmetatable(lua_State *L, char const *mt_name) {
	luaL_getmetatable(L, mt_name);
	lua_setmetatable(L, -2);
}

void *testudata(lua_State *L, int idx, char const *tname) {
#if LUA_VERSION_NUM < 502
	// Adapted from lua 5.4 source
	void *p = lua_touserdata(L, idx);
	if pave_unlikely(!p)
		return NULL;
	if pave_unlikely(!lua_getmetatable(L, idx)) // t1
		return NULL;
	luaL_getmetatable(L, tname); // t1, t2
	if pave_unlikely(!lua_rawequal(L, -1, -2))
		p = NULL;
	lua_pop(L, 2);
	return p;
#else
	return luaL_testudata(L, idx, tname);
#endif
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
	lua_rawget(L, -2);                                             // { <Registry> }, { <ltreesitter Registry> }
	lua_remove(L, -2);                                             // { <ltreesitter Registry> }
	return 1;
}

int ref_into_registry(lua_State *L, int object_to_ref) {
	lua_pushvalue(L, object_to_ref); // object
	push_registry_table(L);          // object, { <ltreesitter registry> }
	lua_insert(L, -2);               // { }, object
	int ref = luaL_ref(L, -2);       // { }
	lua_pop(L, 1);
	return ref;
}

void unref_from_registry(lua_State *L, int ref_to_unref) {
	push_registry_table(L); // { <ltreesitter registry> }
	luaL_unref(L, -1, ref_to_unref);
	lua_pop(L, 1); // <empty>
}

bool push_ref_from_registry(lua_State *L, int ref) {
	push_registry_table(L);          // { <ltreesitter registry> }
	int type = table_rawget(L, ref); // {}, object
	lua_insert(L, -2);               // object, {}
	lua_pop(L, 1);                   // object
	return type != LUA_TNIL;
}

void push_registry_field(lua_State *L, char const *f) {
	push_registry_table(L);
	lua_getfield(L, -1, f);
	lua_remove(L, -2);
}

void set_registry_field(lua_State *L, char const *f) {
	push_registry_table(L);
	lua_pushvalue(L, -2);
	lua_setfield(L, -2, f);
	lua_pop(L, 1);
}

void newtable_with_mode(lua_State *L, bool weak_keys, bool weak_values) {
	lua_newtable(L);
	lua_newtable(L); // {}, {}
	static char const *modes[] = { "", "k", "v", "kv" };
	lua_pushstring(L, modes[weak_keys + (weak_values << 1)]);
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

bool sb_push_char(StringBuilder *sb, char c) {
	if pave_unlikely(!sb_ensure_cap(sb, sb->length + 1))
		return false;
	sb->data[sb->length] = c;
	sb->length += 1;
	return true;
}

bool sb_push_str(StringBuilder *sb, char const *str) {
	size_t len = strlen(str);
	return sb_push_lstr(sb, len, str);
}

bool sb_push_lstr(StringBuilder *sb, size_t len, char const *str) {
	if pave_unlikely(!sb_ensure_cap(sb, sb->length + len))
		return false;
	memcpy(sb->data + sb->length, str, len);
	sb->length += len;
	return true;
}

bool sb_push_fmt(StringBuilder *sb, char const *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n;
	{
		va_list copy;
		va_copy(copy, args);
		n = vsnprintf(NULL, 0, fmt, copy);
		va_end(copy);
	}
	if pave_unlikely(!sb_ensure_cap(sb, sb->length + n + 1)) {
		va_end(args);
		return false;
	}
	sb->length += vsnprintf(sb->data + sb->length, n + 1, fmt, args);
	va_end(args);
	return true;
}

void sb_free(StringBuilder *sb) {
	free(sb->data);
	*sb = (StringBuilder){0};
}

void sb_push_to_lua(lua_State *L, StringBuilder *sb) {
	lua_pushlstring(L, sb->data, sb->length);
}

void mos_free(MaybeOwnedString *s) {
	if (s->owned)
		free((void *)s->data);
	s->data = NULL;
	s->length = 0;
	s->owned = false;
}

void mos_push_to_lua(lua_State *L, MaybeOwnedString s) {
	lua_pushlstring(L, s.data, s.length);
}

bool mos_eq(MaybeOwnedString a, MaybeOwnedString b) {
	return a.length == b.length && memcmp(a.data, b.data, a.length) == 0;
}

bool test_int(lua_State *L, int index, lua_Integer *out) {
#if LUA_VERSION_NUM < 503
	if pave_unlikely(!lua_isnumber(L, index)) return false;
#else
	if pave_unlikely(!lua_isinteger(L, index)) return false;
#endif

	*out = lua_tointeger(L, index);
	return true;
}

bool test_u32(lua_State *L, int idx, uint32_t *out) {
	lua_Integer arg;
	if pave_unlikely(!test_int(L, idx, &arg)) return false;
	if pave_unlikely(arg < 0) return false;
	if pave_unlikely(arg > (lua_Integer)UINT32_MAX) return false;
	*out = (uint32_t)arg;
	return true;
}

bool test_u32_one_index(lua_State *L, int idx, uint32_t end_inclusive, uint32_t *out) {
	lua_Integer arg;
	if pave_unlikely(!test_int(L, idx, &arg)) return false;
	if pave_unlikely(arg < 0) return false;
	if pave_unlikely(arg > (lua_Integer)end_inclusive) return false;
	*out = (uint32_t)arg;
	return true;
}

bool test_u32_zero_index(lua_State *L, int idx, uint32_t end_exclusive, uint32_t *out) {
	lua_Integer arg = luaL_checkinteger(L, idx);
	if pave_unlikely(arg < 0) return false;
	if pave_unlikely(arg >= (lua_Integer)end_exclusive) return false;
	*out = (uint32_t)arg;
	return true;
}

bool clamp_u32(lua_State *L, int idx, uint32_t *out) {
	lua_Integer arg;
	if pave_unlikely(!test_int(L, idx, &arg))
		return false;
	if pave_unlikely(arg < 0)
		*out = 0;
	else if pave_unlikely(arg > (lua_Integer)UINT32_MAX)
		*out = UINT32_MAX;
	else
		*out = (uint32_t)arg;
	return true;
}

uint32_t clamp_u32_or_argerror(lua_State *L, int idx) {
	uint32_t result;
	if pave_unlikely(!clamp_u32(L, idx, &result)) {
		char buf[128];
		snprintf(buf, sizeof buf, "Expected integer, got `%s'", lua_typename(L, lua_type(L, idx)));
		luaL_argerror(L, idx, buf);
	}
	return result;
}

// ( any -- )
static uint32_t u32_check(lua_State *L, int argument_index, char const *field_name) {
	char buf[256];
	uint32_t arg = 0;
	if pave_unlikely(!clamp_u32(L, -1, &arg)) {
		snprintf(buf, sizeof buf, "Expected field `%s' to be an integer, but got a `%s'", field_name, lua_typename(L, lua_type(L, -1)));
		luaL_argerror(L, argument_index, buf);
	}
	lua_pop(L, 1);
	return arg;
}

TSInputEdit expect_edit_table_arg(lua_State *L, int arg) {
	TSInputEdit edit;

	luaL_argcheck(L, lua_type(L, arg) == LUA_TTABLE, 2, "expected table");

	expect_field(L, arg, "start_byte", LUA_TNUMBER);
	edit.start_byte = u32_check(L, arg, "start_byte");
	expect_field(L, arg, "old_end_byte", LUA_TNUMBER);
	edit.old_end_byte = u32_check(L, arg, "old_end_byte");
	expect_field(L, arg, "new_end_byte", LUA_TNUMBER);
	edit.new_end_byte = u32_check(L, arg, "new_end_byte");

	expect_field(L, arg, "start_point", LUA_TTABLE);
	expect_field(L, -1, "row", LUA_TNUMBER);
	edit.start_point.row = u32_check(L, arg, "start_point.row");
	expect_field(L, -1, "column", LUA_TNUMBER);
	edit.start_point.column = u32_check(L, arg, "start_point.column");
	lua_pop(L, 1);

	expect_field(L, arg, "old_end_point", LUA_TTABLE);
	expect_nested_field(L, -1, "old_end_point", "row", LUA_TNUMBER);
	edit.old_end_point.row = u32_check(L, arg, "old_end_point.row");
	expect_nested_field(L, -1, "old_end_point", "column", LUA_TNUMBER);
	edit.old_end_point.column = u32_check(L, arg, "old_end_point.column");
	lua_pop(L, 1);

	expect_field(L, arg, "new_end_point", LUA_TTABLE);
	expect_nested_field(L, -1, "new_end_point", "row", LUA_TNUMBER);
	edit.new_end_point.row = u32_check(L, arg, "new_end_point.row");
	expect_nested_field(L, -1, "new_end_point", "column", LUA_TNUMBER);
	edit.new_end_point.column = u32_check(L, arg, "new_end_point.column");
	lua_pop(L, 1);

	return edit;
}

TSInputEdit expect_edit_positional_args(lua_State *L, int first_arg) {
	TSInputEdit edit;

	edit.start_byte           = clamp_u32_or_argerror(L, first_arg + 0);
	edit.old_end_byte         = clamp_u32_or_argerror(L, first_arg + 1);
	edit.new_end_byte         = clamp_u32_or_argerror(L, first_arg + 2);

	edit.start_point.row      = clamp_u32_or_argerror(L, first_arg + 3);
	edit.start_point.column   = clamp_u32_or_argerror(L, first_arg + 4);

	edit.old_end_point.row    = clamp_u32_or_argerror(L, first_arg + 5);
	edit.old_end_point.column = clamp_u32_or_argerror(L, first_arg + 6);

	edit.new_end_point.row    = clamp_u32_or_argerror(L, first_arg + 7);
	edit.new_end_point.column = clamp_u32_or_argerror(L, first_arg + 8);

	return edit;
}

#ifndef LUA_FILEHANDLE
#define LUA_FILEHANDLE "FILE*"
#endif

FILE *testfile(lua_State *L, int idx) {
	// either a `FILE**` or a `luaL_Stream*`, but `luaL_Stream`'s first member
	// is a `FILE*` so its fine (per the C standard) to reference it as a
	// pointer to its first field

	FILE **ptr = testudata(L, idx, LUA_FILEHANDLE);
	return ptr ? *ptr : NULL;
}

int fd_from_file(FILE *f) {
	int fd = -1;
#ifdef _WIN32
	__declspec(dllimport) int _fileno(FILE *);
	fd = _fileno(f);
#else
	int fileno(FILE *);
	fd = fileno(f);
#endif
	return fd;
}

TSPoint to_clamped_point(lua_State *L, int const idx) {
	int const absidx = absindex(L, idx);
	TSPoint result;
	if pave_unlikely(getfield_type(L, absidx, "row") != LUA_TNUMBER || !clamp_u32(L, -1, &result.row))
		luaL_error(L, "Expected `row' of point to be an integer, got `%s'", lua_typename(L, lua_type(L, -1)));
	if pave_unlikely(getfield_type(L, absidx, "column") != LUA_TNUMBER || !clamp_u32(L, -1, &result.column))
		luaL_error(L, "Expected `column' of point to be an integer, got `%s'", lua_typename(L, lua_type(L, -1)));
	lua_pop(L, 2);
	return result;
}

void push_point(lua_State *L, TSPoint point) {
	lua_createtable(L, 0, 2);
	pushinteger(L, point.row); lua_setfield(L, -2, "row");
	pushinteger(L, point.column); lua_setfield(L, -2, "column");
}

void push_range(lua_State *L, TSRange range) {
	lua_createtable(L, 0, 4);
	pushinteger(L, range.start_byte); lua_setfield(L, -2, "start_byte");
	pushinteger(L, range.end_byte); lua_setfield(L, -2, "end_byte");
	push_point(L, range.start_point); lua_setfield(L, -2, "start_point");
	push_point(L, range.end_point); lua_setfield(L, -2, "end_point");
}
