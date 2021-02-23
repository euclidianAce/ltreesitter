#ifndef LTREESITTER_TYPES_H
#define LTREESITTER_TYPES_H

#include <tree_sitter/api.h>
#include "dynamiclib.h"

struct ltreesitter_Parser {
	const TSLanguage *lang;
	dl_handle *dl;
	TSParser *parser;
};
#define LTREESITTER_PARSER_METATABLE_NAME "ltreesitter.Parser"

struct ltreesitter_Tree {
	const TSLanguage *lang;
	TSTree *tree;

	// The *TSTree structure is opaque so we don't have access to how it
	// internally gets the source of the string that it parsed,
	// so we just keep a copy for ourselves here.
	// Not the most memory efficient, but I'd argue having Node:source()
	// is worth it
	bool own_str;
	const char *src;
	size_t src_len;
};
#define LTREESITTER_TREE_METATABLE_NAME "ltreesitter.Tree"

struct ltreesitter_TreeCursor {
	const TSLanguage *lang;
	TSTreeCursor cursor;
};
#define LTREESITTER_TREE_CURSOR_METATABLE_NAME "ltreesitter.TreeCursor"

struct ltreesitter_Node {
	const TSLanguage *lang;
	TSNode node;
};
#define LTREESITTER_NODE_METATABLE_NAME "ltreesitter.Node"

struct ltreesitter_Query {
	const TSLanguage *lang;
	TSQuery *query;

	const char *src;
	size_t src_len;
};
#define LTREESITTER_QUERY_METATABLE_NAME "ltreesitter.Query"

struct ltreesitter_QueryCursor {
	struct ltreesitter_Query *query;
	TSQueryCursor *query_cursor;
};
#define LTREESITTER_QUERY_CURSOR_METATABLE_NAME "ltreesitter.QueryCursor"

#endif
