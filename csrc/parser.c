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
#include <ltreesitter/types.h>

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
static enum ParserLoadErr try_dlopen(struct ltreesitter_Parser *p, const char *parser_file, const char *lang_name) {
	static char buf[128];
	if (snprintf(buf, sizeof(buf) - sizeof(TREE_SITTER_SYM), TREE_SITTER_SYM "%s", lang_name) == 0) {
		return PARSE_LOAD_ERR_BUFLEN;
	}

	if (!open_dynamic_lib(parser_file, &p->dl)) {
		return PARSE_LOAD_ERR_DLOPEN;
	}

	// ISO C is not a fan of void * -> function pointer
	TSLanguage *(*tree_sitter_lang)(void) = (TSLanguage * (*)(void)) dynamic_sym(p->dl, buf);

	if (!tree_sitter_lang) {
		close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_DLSYM;
	}
	TSParser *parser = ts_parser_new();
	const TSLanguage *lang = tree_sitter_lang();
	const uint32_t version = ts_language_version(lang);
	p->lang_version = version;
	p->lang = lang;
	p->parser = parser;

	if (version < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) {
		close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD;
	} else if (version > TREE_SITTER_LANGUAGE_VERSION) {
		close_dynamic_lib(p->dl);
		return PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW;
	}

	ts_parser_set_language(parser, lang);

	return PARSE_LOAD_ERR_NONE;
}

static struct ltreesitter_Parser *new_parser(lua_State *L) {
	struct ltreesitter_Parser *const p = lua_newuserdata(L, sizeof(struct ltreesitter_Parser));
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

	struct ltreesitter_Parser proxy = {
	    .dl = NULL,
	    .parser = NULL,
	    .lang = NULL,
	};
	switch (try_dlopen(&proxy, parser_file, lang_name)) {
	case PARSE_LOAD_ERR_NONE:
		break;
	case PARSE_LOAD_ERR_DLSYM:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to find symbol %s%s", TREE_SITTER_SYM, lang_name);
		return 2;
	case PARSE_LOAD_ERR_DLOPEN:
		lua_pushnil(L);
		lua_pushstring(L, dynamic_lib_error(proxy.dl));
		return 2;
	case PARSE_LOAD_ERR_BUFLEN:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to copy language name '%s' into buffer", lang_name);
		return 2;
	case PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD:
		lua_pushnil(L);
		lua_pushfstring(L, "%s parser is too old, parser version: %u, minimum version: %u", lang_name, proxy.lang_version, TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
		return 2;
	case PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW:
		lua_pushnil(L);
		lua_pushfstring(L, "%s parser is too new, parser version: %u, maximum version: %u", lang_name, proxy.lang_version, TREE_SITTER_LANGUAGE_VERSION);
		return 2;
	}

	struct ltreesitter_Parser *const p = new_parser(L);
	p->dl = proxy.dl;
	p->parser = proxy.parser;
	p->lang = proxy.lang;

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
static inline void buf_add_str(luaL_Buffer *b, const char *s) {
	luaL_addlstring(b, s, strlen(s));
}

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif
int ltreesitter_require_parser(lua_State *L) {
	// grab args
	lua_settop(L, 2);
	const char *so_name = luaL_checkstring(L, 1);
	const size_t so_len = strlen(so_name);
	const char *lang_name = luaL_optstring(L, 2, so_name);
	const size_t lang_len = strlen(lang_name);

	const char *user_home = getenv("HOME");
	if (user_home) {
		lua_pushfstring(L, "%s" PATH_SEP ".tree-sitter" PATH_SEP "bin" PATH_SEP "?." DL_EXT ";", user_home);
	}

	// prepend ~/.tree-sitter/bin/?.so to package.cpath
	lua_getglobal(L, "package");  // lang_name, <ts path>, package
	lua_getfield(L, -1, "cpath"); // lang_name, <ts path>, package, package.cpath
	lua_remove(L, -2);            // lang_name, <ts path>, package.cpath

	if (user_home) {
		lua_concat(L, 2);
	}

	// lang_name, package.cpath
	const char *cpath = lua_tostring(L, -1);
	const size_t buf_size = strlen(cpath);
	char *buf = malloc(sizeof(char) * (buf_size + lang_len));
	if (!buf)
		return ALLOC_FAIL(L);

	// buffer to build up search paths in error message
	luaL_Buffer b;

	luaL_buffinit(L, &b);
	buf_add_str(&b, "Unable to load parser for ");
	buf_add_str(&b, lang_name);

	// Do an imitation of a package.searchpath
	//	Searchpath will just return the first path which we may be able to open,
	//	but it may not have the symbol we want, so we should keep searching afterward
	ssize_t j = 0;
	for (size_t i = 0; i <= buf_size; ++i, ++j) {
		// cpath doesn't necessarily end with a ; so lets pretend it does
		char c;
		if (i == buf_size)
			c = ';';
		else
			c = cpath[i];

		switch (c) {
		case '?':
			for (size_t k = 0; k < so_len; ++k, ++j) {
				buf[j] = so_name[k];
			}
			--j;
			break;
		case ';': {
			buf[j] = '\0';

			if (push_cached_parser(L, buf, lang_name)) {
				luaL_pushresult(&b);
				lua_pop(L, 1);
				free(buf);
				return 1;
			}

			struct ltreesitter_Parser proxy = (struct ltreesitter_Parser){
			    .dl = NULL,
			    .parser = NULL,
			    .lang = NULL,
			};
			switch (try_dlopen(&proxy, buf, lang_name)) {
			case PARSE_LOAD_ERR_NONE: {
				luaL_pushresult(&b);
				struct ltreesitter_Parser *const p = new_parser(L);
				p->dl = proxy.dl;
				p->parser = proxy.parser;
				p->lang = proxy.lang;

				cache_parser(L, buf, lang_name);
				free(buf);
				return 1;
			}
			case PARSE_LOAD_ERR_DLSYM:
				buf_add_str(&b, "\n\tFound ");
				luaL_addlstring(&b, buf, j);
				buf_add_str(&b, ":\n\t\tunable to find symbol " TREE_SITTER_SYM);
				buf_add_str(&b, lang_name);
				break;
			case PARSE_LOAD_ERR_DLOPEN:
				buf_add_str(&b, "\n\tTried ");
				luaL_addlstring(&b, buf, j);
				buf_add_str(&b, ": ");
				buf_add_str(&b, dynamic_lib_error(proxy.dl));
				break;
			case PARSE_LOAD_ERR_BUFLEN:
				buf_add_str(&b, "\n\tUnable to copy langauge name '");
				buf_add_str(&b, lang_name);
				buf_add_str(&b, "' into buffer");
				goto err_cleanup;
			case PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD:
				buf_add_str(&b, "\n\tthe found ");
				buf_add_str(&b, lang_name);
				buf_add_str(&b, " parser (at ");
				buf_add_str(&b, buf);
				buf_add_str(&b, ") is too old, parser version: ");
				lua_pushfstring(L, "%d", proxy.lang_version);
				luaL_addvalue(&b);
				buf_add_str(&b, ", minimum version: ");
				lua_pushfstring(L, "%d", TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
				luaL_addvalue(&b);
				buf_add_str(&b, ". Either find a newer parser or recompile ltreesitter with an older tree-sitter api version");
				goto err_cleanup;
			case PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW:
				buf_add_str(&b, "\n\tthe found ");
				buf_add_str(&b, lang_name);
				buf_add_str(&b, " parser (at ");
				buf_add_str(&b, buf);
				buf_add_str(&b, ") is too new, parser version: ");
				lua_pushfstring(L, "%d", proxy.lang_version);
				luaL_addvalue(&b);
				buf_add_str(&b, ", maximum version: ");
				lua_pushfstring(L, "%d", TREE_SITTER_LANGUAGE_VERSION);
				luaL_addvalue(&b);
				buf_add_str(&b, ". Either find an older parser or recompile ltreesitter with a newer tree-sitter api version");
				goto err_cleanup;
			}

			j = -1;
			break;
		}
		default:
			buf[j] = cpath[i];
			break;
		}
	}

err_cleanup:
	free(buf);

	luaL_pushresult(&b);
	return lua_error(L);
}

static int parser_gc(lua_State *L) {
	struct ltreesitter_Parser *lp = luaL_checkudata(L, 1, LTREESITTER_PARSER_METATABLE_NAME);
#ifdef LOG_GC
	printf("Parser p: %p is being garbage collected\n", lp);
	printf("   p->dl: %p\n", lp->dl);
#endif
	ts_parser_delete(lp->parser);
	close_dynamic_lib(lp->dl);

	return 0;
}

struct ltreesitter_Parser *ltreesitter_check_parser(lua_State *L, int idx) {
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
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	size_t len;
	const char *str = luaL_checklstring(L, 2, &len);
	const char *copy = str_ldup(str, len);
	if (!copy)
		return ALLOC_FAIL(L);

	TSTree *old_tree;
	if (lua_type(L, 3) == LUA_TNIL) {
		old_tree = NULL;
	} else {
		old_tree = ltreesitter_check_tree_arg(L, 3)->tree;
	}

	TSTree *tree = ts_parser_parse_string(p->parser, old_tree, str, len);
	if (!tree) {
		lua_pushnil(L);
		return 1;
	}

	push_tree(L, p->lang, tree, true, copy, len);
	return 1;
}

enum ReadError {
	READERR_NONE,
	READERR_PCALL,
	READERR_TYPE,
};
struct CallInfo {
	lua_State *L;
	struct {
		size_t len;
		size_t real_len;
		char *str;
	} string_builder;
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

	const char *read_str = lua_tolstring(L, -1, (size_t *)bytes_read);
	while (i->string_builder.real_len < byte_index + *bytes_read) {
		i->string_builder.real_len *= 2;
		i->string_builder.str = realloc(i->string_builder.str, i->string_builder.real_len);
		if (!i->string_builder.str) {
			ALLOC_FAIL(L);
			*bytes_read = 0;
			return NULL;
		}
	}
	memcpy(&i->string_builder.str[byte_index], read_str, *bytes_read);
	if (byte_index + *bytes_read > i->string_builder.len) {
		i->string_builder.len = byte_index + *bytes_read;
	}

	lua_pop(L, 1);

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
	struct ltreesitter_Parser *const p = ltreesitter_check_parser(L, 1);
	TSTree *old_tree = NULL;
	if (!lua_isnil(L, 3)) {
		old_tree = ltreesitter_check_tree_arg(L, 3)->tree;
	}
	lua_pop(L, 1);
	struct CallInfo payload = {
	    .L = L,
	    .read_error = READERR_NONE,
	    .string_builder = {
	        .len = 0,
	        .real_len = 64,
	        .str = malloc(sizeof(char) * 64),
	    },
	};
	if (!payload.string_builder.str) {
		return ALLOC_FAIL(L);
	}

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
	push_tree(L, p->lang, t, true, payload.string_builder.str, payload.string_builder.len);

	return 1;
}

/* @teal-export Parser.set_timeout: function(Parser, integer) [[
   Sets how long the parser is allowed to take in microseconds
]] */
int ltreesitter_parser_set_timeout(lua_State *L) {
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
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
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);

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
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);

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
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	size_t len;
	// lua doesn't guarantee that this string stays alive after it is popped from the stack, so dupe it
	const char *lua_query_src = luaL_checklstring(L, 2, &len);
	const char *query_src = str_ldup(lua_query_src, len);
	if (!query_src) {
		return ALLOC_FAIL(L);
	}
	uint32_t err_offset = 0;
	TSQueryError err_type = TSQueryErrorNone;
	// TODO: this segfaults for some reason, (:
	TSQuery *q = ts_query_new(
	    p->lang,
	    query_src,
	    len,
	    &err_offset,
	    &err_type);
	handle_query_error(L, q, err_offset, err_type, query_src);

	if (q) {
		push_query(L, p->lang, query_src, len, q, 1);
		return 1;
	}

	return 0;
}

/* @teal-export Parser.get_version: function(Parser): integer [[
   get the api version of the parser's language
]] */
static int get_version(lua_State *L) {
	struct ltreesitter_Parser *p = ltreesitter_check_parser(L, 1);
	pushinteger(L, p->lang_version);
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
