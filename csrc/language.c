#include "language.h"
#include "dynamiclib.h"
#include "object.h"
#include "query.h"

#include <assert.h>
#include <inttypes.h>

#define TREE_SITTER_SYM "tree_sitter_"
#define TREE_SITTER_SYM_LEN (sizeof TREE_SITTER_SYM - 1)
#define MAX_LANG_NAME_LEN 200

/* @teal-inline [[
   type StateId = integer
   type Symbol = integer
   type FieldId = integer
]] */

#define dynlib_registry_field "dynlibs"

void setup_dynlib_cache(lua_State *L) {
	newtable_with_mode(L, "v");
	set_registry_field(L, dynlib_registry_field);
}

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

static int dynlib_gc(lua_State *L) {
	Dynlib *lib = luaL_checkudata(L, 1, LTREESITTER_DYNLIB_METATABLE_NAME);
#ifdef LOG_GC
	printf("Dynlib %p is being garbage collected\n", (void const *)lib);
#endif
	dynlib_close(lib);
	return 0;
}

void dynlib_init_metatable(lua_State *L) {
	static const luaL_Reg metamethods[] = {
		{"__gc", dynlib_gc},
		{NULL, NULL}};
	create_metatable(L, LTREESITTER_DYNLIB_METATABLE_NAME, metamethods, (luaL_Reg[]){{NULL, NULL}});
}

// ( -- Dynlib )
static void cache_dynlib(lua_State *L, char const *path_loaded_from, Dynlib dl) {
	// TODO: should we even attempt to normalize the path?
	push_registry_field(L, dynlib_registry_field);      // cache
	*(Dynlib *)lua_newuserdata(L, sizeof(Dynlib)) = dl; // cache, dynlib
	setmetatable(L, LTREESITTER_DYNLIB_METATABLE_NAME);
	lua_pushvalue(L, -1);                  // cache, dynlib, dynlib
	lua_setfield(L, -3, path_loaded_from); // cache, dynlib
	lua_remove(L, -2);                     // dynlib
}

// ( -- Dynlib )
static Dynlib *get_cached_dynlib(lua_State *L, char const *path) {
	push_registry_field(L, dynlib_registry_field); // cache
	lua_getfield(L, -1, path);                     // cache, ?Dynlib
	lua_remove(L, -2);                             // ?Dynlib
	void *data = testudata(L, -1, LTREESITTER_DYNLIB_METATABLE_NAME);
	return data;
}

/* @teal-export load: function(file_name: string, language_name: string): Language, string [[
   Load a language from a given file

   Keep in mind that this includes the <code>.so</code>, <code>.dll</code>, or <code>.dynlib</code> extension

   On Unix this uses dlopen, on Windows this uses LoadLibrary so if a path without a path separator is given, these functions have their own path's that they will search for your file in.
   So if in doubt use a file path like
   <pre>
   local my_language = ltreesitter.load("./my_parser.so", "my_language")
   </pre>
]] */
TSLanguage const *language_load_from(Dynlib dl, size_t lang_name_len, char const *language_name) {
	char buf[TREE_SITTER_SYM_LEN + MAX_LANG_NAME_LEN + 1] = {'t', 'r', 'e', 'e', '_', 's', 'i', 't', 't', 'e', 'r', '_'};
	{
		assert(lang_name_len <= MAX_LANG_NAME_LEN);
		memcpy(buf + TREE_SITTER_SYM_LEN, language_name, lang_name_len);
		buf[TREE_SITTER_SYM_LEN + lang_name_len] = 0;
	}
	void *sym = dynlib_sym(&dl, buf);
	if (!sym)
		return NULL;
	TSLanguage *(*tree_sitter_lang)(void);
	*(void **)(&tree_sitter_lang) = sym;
	return tree_sitter_lang();
}

int language_load(lua_State *L) {
	char const *dl_file = luaL_checkstring(L, 1);
	size_t lang_name_len = 0;
	char const *lang_name = luaL_checklstring(L, 2, &lang_name_len);
	if (lang_name_len > MAX_LANG_NAME_LEN) {
		lua_pushnil(L);
		lua_pushfstring(L, "Language name is too long (%zu bytes, max of %d is allowed)", lang_name_len, MAX_LANG_NAME_LEN);
		return 2;
	}

	Dynlib opened;
	bool cached = false;
	{
		Dynlib *dl = get_cached_dynlib(L, dl_file);
		if (dl) {
			opened = *dl;
			cached = true;
		} else {
			char const *err = NULL;
			if (!dynlib_open(dl_file, &opened, &err)) {
				lua_pushnil(L);
				lua_pushstring(L, err);
				return 2;
			}
		}
	} // dynlib | nothing

	TSLanguage const *lang = language_load_from(opened, lang_name_len, lang_name);
	if (!lang) {
		lua_pushnil(L);
		lua_pushfstring(L, "Symbol not found in %s", dl_file);
		if (!cached)
			dynlib_close(&opened);
		return 1;
	}

	TSLanguage const **result = lua_newuserdata(L, sizeof(TSLanguage *));
	*result = lang;
	setmetatable(L, LTREESITTER_LANGUAGE_METATABLE_NAME);
	// dynlib | nothing, lang

	if (!cached) {
		cache_dynlib(L, dl_file, opened);
		lua_insert(L, -2);
	} // dynlib, lang

	bind_lifetimes(L, -1, -2); // language keeps dll alive

	return 1;
}

static bool try_load_from_path(
	lua_State *L,
	char const *dl_file,
	size_t lang_name_len,
	char const *lang_name,
	StringBuilder *err_buf) {
	char const *dynlib_error = NULL;
	TSLanguage const *lang = NULL;
	bool should_cache_dl = false;

	{
		Dynlib *dl = get_cached_dynlib(L, dl_file);
		if (dl) {
			lang = language_load_from(*dl, lang_name_len, lang_name);
			if (!lang) {
				sb_push_fmt(err_buf, "\n\tFound %s, but unable to find symbol " TREE_SITTER_SYM "%s", dl_file, lang_name);
				return false;
			}
		}
	} // dynlib | nothing

	Dynlib dl;
	if (!lang) {
		should_cache_dl = true;
		if (!dynlib_open(dl_file, &dl, &dynlib_error)) {
			sb_push_fmt(err_buf, "\n\tTried %s: %s", dl_file, dynlib_error);
			return false;
		}
		lang = language_load_from(dl, lang_name_len, lang_name);
		if (!lang) {
			dynlib_close(&dl);
			sb_push_fmt(err_buf, "\n\tFound %s, but unable to find symbol " TREE_SITTER_SYM "%s", dl_file, lang_name);
			return false;
		}
	}

	uint32_t const version = ts_language_version(lang);
	if (version < TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION) {
		if (should_cache_dl)
			dynlib_close(&dl);
		sb_push_fmt(
			err_buf,
			"\n\tFound %s, but the version is too old, language version: %" PRIu32 ", minimum version: %d",
			dl_file,
			version,
			TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
		return false;
	} else if (version > TREE_SITTER_LANGUAGE_VERSION) {
		if (should_cache_dl)
			dynlib_close(&dl);
		sb_push_fmt(
			err_buf,
			"\n\tFound %s, but the version is too new, language version: %" PRIu32 ", maximum version: %d",
			dl_file,
			version,
			TREE_SITTER_LANGUAGE_VERSION);
		return false;
	}

	// assert(lang);

	TSLanguage const **result = lua_newuserdata(L, sizeof(TSLanguage const *));
	*result = lang;
	setmetatable(L, LTREESITTER_LANGUAGE_METATABLE_NAME);

	// dynlib | nothing, lang

	if (should_cache_dl) { // (nothing), lang
		cache_dynlib(L, dl_file, dl);
		lua_insert(L, -2);
	} // dynlib, lang

	bind_lifetimes(L, -1, -2); // language keeps dll alive
	lua_remove(L, -2);

	return true;
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
	char const *to_replace_with) {
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
	size_t path_list_len,
	char const *path_list,
	char const *dl_name,
	size_t lang_name_len,
	char const *lang_name,
	StringBuilder *path_buf,
	StringBuilder *err_buf) {
	size_t start = 0;
	size_t end = 0;
	bool result = false;

	StringBuilder name_buf = {0};
	sb_ensure_cap(&name_buf, strlen(dl_name) + sizeof("parser" PATH_SEP));
	sb_push_str(&name_buf, "parser" PATH_SEP);
	sb_push_str(&name_buf, dl_name);
	sb_push_char(&name_buf, 0);

	while (end < path_list_len) {
		path_buf->length = 0;
		start = end;
		end += find_char(path_list + start, path_list_len - start, ';');
		size_t const len = end - start;
		// first try just `dl_name`
		substitute_question_marks(path_buf, path_list + start, len, dl_name);

		if (try_load_from_path(L, path_buf->data, lang_name_len, lang_name, err_buf)) {
			result = true;
			break;
		}

		path_buf->length = 0;
		name_buf.length = 0;
		// then try parser/`dl_name`
		substitute_question_marks(path_buf, path_list + start, len, name_buf.data);
		if (try_load_from_path(L, path_buf->data, lang_name_len, lang_name, err_buf)) {
			result = true;
			break;
		}

		end += 1;
	}

	sb_free(&name_buf);
	return result;
}

/* @teal-export require: function(library_file_name: string, language_name?: string): Language, string [[
   Search <code>package.cpath</code> for a parser with the filename <code>library_file_name.so</code> or <code>parsers/library_file_name.so</code> (or <code>.dll</code> on Windows) and try to load the symbol <code>tree_sitter_'language_name'</code>
   <code>language_name</code> is optional and will be set to <code>library_file_name</code> if not provided.

   So if you want to load a Lua parser from a file named <code>lua.so</code> then use <code>ltreesitter.require("lua")</code>
   But if you want to load a Lua parser from a file named <code>parser.so</code> then use <code>ltreesitter.require("parser", "lua")</code>

   Like the regular <code>require</code>, this will error if the parser is not found or the symbol couldn't be loaded. Use either <code>pcall</code> or <code>ltreesitter.load</code> to not error out on failure.

   Returns the language and the path it was loaded from.

   <pre>
   local my_language, loaded_from = ltreesitter.require("my_language")
   print(my_language:name(), loaded_from) -- "my_language", /home/user/.luarocks/lib/lua/5.4/parsers/my_language.so
   -- etc.
   </pre>
]] */
int language_require(lua_State *L) {
	lua_settop(L, 2);
	char const *so_name = luaL_checkstring(L, 1);
	size_t lang_name_len = 0;
	char const *lang_name = luaL_optlstring(L, 2, so_name, &lang_name_len);
	if (lang_name_len > MAX_LANG_NAME_LEN) {
		char buf[128];
		snprintf(buf, sizeof buf, "Language name is too long (%zu bytes, max of %d is allowed)", lang_name_len, MAX_LANG_NAME_LEN);
		luaL_argcheck(L, false, 2, buf);
		return 0;
	}

	lua_getglobal(L, "package"); // lang_name, <ts path>, package
	if (lua_isnil(L, -1))
		return luaL_error(L, "Unable to load language %s, `package` was nil", lang_name);
	lua_getfield(L, -1, "cpath"); // lang_name, <ts path>, package, package.cpath
	if (lua_isnil(L, -1))
		return luaL_error(L, "Unable to load language %s, `package.cpath` was nil", lang_name);
	lua_remove(L, -2); // lang_name, <ts path>, package.cpath
	size_t cpath_len;
	char const *cpath = lua_tolstring(L, -1, &cpath_len);
	if (!cpath)
		return luaL_error(L, "Unable to load language %s, `package.cpath` was not a string", lang_name);

	StringBuilder path = {0};
	// buffer to build up search paths in error message
	StringBuilder err_buf = {0};
	sb_push_lit(&err_buf, "Unable to load language ");
	sb_push_str(&err_buf, lang_name);

	if (!try_load_from_path_list(L, cpath_len, cpath, so_name, lang_name_len, lang_name, &path, &err_buf)) {
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

/* @teal-export Language.parser: function(Language): Parser [[
   Create a parser of the given language
]] */
static int make_parser(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	TSParser *parser = ts_parser_new();
	if (!ts_parser_set_language(parser, l))
		return luaL_error(L, "Internal error: an incompatible language was loaded");

	TSParser **result = lua_newuserdata(L, sizeof(TSParser *));
	bind_lifetimes(L, -1, -2); // parser keeps language alive
	setmetatable(L, LTREESITTER_PARSER_METATABLE_NAME);
	*result = parser;

	return 1;
}

static int language_gc(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	ts_language_delete(l);
	return 0;
}

/* @teal-export Language.name: function(Language): string [[
   Get the name of the language. May return nil.
]] */
static int language_name(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	char const *name = ts_language_name(l);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

/* @teal-export Language.symbol_count: function(Language): integer [[
   Get the number of distinct node types in the given language
]] */
static int language_symbol_count(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_pushinteger(L, ts_language_symbol_count(l));
	return 1;
}

/* @teal-export Language.state_count: function(Language): integer [[
   Get the number of valid states in the given language
]] */
static int language_state_count(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_pushinteger(L, ts_language_state_count(l));
	return 1;
}

/* @teal-export Language.field_count: function(Language): integer [[
   Get the number of distinct field names in the given parser's language
]] */
static int language_field_count(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_pushinteger(L, ts_language_field_count(l));
	return 1;
}

/* @teal-export Language.abi_version: function(Language): integer [[
   Get the ABI version number for the given parser's language
]] */
static int language_abi_version(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_pushinteger(L, ts_language_abi_version(l));
	return 1;
}

/* @teal-inline [[
   interface LanguageMetadata
      major_version: integer
      minor_version: integer
      patch_version: integer
   end
]] */
/* @teal-export Language.metadata: function(Language): LanguageMetadata [[
   Get the metadata for the given language. This information relies on
   the language author providing the correct data in the language's
   `tree-sitter.json`

   May return nil
]] */
static int language_metadata(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	TSLanguageMetadata const *meta = ts_language_metadata(l);
	if (meta) {
		lua_createtable(L, 0, 3);
		pushinteger(L, meta->major_version);
		lua_setfield(L, -2, "major_version");
		pushinteger(L, meta->minor_version);
		lua_setfield(L, -2, "minor_version");
		pushinteger(L, meta->patch_version);
		lua_setfield(L, -2, "patch_version");
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/* @teal-export Language.field_id_for_name: function(Language, string): FieldId [[
   Get the numeric id for the given field name
]] */
static int language_field_id_for_name(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	size_t len;
	const char *name = lua_tolstring(L, 2, &len);
	pushinteger(L, ts_language_field_id_for_name(l, name, len));
	return 1;
}

/* @teal-export Language.name_for_field_id: function(Language, FieldId): string [[
   Get the name for a numeric field id
]] */
static int language_name_for_field_id(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a FieldId)");
	char const *name = ts_language_field_name_for_id(l, (TSFieldId)id);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

/* @teal-export Language.symbol_for_name: function(Language, string, is_named: boolean): Symbol [[
   Get the numerical id for the given node type string
]] */
static int language_symbol_for_name(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	size_t len;
	char const *name = lua_tolstring(L, 2, &len);
	bool is_named = lua_toboolean(L, 3);
	TSSymbol sym = ts_language_symbol_for_name(l, name, (uint32_t)len, is_named);
	if (sym)
		lua_pushinteger(L, sym);
	else
		lua_pushnil(L);
	return 1;
}

/* @teal-export Language.symbol_name: function(Language, Symbol): string [[
   Get a node type string for the given symbol id
]] */
static int language_symbol_name(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a Symbol)");
	char const *name = ts_language_symbol_name(l, (TSSymbol)id);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushnil(L);
	return 1;
}

/* @teal-export Language.symbol_type: function(Language, Symbol): SymbolType [[
   Check whether the given node type id belongs to named nodes, anonymous nodes,
   or hidden nodes
]] */
static int language_symbol_type(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	lua_Integer id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, id >= 0, 2, "expected a non-negative integer (a Symbol)");

	switch (ts_language_symbol_type(l, (TSSymbol)id)) {
	// clang-format: off
	case TSSymbolTypeRegular:   lua_pushliteral(L, "regular");   break;
	case TSSymbolTypeAnonymous: lua_pushliteral(L, "anonymous"); break;
	case TSSymbolTypeSupertype: lua_pushliteral(L, "supertype"); break;
	case TSSymbolTypeAuxiliary: lua_pushliteral(L, "auxiliary"); break;
	default:                    lua_pushnil(L); break;
	// clang-format: on
	}
	return 1;
}

/* @teal-export Language.supertypes: function(Language): {Symbol} [[
   Get a list of all supertype symbols for the given language
]] */
static int language_supertypes(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
	uint32_t length;
	TSSymbol const *syms = ts_language_supertypes(l, &length);
	lua_createtable(L, length, 0);
	for (uint32_t i = 0; i < length; ++i) {
		pushinteger(L, syms[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* @teal-export Language.subtypes: function(Language, supertype: Symbol): {Symbol} [[
   Get a list of all supertype symbols for the given language
]] */
static int language_subtypes(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);
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

/* @teal-export Language.next_state: function(Language, StateId, Symbol): StateId [[
   Get the next parse state
]] */
static int language_next_state(lua_State *L) {
	TSLanguage const *l = *language_assert(L, 1);

	lua_Integer state_id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, state_id >= 0, 2, "expected a non-negative integer (a StateId)");

	lua_Integer sym_id = luaL_checkinteger(L, 3);
	luaL_argcheck(L, sym_id >= 0, 2, "expected a non-negative integer (a Symbol)");

	TSStateId result = ts_language_next_state(l, (TSStateId)state_id, (TSSymbol)sym_id);
	pushinteger(L, result);

	return 1;
}

/* @teal-export Language.query: function(Language, string): Query [[
   Create a query out of the given string for this language
]] */
static int make_query(lua_State *L) {
	lua_settop(L, 2);
	TSLanguage const *lang = *language_assert(L, 1);
	size_t len;
	char const *lua_query_src = luaL_checklstring(L, 2, &len);
	uint32_t err_offset = 0;
	TSQueryError err_type = TSQueryErrorNone;
	TSQuery *q = ts_query_new(
		lang,
		lua_query_src,
		len,
		&err_offset,
		&err_type);
	query_handle_error(L, q, err_offset, err_type, lua_query_src, len);

	if (q)
		query_push(L, lua_query_src, len, q, 1);
	else
		lua_pushnil(L);

	return 1;
}

static const luaL_Reg language_methods[] = {
	{"parser", make_parser},
	{"query", make_query},

	{"name", language_name},
	{"symbol_count", language_symbol_count},
	{"state_count", language_state_count},
	{"field_count", language_field_count},
	{"abi_version", language_abi_version},
	{"metadata", language_metadata},

	{"field_id_for_name", language_field_id_for_name},
	{"name_for_field_id", language_name_for_field_id},
	{"symbol_for_name", language_symbol_for_name},
	{"symbol_name", language_symbol_name},
	{"symbol_type", language_symbol_type},
	{"supertypes", language_supertypes},
	{"subtypes", language_subtypes},
	{"next_state", language_next_state},

	{NULL, NULL}};

static const luaL_Reg language_metamethods[] = {
	{"__gc", language_gc},

	{NULL, NULL}};

void language_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_LANGUAGE_METATABLE_NAME, language_metamethods, language_methods);
}
