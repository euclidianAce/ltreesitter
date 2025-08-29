#include <stdio.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <tree_sitter/api.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dynamiclib.h"
#include "luautils.h"
#include "object.h"
#include "parser.h"

#include "query.h"
#include "tree.h"

static int parser_gc(lua_State *L) {
	TSParser *p = *parser_assert(L, 1);
#ifdef LOG_GC
	printf("Parser %p is being garbage collected\n", (void const *)p);
#endif
	ts_parser_delete(p);
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
		if (type == LUA_TNIL)
			return TSInputEncodingUTF8;
		luaL_error(L, "Expected one of `utf-8`, `utf-16le`, `utf-16be`, got %s", lua_typename(L, type));
		return TSInputEncodingUTF8;
	}

	switch (len) {
	case 5:
		if (memcmp(encoding_str, "utf-8", 5) == 0)
			return TSInputEncodingUTF8;
		break;

		// #CustomEncoding
		// case 6:
		//	if (memcmp(encoding_str, "custom", 6) == 0) return TSInputEncodingCustom;
		//	break;

	case 8:
		if (memcmp(encoding_str, "utf-16le", 8) == 0)
			return TSInputEncodingUTF16LE;
		if (memcmp(encoding_str, "utf-16be", 8) == 0)
			return TSInputEncodingUTF16BE;
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
	TSParser *p = *parser_assert(L, 1);
	size_t len;
	char const *to_parse = luaL_checklstring(L, 2, &len);

	TSInputEncoding encoding = encoding_from_str(L, 3);

	TSTree *const old_tree = lua_type(L, 4) == LUA_TNIL
		? NULL
		: tree_assert(L, 4)->tree;

	// #CustomEncoding
	// if (encoding == TSInputEncodingCustom)
	//	return luaL_error(L, "Custom encodings are only usable with `parse_with`");

	TSTree *const tree = ts_parser_parse_string_encoding(p, old_tree, to_parse, len, encoding);
	if (!tree) {
		lua_pushnil(L);
		return 1;
	}

	tree_push(L, tree, len, to_parse);
	return 1;
}

#define read_callback_idx 2
#define progress_callback_idx 3
// #define decode_callback_idx 4

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
	TSParser *const p = *parser_assert(L, 1);
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
	// if (encoding == TSInputEncodingCustom)
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
		? ts_parser_parse(p, old_tree, input)
		: ts_parser_parse_with_options(p, old_tree, input, options);

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
	TSParser *p = *parser_assert(L, 1);
	ts_parser_reset(p);
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
	TSParser *p = *parser_assert(L, 1);

	if (lua_isnil(L, 2)) {
		lua_pushboolean(L, ts_parser_set_included_ranges(p, NULL, 0));
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

	lua_pushboolean(L, ts_parser_set_included_ranges(p, ranges, len));
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
	TSParser *p = *parser_assert(L, 1);

	uint32_t length = 0;
	TSRange const *ranges = ts_parser_included_ranges(p, &length);
	lua_createtable(L, (int)length, 0);
	for (uint32_t i = 0; i < length; ++i) {
		push_range(L, ranges + i);
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

static int parser_language(lua_State *L) {
	(void)parser_assert(L, 1);
	push_kept(L, -1);
	return 1;
}

static const luaL_Reg parser_methods[] = {
	{"reset", parser_reset},
	{"set_ranges", parser_set_ranges},
	{"get_ranges", parser_get_ranges},

	{"parse_string", parser_parse_string},
	{"parse_with", parser_parse_with},

	{"language", parser_language},

	{NULL, NULL}};
static const luaL_Reg parser_metamethods[] = {
	{"__gc", parser_gc},
	{NULL, NULL}};

void parser_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_PARSER_METATABLE_NAME, parser_metamethods, parser_methods);
}
