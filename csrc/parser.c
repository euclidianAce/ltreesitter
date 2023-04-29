#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <tree_sitter/api.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "luautils.h"
#include <ltreesitter/dynamiclib.h>
#include <ltreesitter/query.h>
#include <ltreesitter/tree.h>

static const char *parser_cache_index = "parsers";

void setup_parser_cache(lua_State *L) {
	newtable_with_mode(L, "v");
	set_registry_field(L, parser_cache_index);
}

static inline void push_parser_cache(lua_State *L) {
	push_registry_field(L, parser_cache_index);
}

enum ParserLoadErr {
	PARSE_LOAD_ERR_BUFLEN,
	PARSE_LOAD_ERR_DLOPEN,
	PARSE_LOAD_ERR_DLSYM,
	PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD,
	PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW,
	PARSE_LOAD_ERR_NONE,
};

#define TREE_SITTER_SYM "tree_sitter_"
static enum ParserLoadErr try_dlopen(ltreesitter_Parser *p, const char *parser_file, const char *lang_name, uint32_t *out_version) {
	static char buf[128];
	if (snprintf(buf, sizeof(buf) - sizeof(TREE_SITTER_SYM), TREE_SITTER_SYM "%s", lang_name) == 0) {
		return PARSE_LOAD_ERR_BUFLEN;
	}

	if (!ltreesitter_open_dynamic_lib(parser_file, &p->dl)) {
		return PARSE_LOAD_ERR_DLOPEN;
	}

	// ISO C is not a fan of void * -> function pointer
	TSLanguage *(*tree_sitter_lang)(void);
	*(void **)(&tree_sitter_lang) = ltreesitter_dynamic_sym(p->dl, buf);

	if (!tree_sitter_lang) {
		ltreesitter_close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_DLSYM;
	}
	TSParser *parser = ts_parser_new();
	const TSLanguage *lang = tree_sitter_lang();
	const uint32_t version = ts_language_version(lang);
	*out_version = version;
	p->parser = parser;

	if (version < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) {
		ltreesitter_close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD;
	} else if (version > TREE_SITTER_LANGUAGE_VERSION) {
		ltreesitter_close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW;
	}

	ts_parser_set_language(parser, lang);

	return PARSE_LOAD_ERR_NONE;
}

static ltreesitter_Parser *new_parser(lua_State *L) {
	ltreesitter_Parser *const p = lua_newuserdata(L, sizeof(struct ltreesitter_Parser));
	setmetatable(L, LTREESITTER_PARSER_METATABLE_NAME);
	return p;
}

static inline void push_parser_cache_key(lua_State *L, const char *dl_file, const char *lang_name) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addstring(&b, dl_file);
	luaL_addchar(&b, 1);
	luaL_addstring(&b, lang_name);
	luaL_pushresult(&b);
}

static void cache_parser(lua_State *L, const char *dl_file, const char *lang_name) {
	const int parser_idx = lua_gettop(L);
	push_parser_cache(L);                         // cache
	push_parser_cache_key(L, dl_file, lang_name); // parser, cache, "thing.so\1blah"
	lua_pushvalue(L, parser_idx);                 // cache, "thing.so\1blah", parser
	lua_rawset(L, -3);                            // cache
	lua_pop(L, 1);
}

static bool push_cached_parser(lua_State *L, const char *dl_file, const char *lang_name) {
	push_parser_cache(L);                         // cache
	push_parser_cache_key(L, dl_file, lang_name); // cache, "dl_file\1lang_name"

	if (table_rawget(L, -2) != LUA_TNIL) { // cache, nil | parser
		lua_remove(L, -2);
		return true;
	}
	lua_pop(L, 2);
	return false;
}

/* @teal-export load: function(file_name: string, language_name: string): Parser, string [[
   Load a parser from a given file

   Keep in mind that this includes the <code>.so</code> or <code>.dll</code> extension

   On Unix this uses dlopen, on Windows this uses LoadLibrary so if a path without a path separator is given, these functions have their own path's that they will search for your file in.
   So if in doubt use a file path like
   <pre>
   local my_parser = ltreesitter.load("./my_parser.so", "my_language")
   </pre>
]] */
int ltreesitter_load_parser(lua_State *L) {
	lua_settop(L, 2);
	const char *parser_file = luaL_checkstring(L, 1);
	const char *lang_name = luaL_checkstring(L, 2);

	if (push_cached_parser(L, parser_file, lang_name))
		return 1;

	ltreesitter_Parser proxy = {
		.dl = NULL,
		.parser = NULL,
	};
	uint32_t version = 0;
	switch (try_dlopen(&proxy, parser_file, lang_name, &version)) {
	case PARSE_LOAD_ERR_NONE:
		break;
	case PARSE_LOAD_ERR_DLSYM:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to find symbol %s%s", TREE_SITTER_SYM, lang_name);
		return 2;
	case PARSE_LOAD_ERR_DLOPEN:
		lua_pushnil(L);
		lua_pushstring(L, ltreesitter_dynamic_lib_error(proxy.dl));
		return 2;
	case PARSE_LOAD_ERR_BUFLEN:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to copy language name '%s' into buffer", lang_name);
		return 2;
	case PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD:
		lua_pushnil(L);
		lua_pushfstring(L, "%s parser is too old, parser version: %u, minimum version: %u", lang_name, version, TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
		return 2;
	case PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW:
		lua_pushnil(L);
		lua_pushfstring(L, "%s parser is too new, parser version: %u, maximum version: %u", lang_name, version, TREE_SITTER_LANGUAGE_VERSION);
		return 2;
	}

	ltreesitter_Parser *const p = new_parser(L);
	p->dl = proxy.dl;
	p->parser = proxy.parser;

	return 1;
}

/* @teal-export require: function(library_file_name: string, language_name: string): Parser [[
   Search <code>~/.tree-sitter/bin</code> and <code>package.cpath</code> for a parser with the filename <code>library_file_name.so</code> (or <code>.dll</code> on Windows) and try to load the symbol <code>tree_sitter_'language_name'</code>
   <code>language_name</code> is optional and will be set to <code>library_file_name</code> if not provided.

   So if you want to load a Lua parser from a file named <code>lua.so</code> then use <code>ltreesitter.require("lua")</code>
   But if you want to load a Lua parser from a file named <code>parser.so</code> then use <code>ltreesitter.require("parser", "lua")</code>

   Like the regular <code>require</code>, this will error if the parser is not found or the symbol couldn't be loaded. Use either <code>pcall</code> or <code>ltreesitter.load</code> to not error out on failure.

   <pre>
   local my_parser = ltreesitter.require("my_language")
   my_parser:parse_string(...)
   -- etc.
   </pre>
]] */

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

static bool try_load_from_path(
	lua_State *L,
	const char *dl_file,
	const char *lang_name,
	StringBuilder *err_buf
) {
	if (push_cached_parser(L, dl_file, lang_name))
		return true;

	ltreesitter_Parser proxy = {
		.dl = NULL,
		.parser = NULL,
	};
	uint32_t version = 0;
	switch (try_dlopen(&proxy, dl_file, lang_name, &version)) {
	case PARSE_LOAD_ERR_NONE: {
		ltreesitter_Parser *const p = new_parser(L);
		p->dl = proxy.dl;
		p->parser = proxy.parser;

		cache_parser(L, dl_file, lang_name);
		return true;
	}

	case PARSE_LOAD_ERR_BUFLEN:
		sb_push_fmt(err_buf, "\n\tLanguage name '%s' is too long");
		break;

	case PARSE_LOAD_ERR_DLOPEN:
		sb_push_fmt(err_buf, "\n\tTried %s: %s", dl_file, ltreesitter_dynamic_lib_error(proxy.dl));
		break;

	case PARSE_LOAD_ERR_DLSYM:
		sb_push_fmt(err_buf, "\n\tFound %s, but unable to find symbol " TREE_SITTER_SYM "%s", dl_file, lang_name);
		break;

	case PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD:
		sb_push_fmt(
			err_buf,
			"\n\tFound %s, but the version is too old, parser version: %d, minimum version: %d",
			dl_file,
			version,
			TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION
		);
		break;

	case PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW:
		sb_push_fmt(
			err_buf,
			"\n\tFound %s, but the version is too new, parser version: %d, maximum version: %d",
			dl_file,
			version,
			TREE_SITTER_LANGUAGE_VERSION
		);
		break;
	}

	return false;
}

static size_t find_char(const char *str, size_t len, char c) {
	const char *ptr = memchr(str, c, len);
	if (!ptr)
		return len;
	return (size_t)(ptr - str);
}

static void substitute_question_marks(
	StringBuilder *buf,
	const char *path_pattern,
	size_t path_pattern_len,
	const char *to_replace_with
) {
	size_t i = 0;
	while (i < path_pattern_len) {
		const size_t prev_i = i;
		i += find_char(path_pattern + prev_i, path_pattern_len - prev_i, '?');

		if (i > prev_i + 1)
			sb_push_lstr(buf, i - prev_i, path_pattern + prev_i);

		if (i < path_pattern_len)
			sb_push_str(buf, to_replace_with);

		i += 1;
	}

	sb_push_char(buf, 0);

	// fprintf(
		// stderr,
		// "substitute_question_marks(buf=%p,\n"
		// "                          path_pattern=\"%.*s\",\n"
		// "                          path_pattern_len=%zu,\n"
		// "                          to_replace_with=\"%s\") -> \"%s\"\n"
		// ,
		// (void *)buf,
		// (int)path_pattern_len, path_pattern,
		// path_pattern_len,
		// to_replace_with,
		// buf->data
	// );
}

// load from a package.path style list
static bool try_load_from_path_list(
	lua_State *L,
	const char *path_list,
	size_t path_list_len,
	const char *dl_name,
	const char *lang_name,
	StringBuilder *err_buf
) {
	size_t start = 0;
	size_t end = 0;

	StringBuilder buf = {0};
	while (end < path_list_len) {
		buf.length = 0;
		start = end;
		end += find_char(path_list + start, path_list_len - start, ';');
		const size_t len = end - start;
		substitute_question_marks(&buf, path_list + start, len, dl_name);

		if (try_load_from_path(L, buf.data, lang_name, err_buf)) {
			sb_free(&buf);
			return true;
		}
		end += 1;
	}

	sb_free(&buf);
	return false;
}

int ltreesitter_require_parser(lua_State *L) {
	// grab args
	lua_settop(L, 2);
	const char *so_name = luaL_checkstring(L, 1);
	const char *lang_name = luaL_optstring(L, 2, so_name);

	lua_getglobal(L, "package");  // lang_name, <ts path>, package
	lua_getfield(L, -1, "cpath"); // lang_name, <ts path>, package, package.cpath
	lua_remove(L, -2);            // lang_name, <ts path>, package.cpath
	size_t cpath_len;
	const char *cpath = lua_tolstring(L, -1, &cpath_len);

	// buffer to build up search paths in error message
	StringBuilder err_buf = {0};
	sb_push_str(&err_buf, "Unable to load parser for ");
	sb_push_str(&err_buf, lang_name);
	bool ok;

#define CHECK(x) do { \
	ok = (x); \
	if (!ok) break; \
	sb_free(&err_buf); \
	return 1; \
} while (0)

	CHECK(try_load_from_path_list(L, cpath, cpath_len, so_name, lang_name, &err_buf));

#undef CHECK

	if (!ok) {
		sb_push_to_lua(L, &err_buf);
		sb_free(&err_buf);
		return lua_error(L);
	}
	return 1;
}

static int parser_gc(lua_State *L) {
	ltreesitter_Parser *lp = luaL_checkudata(L, 1, LTREESITTER_PARSER_METATABLE_NAME);
#ifdef LOG_GC
	printf("Parser p: %p is being garbage collected\n", (void const *)lp);
	printf("   p->dl: %p\n", lp->dl);
#endif
	ts_parser_delete(lp->parser);
	ltreesitter_close_dynamic_lib(lp->dl);

	return 0;
}

ltreesitter_Parser *ltreesitter_check_parser(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_PARSER_METATABLE_NAME);
}

/* @teal-export Parser.parse_string: function(Parser, string, Tree): Tree [[
   Uses the given parser to parse the string

   If <code>Tree</code> is provided then it will be used to create a new updated tree
   (but it is the responsibility of the programmer to make the correct <code>Tree:edit</code> calls)

   Could return <code>nil</code> if the parser has a timeout
]] */
int ltreesitter_parser_parse_string(lua_State *L) {
	lua_settop(L, 3);
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	size_t len;
	const char *to_parse = luaL_checklstring(L, 2, &len);

	TSTree *old_tree;
	if (lua_type(L, 3) == LUA_TNIL) {
		old_tree = NULL;
	} else {
		old_tree = ltreesitter_check_tree_arg(L, 3)->tree;
	}

	TSTree *tree = ts_parser_parse_string(p->parser, old_tree, to_parse, len);
	if (!tree) {
		lua_pushnil(L);
		return 1;
	}

	ltreesitter_push_tree(L, tree, len, to_parse);
	return 1;
}

enum ReadError {
	READERR_NONE,
	READERR_PCALL,
	READERR_TYPE,
};
struct CallInfo {
	lua_State *L;
	StringBuilder string_builder;
	enum ReadError read_error;
};
static const char *ltreesitter_parser_read(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read) {
	struct CallInfo *const i = payload;
	lua_State *const L = i->L;
	lua_pushvalue(L, -1); // grab a copy of the function
	pushinteger(L, byte_index);

	lua_newtable(L);                 // byte_index, {}
	pushinteger(L, position.row);    // byte_index, {}, row
	lua_setfield(L, -2, "row");      // byte_index, { row = row }
	pushinteger(L, position.column); // byte_index, { row = row }, column
	lua_setfield(L, -2, "column");   // byte_index, { row = row, column = column }

	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		i->read_error = READERR_PCALL;
		*bytes_read = 0;
		return NULL;
	}

	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		*bytes_read = 0;
		return NULL;
	}

	if (lua_type(L, -1) != LUA_TSTRING) {
		i->read_error = READERR_TYPE;
		*bytes_read = 0;
		return NULL;
	}

	size_t n = 0;
	const char *read_str = lua_tolstring(L, -1, &n);
	if (!sb_ensure_cap(&i->string_builder, byte_index + n)) {
		ALLOC_FAIL(L);
		*bytes_read = 0;
		return NULL;
	}

	memcpy(&i->string_builder.data[byte_index], read_str, n);
	if (byte_index + *bytes_read > i->string_builder.length) {
		i->string_builder.length = byte_index + n;
	}

	lua_pop(L, 1);

	*bytes_read = n;
	return read_str;
}

/* @teal-export Parser.parse_with: function(Parser, reader: function(integer, Point): (string), old_tree: Tree): Tree [[
   <code>reader</code> should be a function that takes a byte index
   and a <code>Point</code> and returns the text at that point. The
   function should return either <code>nil</code> or an empty string
   to signal that there is no more text.

   A <code>Tree</code> can be provided to reuse parts of it for parsing,
   provided the <code>Tree:edit</code> has been called previously
]] */
int ltreesitter_parser_parse_with(lua_State *L) {
	lua_settop(L, 3);
	ltreesitter_Parser *const p = ltreesitter_check_parser(L, 1);
	TSTree *old_tree = NULL;
	if (!lua_isnil(L, 3)) {
		old_tree = ltreesitter_check_tree_arg(L, 3)->tree;
	}
	lua_pop(L, 1);
	struct CallInfo payload = {
		.L = L,
		.read_error = READERR_NONE,
		.string_builder = {0},
	};

	TSInput input = (TSInput){
		.read = ltreesitter_parser_read,
		.payload = &payload,
		.encoding = TSInputEncodingUTF8,
	};

	TSTree *t = ts_parser_parse(p->parser, old_tree, input);

	switch (payload.read_error) {
	case READERR_PCALL:
		return luaL_error(L, "read error: Provided function errored: %s", lua_tostring(L, -1));
	case READERR_TYPE:
		return luaL_error(L, "read error: Provided function returned %s (expected string)", lua_typename(L, lua_type(L, -1)));
	case READERR_NONE:
	default:
		break;
	}

	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	ltreesitter_push_tree(L, t, payload.string_builder.length, payload.string_builder.data);
	sb_free(&payload.string_builder);

	return 1;
}

/* @teal-export Parser.set_timeout: function(Parser, integer) [[
   Sets how long the parser is allowed to take in microseconds
]] */
int ltreesitter_parser_set_timeout(lua_State *L) {
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	const lua_Number n = luaL_checknumber(L, 2);
	luaL_argcheck(L, n >= 0, 2, "expected non-negative integer");
	ts_parser_set_timeout_micros(p->parser, (uint64_t)n);
	return 0;
}

/* @teal-inline [[
   record Range
      start_byte: integer
      end_byte: integer

      start_point: Point
      end_point: Point
   end
]]*/

static TSPoint topoint(lua_State *L, const int idx) {
	const int absidx = absindex(L, idx);
	expect_field(L, absidx, "row", LUA_TNUMBER);
	expect_field(L, absidx, "column", LUA_TNUMBER);
	const uint32_t row = lua_tonumber(L, -2);
	const uint32_t col = lua_tonumber(L, -1);
	lua_pop(L, 2);
	return (TSPoint){
		.row = row,
		.column = col,
	};
}

/* @teal-export Parser.set_ranges: function(Parser, {Range}): boolean [[
   Sets the ranges that <code>Parser</code> will include when parsing, so you don't have to parse an entire document, but the ranges in the tree will still match the document.
   The array of <code>Range</code>s must satisfy the following relationship: for a positive integer <code>i</code> within the length of <code>ranges: {Range}</code>:
   <pre>
   ranges[i].end_byte <= ranges[i + 1].start_byte
   </pre>

   returns whether or not setting the range succeeded
]]*/
static int parser_set_ranges(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);

	if (lua_isnil(L, 2)) {
		lua_pushboolean(L, ts_parser_set_included_ranges(p->parser, NULL, 0));
		return 1;
	}

	size_t len = length_of(L, -1);
	TSRange *ranges = malloc(len * sizeof(TSRange));
	if (!ranges)
		return ALLOC_FAIL(L);

#define COPY_FIELD(field_name, field_type, method)           \
	do {                                                     \
		if (!expect_field(L, -1, #field_name, field_type)) { \
			free(ranges);                                    \
			return 0;                                        \
		}                                                    \
		ranges[i].field_name = method(L, -1);                \
		lua_pop(L, 1);                                       \
	} while (0)

	for (size_t i = 0; i < len; ++i) {
		table_geti(L, 2, i + 1);
		COPY_FIELD(start_byte, LUA_TNUMBER, lua_tonumber);
		COPY_FIELD(end_byte, LUA_TNUMBER, lua_tonumber);
		COPY_FIELD(start_point, LUA_TTABLE, topoint);
		COPY_FIELD(end_point, LUA_TTABLE, topoint);
		lua_pop(L, 1);

		if (i > 0 && ranges[i - 1].end_byte > ranges[i].start_byte) {
			return luaL_error(L, "Error in ranges: range[%d].end_byte (%d) is greater than range[%d].start_byte (%d)", i, (int)ranges[i - 1].end_byte, i + 1, (int)ranges[i].start_byte);
		}
	}

#undef COPY_FIELD

	lua_pushboolean(L, ts_parser_set_included_ranges(p->parser, ranges, len));
	free(ranges);
	return 1;
}

#define SET_FIELD(L, push_fn, struct_ptr, field_name) \
	do {                                              \
		push_fn(L, (struct_ptr)->field_name);         \
		lua_setfield(L, -2, #field_name);             \
	} while (0);

#define SET_FIELD_P(L, push_fn, struct_ptr, field_name) \
	do {                                                \
		push_fn(L, &(struct_ptr)->field_name);          \
		lua_setfield(L, -2, #field_name);               \
	} while (0);

static void push_point(lua_State *L, TSPoint const *point) {
	lua_createtable(L, 0, 2);
	SET_FIELD(L, pushinteger, point, row);
	SET_FIELD(L, pushinteger, point, column);
}

static void push_range(lua_State *L, TSRange const *range) {
	lua_createtable(L, 0, 4);
	SET_FIELD(L, pushinteger, range, start_byte);
	SET_FIELD(L, pushinteger, range, end_byte);
	SET_FIELD_P(L, push_point, range, start_point);
	SET_FIELD_P(L, push_point, range, end_point);
}

/* @teal-export Parser.get_ranges: function(Parser): {Range} [[
   Get the ranges of text that the parser will include when parsing
]] */
static int parser_get_ranges(lua_State *L) {
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);

	uint32_t length = 0;
	const TSRange *ranges = ts_parser_included_ranges(p->parser, &length);
	lua_createtable(L, (int)length, 0);
	for (uint32_t i = 0; i < length; ++i) {
		push_range(L, ranges + i);
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

/* @teal-export Parser.query: function(Parser, string): Query [[
   Create a query out of the given string for the language of the given parser
]] */
int make_query(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	size_t len;
	const char *lua_query_src = luaL_checklstring(L, 2, &len);
	uint32_t err_offset = 0;
	TSQueryError err_type = TSQueryErrorNone;
	const TSLanguage *lang = ts_parser_language(p->parser);
	TSQuery *q = ts_query_new(
		lang,
		lua_query_src,
		len,
		&err_offset,
		&err_type);
	ltreesitter_handle_query_error(L, q, err_offset, err_type, lua_query_src);

	if (q)
		ltreesitter_push_query(L, lang, lua_query_src, len, q, 1);
	else
		lua_pushnil(L);

	return 1;
}

/* @teal-export Parser.get_version: function(Parser): integer [[
   get the api version of the parser's language
]] */
static int get_version(lua_State *L) {
	ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	pushinteger(L, ts_language_version(ts_parser_language(p->parser)));
	return 1;
}

static const luaL_Reg parser_methods[] = {
	{"set_timeout", ltreesitter_parser_set_timeout},
	{"set_ranges", parser_set_ranges},
	{"get_ranges", parser_get_ranges},
	{"get_version", get_version},

	{"parse_string", ltreesitter_parser_parse_string},
	{"parse_with", ltreesitter_parser_parse_with},
	{"query", make_query},

	{NULL, NULL}};
static const luaL_Reg parser_metamethods[] = {
	{"__gc", parser_gc},
	{NULL, NULL}};

void ltreesitter_create_parser_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_PARSER_METATABLE_NAME, parser_metamethods, parser_methods);
}
