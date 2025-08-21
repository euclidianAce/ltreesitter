#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <tree_sitter/api.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "luautils.h"
#include "object.h"
#include "parser.h"
#include "dynamiclib.h"

#include "query.h"
#include "tree.h"

struct ltreesitter_Parser {
	TSParser *parser;
	Dynlib dl;
};

enum ParserLoadErr {
	PARSE_LOAD_ERR_BUFLEN,
	PARSE_LOAD_ERR_DLOPEN,
	PARSE_LOAD_ERR_DLSYM,
	PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD,
	PARSE_LOAD_ERR_LANG_VERSION_TOO_NEW,
	PARSE_LOAD_ERR_NONE,
};

#define TREE_SITTER_SYM "tree_sitter_"
#define TREE_SITTER_SYM_LEN (sizeof TREE_SITTER_SYM - 1)
static enum ParserLoadErr try_dlopen(ltreesitter_Parser *p, char const *parser_file, char const *lang_name, uint32_t *out_version, char const **dynlib_open_error) {
	static char buf[128] = { 't', 'r', 'e', 'e', '_', 's', 'i', 't', 't', 'e', 'r', '_' };
	{
		size_t lang_name_len = strlen(lang_name);
		if (lang_name_len > sizeof buf - TREE_SITTER_SYM_LEN - 1)
			return PARSE_LOAD_ERR_BUFLEN;
		memcpy(buf + 12, lang_name, lang_name_len);
		buf[12 + lang_name_len] = 0;
	}

	if (!dynlib_open(parser_file, &p->dl, dynlib_open_error)) {
		return PARSE_LOAD_ERR_DLOPEN;
	}

	// ISO C is not a fan of void * -> function pointer
	TSLanguage *(*tree_sitter_lang)(void);
	*(void **)(&tree_sitter_lang) = dynlib_sym(&p->dl, buf);

	if (!tree_sitter_lang) {
		dynlib_close(&p->dl);
		return PARSE_LOAD_ERR_DLSYM;
	}
	TSParser *parser = ts_parser_new();
	TSLanguage const *lang = tree_sitter_lang();
	uint32_t const version = ts_language_version(lang);
	*out_version = version;
	p->parser = parser;

	if (version < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) {
		dynlib_close(&p->dl);
		return PARSE_LOAD_ERR_LANG_VERSION_TOO_OLD;
	} else if (version > TREE_SITTER_LANGUAGE_VERSION) {
		dynlib_close(&p->dl);
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

static inline void push_parser_cache_key(lua_State *L, char const *dl_file, char const *lang_name) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addstring(&b, dl_file);
	luaL_addchar(&b, 1);
	luaL_addstring(&b, lang_name);
	luaL_pushresult(&b);
}

static void cache_parser(lua_State *L, char const *dl_file, char const *lang_name) {
	int const parser_idx = lua_gettop(L);
	push_parser_cache(L);                         // cache
	push_parser_cache_key(L, dl_file, lang_name); // parser, cache, "thing.so\1blah"
	lua_pushvalue(L, parser_idx);                 // cache, "thing.so\1blah", parser
	lua_rawset(L, -3);                            // cache
	lua_pop(L, 1);
}

static bool push_cached_parser(lua_State *L, char const *dl_file, char const *lang_name) {
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
int parser_load(lua_State *L) {
	lua_settop(L, 2);
	char const *parser_file = luaL_checkstring(L, 1);
	char const *lang_name = luaL_checkstring(L, 2);

	if (push_cached_parser(L, parser_file, lang_name))
		return 1;

	ltreesitter_Parser proxy = {
		.dl = NULL,
		.parser = NULL,
	};
	uint32_t version = 0;
	char const *dynlib_error;
	switch (try_dlopen(&proxy, parser_file, lang_name, &version, &dynlib_error)) {
	case PARSE_LOAD_ERR_NONE:
		break;
	case PARSE_LOAD_ERR_DLSYM:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to find symbol " TREE_SITTER_SYM "%s", lang_name);
		return 2;
	case PARSE_LOAD_ERR_DLOPEN:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to open dynamic library '%s': %s", parser_file, dynlib_error);
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
	*p = proxy;

	return 1;
}

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

static bool try_load_from_path(
	lua_State *L,
	char const *dl_file,
	char const *lang_name,
	StringBuilder *err_buf
) {
	if (push_cached_parser(L, dl_file, lang_name))
		return true;

	ltreesitter_Parser proxy = {
		.dl = NULL,
		.parser = NULL,
	};
	uint32_t version = 0;
	char const *dynlib_error;
	switch (try_dlopen(&proxy, dl_file, lang_name, &version, &dynlib_error)) {
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
		sb_push_fmt(err_buf, "\n\tTried %s: %s", dl_file, dynlib_error);
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

static size_t find_char(char const *str, size_t len, char c) {
	char const *ptr = memchr(str, c, len);
	if (!ptr)
		return len;
	return (size_t)(ptr - str);
}

static void substitute_question_marks(
	StringBuilder *buf,
	char const *path_pattern,
	size_t path_pattern_len,
	char const *to_replace_with
) {
	size_t i = 0;
	while (i < path_pattern_len) {
		size_t const prev_i = i;
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
	char const *path_list,
	size_t path_list_len,
	char const *dl_name,
	char const *lang_name,
	StringBuilder *path_buf,
	StringBuilder *err_buf
) {
	size_t start = 0;
	size_t end = 0;
	bool result = false;

	StringBuilder name_buf = {0};
	sb_ensure_cap(&name_buf, strlen(dl_name) + sizeof("parsers" PATH_SEP));
	sb_push_str(&name_buf, "parser" PATH_SEP);
	sb_push_str(&name_buf, dl_name);
	sb_push_char(&name_buf, 0);

	while (end < path_list_len) {
		path_buf->length = 0;
		start = end;
		end += find_char(path_list + start, path_list_len - start, ';');
		size_t const len = end - start;
		substitute_question_marks(path_buf, path_list + start, len, dl_name);

		if (try_load_from_path(L, path_buf->data, lang_name, err_buf)) {
			result = true;
			break;
		}

		path_buf->length = 0;
		name_buf.length = 0;
		substitute_question_marks(path_buf, path_list + start, len, name_buf.data);
		if (try_load_from_path(L, path_buf->data, lang_name, err_buf)) {
			result = true;
			break;
		}

		end += 1;
	}

	sb_free(&name_buf);
	return result;
}

/* @teal-export require: function(library_file_name: string, language_name?: string): Parser, string [[
   Search <code>package.cpath</code> for a parser with the filename <code>library_file_name.so</code> or <code>parsers/library_file_name.so</code> (or <code>.dll</code> on Windows) and try to load the symbol <code>tree_sitter_'language_name'</code>
   <code>language_name</code> is optional and will be set to <code>library_file_name</code> if not provided.

   So if you want to load a Lua parser from a file named <code>lua.so</code> then use <code>ltreesitter.require("lua")</code>
   But if you want to load a Lua parser from a file named <code>parser.so</code> then use <code>ltreesitter.require("parser", "lua")</code>

   Like the regular <code>require</code>, this will error if the parser is not found or the symbol couldn't be loaded. Use either <code>pcall</code> or <code>ltreesitter.load</code> to not error out on failure.

   Returns the parser and the path it was loaded from.

   <pre>
   local my_parser, loaded_from = ltreesitter.require("my_language")
   print(loaded_from) -- /home/user/.luarocks/lib/lua/5.4/parsers/my_language.so
   my_parser:parse_string(...)
   -- etc.
   </pre>
]] */

int parser_require(lua_State *L) {
	// grab args
	lua_settop(L, 2);
	char const *so_name = luaL_checkstring(L, 1);
	char const *lang_name = luaL_optstring(L, 2, so_name);

	lua_getglobal(L, "package");  // lang_name, <ts path>, package
	if (lua_isnil(L, -1)) return luaL_error(L, "Unable to load parser for %s, `package` was nil", lang_name);
	lua_getfield(L, -1, "cpath"); // lang_name, <ts path>, package, package.cpath
	if (lua_isnil(L, -1)) return luaL_error(L, "Unable to load parser for %s, `package.cpath` was nil", lang_name);
	lua_remove(L, -2);            // lang_name, <ts path>, package.cpath
	size_t cpath_len;
	char const *cpath = lua_tolstring(L, -1, &cpath_len);
	if (!cpath) return luaL_error(L, "Unable to load parser for %s, `package.cpath` was not a string", lang_name);

	StringBuilder path = {0};
	// buffer to build up search paths in error message
	StringBuilder err_buf = {0};
	sb_push_str(&err_buf, "Unable to load parser for ");
	sb_push_str(&err_buf, lang_name);

	if (!try_load_from_path_list(L, cpath, cpath_len, so_name, lang_name, &path, &err_buf)) {
		sb_push_to_lua(L, &err_buf);
		sb_free(&path);
		sb_free(&err_buf);
		return lua_error(L);
	}
	sb_push_to_lua(L, &path);
	sb_free(&path);
	sb_free(&err_buf);

	return 2;
}

static int parser_gc(lua_State *L) {
	ltreesitter_Parser *lp = luaL_checkudata(L, 1, LTREESITTER_PARSER_METATABLE_NAME);
#ifdef LOG_GC
	printf("Parser p: %p is being garbage collected\n", (void const *)lp);
	printf("   p->dl: %p\n", lp->dl);
#endif
	ts_parser_delete(lp->parser);
	dynlib_close(lp->dl);

	return 0;
}

// TODO: #CustomEncoding via "custom"
// not possible until https://github.com/tree-sitter/tree-sitter/issues/4721
// is resolved

/* @teal-inline [[
   enum Encoding
      "utf-8"
      "utf-16le"
      "utf-16be"
   end

   enum SymbolType
      "regular"
      "anonymous"
      "supertype"
      "auxiliary"
   end
]]*/

static TSInputEncoding encoding_from_str(lua_State *L, int str_index) {
	size_t len = 0;
	char const *encoding_str = lua_tolstring(L, str_index, &len);

	if (!encoding_str) {
		int type = lua_type(L, str_index);
		if (type == LUA_TNIL) return TSInputEncodingUTF8;
		luaL_error(L, "Expected one of `utf-8`, `utf-16le`, `utf-16be`, got %s", lua_typename(L, type));
		return TSInputEncodingUTF8;
	}

	switch (len) {
	case 5:
		if (memcmp(encoding_str, "utf-8", 5) == 0) return TSInputEncodingUTF8;
		break;

	// #CustomEncoding
	//case 6:
	//	if (memcmp(encoding_str, "custom", 6) == 0) return TSInputEncodingCustom;
	//	break;

	case 8:
		if (memcmp(encoding_str, "utf-16le", 8) == 0) return TSInputEncodingUTF16LE;
		if (memcmp(encoding_str, "utf-16be", 8) == 0) return TSInputEncodingUTF16BE;
		break;

	default:
		break;
	}

	luaL_error(L, "Expected one of `utf-8`, `utf-16le`, or `utf-16be`, got %s", encoding_str);
	return TSInputEncodingUTF8;
}

/* @teal-export Parser.parse_string: function(Parser, string, ?Encoding, ?Tree): Tree [[
   Uses the given parser to parse the string

   If <code>Tree</code> is provided then it will be used to create a new updated tree
   (but it is the responsibility of the programmer to make the correct <code>Tree:edit</code> calls)
]] */
static int parser_parse_string(lua_State *L) {
	lua_settop(L, 4);
	ltreesitter_Parser *p = parser_assert(L, 1);
	size_t len;
	char const *to_parse = luaL_checklstring(L, 2, &len);

	TSInputEncoding encoding = encoding_from_str(L, 3);

	TSTree *const old_tree = lua_type(L, 4) == LUA_TNIL
		? NULL
		: tree_assert(L, 4)->tree;

	// #CustomEncoding
	//if (encoding == TSInputEncodingCustom)
	//	return luaL_error(L, "Custom encodings are only usable with `parse_with`");

	TSTree *const tree = ts_parser_parse_string_encoding(p->parser, old_tree, to_parse, len, encoding);
	if (!tree) {
		lua_pushnil(L);
		return 1;
	}

	tree_push(L, tree, len, to_parse);
	return 1;
}

#define read_callback_idx 2
#define progress_callback_idx 3
//#define decode_callback_idx 4

typedef struct {
	lua_State *L;
	bool callback_errored;
} ProgressInfo;
static bool progress_callback(TSParseState *state) {
	// assumed state, lua callback is on top of the stack
	ProgressInfo *const info = state->payload;
	lua_State *const L = info->L;
	lua_settop(L, progress_callback_idx);

	lua_pushvalue(L, -1);
	lua_pushboolean(L, state->has_error);
	lua_pushinteger(L, state->current_byte_offset);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		info->callback_errored = true;
		return true;
	}

	return lua_toboolean(L, -1);
}

enum ReadError {
	READERR_NONE,
	READERR_PCALL,
	READERR_TYPE,
};
struct CallInfo {
	lua_State *L;
	enum ReadError read_error;
};
static char const *read_callback(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read) {
	struct CallInfo *const i = payload;
	lua_State *const L = i->L;
	lua_settop(L, 3);
	lua_pushvalue(L, read_callback_idx); // grab a copy of the function
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
	char const *read_str = lua_tolstring(L, -1, &n);
	*bytes_read = n;
	return read_str;
}

// TODO: allow taking a DecodeFunction

/* @teal-export Parser.parse_with: function(
   Parser,
   reader: (function(integer, Point): string),
   progress_callback?: (function(has_error: boolean, byte_offset: integer): boolean),
   encoding?: Encoding,
   old_tree?: Tree
   ): Tree [[

   <code>reader</code> should be a function that takes a byte index
   and a <code>Point</code> and returns the text at that point. The
   function should return either <code>nil</code> or an empty string
   to signal that there is no more text.

   <code>progress_callback</code> should be a function that takes a boolean
   signalling if an error has occurred, and an integer byte offset. This
   function will be called intermittently while parsing and may return `true`
   to cancel parsing.

   A <code>Tree</code> can be provided to reuse parts of it for parsing,
   provided the <code>Tree:edit</code> has been called previously

   <code>encoding</code> defaults to <code>"utf-8"</code> when not provided.

   May return nil if the progress callback cancelled parsing
]] */
static int parser_parse_with(lua_State *L) {
	lua_settop(L, 5);
	ltreesitter_Parser *const p = parser_assert(L, 1);
	TSTree *old_tree = NULL;
	TSInputEncoding encoding = encoding_from_str(L, 4);
	if (!lua_isnil(L, 5)) {
		old_tree = tree_assert(L, 5)->tree;
	}
	lua_pop(L, 2);
	struct CallInfo read_payload = {
		.L = L,
		.read_error = READERR_NONE,
	};

	// #CustomEncoding
	//if (encoding == TSInputEncodingCustom)
	//	return luaL_error(L, "Custom encodings are not yet supported");

	TSInput input = {
		.read = read_callback,
		.payload = &read_payload,
		.encoding = encoding,
		.decode = NULL,
	};

	ProgressInfo progress_payload = {
		.L = L,
		.callback_errored = false,
	};

	TSParseOptions options = {
		.payload = &progress_payload,
		.progress_callback = progress_callback,
	};

	TSTree *t = lua_isnil(L, progress_callback_idx)
		? ts_parser_parse(p->parser, old_tree, input)
		: ts_parser_parse_with_options(p->parser, old_tree, input, options);

	switch (read_payload.read_error) {
	case READERR_PCALL:
		return luaL_error(L, "Read function errored: %s", lua_tostring(L, -1));
	case READERR_TYPE:
		return luaL_error(L, "Read function returned %s (expected string)", lua_typename(L, lua_type(L, -1)));
	case READERR_NONE:
	default:
		break;
	}

	if (progress_payload.callback_errored) {
		return luaL_error(L, "Progress function errored: %s", lua_tostring(L, -1));
	}

	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	tree_push_with_reader(L, t, 2);

	return 1;
}

/* @teal-export Parser.reset: function(Parser) [[
   Reset the parser, causing the next parse to start from the beginning
]] */
static int parser_reset(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	ts_parser_reset(p->parser);
	return 0;
}

/* @teal-inline [[
   interface Range
      start_byte: integer
      end_byte: integer

      start_point: Point
      end_point: Point
   end
]]*/

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
	ltreesitter_Parser *p = parser_assert(L, 1);

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
			int end_byte = (int)ranges[i - 1].end_byte;
			int start_byte = (int)ranges[i].start_byte;
			free(ranges);
			return luaL_error(L, "Error in ranges: range[%d].end_byte (%d) is greater than range[%d].start_byte (%d)", i, end_byte, i + 1, start_byte);
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
	ltreesitter_Parser *p = parser_assert(L, 1);

	uint32_t length = 0;
	TSRange const *ranges = ts_parser_included_ranges(p->parser, &length);
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
static int make_query(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Parser *p = parser_assert(L, 1);
	size_t len;
	char const *lua_query_src = luaL_checklstring(L, 2, &len);
	uint32_t err_offset = 0;
	TSQueryError err_type = TSQueryErrorNone;
	TSLanguage const *lang = ts_parser_language(p->parser);
	TSQuery *q = ts_query_new(
		lang,
		lua_query_src,
		len,
		&err_offset,
		&err_type);
	query_handle_error(L, q, err_offset, err_type, lua_query_src, len);

	if (q)
		query_push(L, lang, lua_query_src, len, q, 1);
	else
		lua_pushnil(L);

	return 1;
}

/* @teal-export Parser.get_version: function(Parser): integer [[
   get the api version of the parser's language
]] */
static int get_version(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	pushinteger(L, ts_language_version(ts_parser_language(p->parser)));
	return 1;
}

// Language methods:
//
// I've made the conscious decision to not expost `TSLanguage` objects
// directly, so methods are exposed through Parsers

/* @teal-export Parser.language_name: function(Parser): string [[
   Get the name of the language this parser is for. May return nil.
]] */
static int parser_language_name(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	char const *name = ts_language_name(ts_parser_language(p->parser));
	if (name) lua_pushstring(L, name);
	else lua_pushnil(L);
	return 1;
}

/* @teal-export Parser.language_symbol_count: function(Parser): integer [[
   Get the number of distinct node types in the given parser's language
]] */
static int parser_language_symbol_count(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	lua_pushinteger(L, ts_language_symbol_count(ts_parser_language(p->parser)));
	return 1;
}

/* @teal-export Parser.language_state_count: function(Parser): integer [[
   Get the number of valid states in the given parser's language
]] */
static int parser_language_state_count(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	lua_pushinteger(L, ts_language_state_count(ts_parser_language(p->parser)));
	return 1;
}

/* @teal-export Parser.language_field_count: function(Parser): integer [[
	Get the number of distinct field names in the given parser's language
]] */
static int parser_language_field_count(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	lua_pushinteger(L, ts_language_field_count(ts_parser_language(p->parser)));
	return 1;
}

/* @teal-export Parser.language_abi_version: function(Parser): integer [[
   Get the ABI version number for the given parser's language
]] */
static int parser_language_abi_version(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	lua_pushinteger(L, ts_language_abi_version(ts_parser_language(p->parser)));
	return 1;
}

/* @teal-inline [[
   interface LanguageMetadata
      major_version: integer
      minor_version: integer
      patch_version: integer
   end
]] */
/* @teal-export Parser.language_metadata: function(Parser): LanguageMetadata [[
   Get the metadata for the given parser's language. This information relies on
   the language author providing the correct data in the language's
   `tree-sitter.json`

   May return nil
]] */
static int parser_language_metadata(lua_State *L) {
	ltreesitter_Parser *p = parser_assert(L, 1);
	TSLanguageMetadata const *meta = ts_language_metadata(ts_parser_language(p->parser));
	if (meta) {
		lua_createtable(L, 0, 3);
		pushinteger(L, meta->major_version); lua_setfield(L, -2, "major_version");
		pushinteger(L, meta->minor_version); lua_setfield(L, -2, "minor_version");
		pushinteger(L, meta->patch_version); lua_setfield(L, -2, "patch_version");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/* @teal-inline [[
   type StateId = integer
   type Symbol = integer
   type FieldId = integer
]] */

/* @teal-export Parser.language_field_id_for_name: function(Parser, string): FieldId [[
   Get the numeric id for the given field name
]] */
static int parser_language_field_id_for_name(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	size_t len;
	const char *name = lua_tolstring(L, 2, &len);
	pushinteger(L, ts_language_field_id_for_name(l, name, len));
	return 1;
}

/* @teal-export Parser.language_name_for_field_id: function(Parser, FieldId): string [[
   Get the name for a numeric field id
]] */
static int parser_language_name_for_field_id(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a FieldId)");
	char const *name = ts_language_field_name_for_id(l, (TSFieldId)id);
	if (name) lua_pushstring(L, name);
	else lua_pushnil(L);
	return 1;
}

/* @teal-export Parser.language_symbol_for_name: function(Parser, string, is_named: boolean): Symbol [[
   Get the numerical id for the given node type string
]] */
static int parser_language_symbol_for_name(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	size_t len;
	char const *name = lua_tolstring(L, 2, &len);
	bool is_named = lua_toboolean(L, 3);
	TSSymbol sym = ts_language_symbol_for_name(l, name, (uint32_t)len, is_named);
	if (sym) lua_pushinteger(L, sym);
	else lua_pushnil(L);
	return 1;
}

/* @teal-export Parser.language_symbol_name: function(Parser, Symbol): string [[
   Get a node type string for the given symbol id
]] */
static int parser_language_symbol_name(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a Symbol)");
	char const *name = ts_language_symbol_name(l, (TSSymbol)id);
	if (name) lua_pushstring(L, name);
	else lua_pushnil(L);
	return 1;
}

/* @teal-export Parser.language_symbol_type: function(Parser, Symbol): SymbolType [[
   Check whether the given node type id belongs to named nodes, anonymous nodes,
   or hidden nodes
]] */
static int parser_language_symbol_type(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a Symbol)");

	switch (ts_language_symbol_type(l, (TSSymbol)id)) {
	case TSSymbolTypeRegular:   lua_pushliteral(L, "regular");   break;
	case TSSymbolTypeAnonymous: lua_pushliteral(L, "anonymous"); break;
	case TSSymbolTypeSupertype: lua_pushliteral(L, "supertype"); break;
	case TSSymbolTypeAuxiliary: lua_pushliteral(L, "auxiliary"); break;
	default:                    lua_pushnil(L); break;
	}
	return 1;
}

/* @teal-export Parser.language_supertypes: function(Parser): {Symbol} [[
   Get a list of all supertype symbols for the given parser's language
]] */
static int parser_language_supertypes(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	uint32_t length;
	TSSymbol const *syms = ts_language_supertypes(l, &length);
	lua_createtable(L, length, 0);
	for (uint32_t i = 0; i < length; ++i) {
		pushinteger(L, syms[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* @teal-export Parser.language_subtypes: function(Parser, supertype: Symbol): {Symbol} [[
   Get a list of all supertype symbols for the given parser's language
]] */
static int parser_language_subtypes(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a Symbol)");

	uint32_t length;
	TSSymbol const *syms = ts_language_subtypes(l, (TSSymbol)id, &length);

	lua_createtable(L, length, 0);
	for (uint32_t i = 0; i < length; ++i) {
		pushinteger(L, syms[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* @teal-export Parser.language_next_state: function(Parser, StateId, Symbol): StateId [[
   Get the next parse state
]] */
static int parser_language_next_state(lua_State *L) {
	TSLanguage const *l = ts_parser_language(parser_assert(L, 1)->parser);

	lua_Integer state_id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, state_id >= 0, 2, "expected a non-negative integer (a StateId)");

	lua_Integer sym_id = luaL_checkinteger(L, 3);
	luaL_argcheck(L, sym_id >= 0, 2, "expected a non-negative integer (a Symbol)");

	TSStateId result = ts_language_next_state(l, (TSStateId)state_id, (TSSymbol)sym_id);
	pushinteger(L, result);

	return 1;
}

static const luaL_Reg parser_methods[] = {
	{"reset", parser_reset},
	{"set_ranges", parser_set_ranges},
	{"get_ranges", parser_get_ranges},
	{"get_version", get_version},

	{"parse_string", parser_parse_string},
	{"parse_with", parser_parse_with},
	{"query", make_query},

	{"language_name", parser_language_name},
	{"language_symbol_count", parser_language_symbol_count},
	{"language_state_count", parser_language_state_count},
	{"language_field_count", parser_language_field_count},
	{"language_abi_version", parser_language_abi_version},
	{"language_metadata", parser_language_metadata},
	{"language_field_id_for_name", parser_language_field_id_for_name},
	{"language_name_for_field_id", parser_language_name_for_field_id},
	{"language_symbol_for_name", parser_language_symbol_for_name},
	{"language_symbol_name", parser_language_symbol_name},
	{"language_symbol_type", parser_language_symbol_type},
	{"language_supertypes", parser_language_supertypes},
	{"language_subtypes", parser_language_subtypes},
	{"language_next_state", parser_language_next_state},

	{NULL, NULL}};
static const luaL_Reg parser_metamethods[] = {
	{"__gc", parser_gc},
	{NULL, NULL}};

void parser_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_PARSER_METATABLE_NAME, parser_metamethods, parser_methods);
}
