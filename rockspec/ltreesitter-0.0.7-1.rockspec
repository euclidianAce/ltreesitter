rockspec_format = "3.0"
package = "ltreesitter"
version = "0.0.7-1"
source = {
   url = "git+https://github.com/euclidianAce/ltreesitter.git",
	tag = "v0.0.7",
}
description = {
   homepage = "https://github.com/euclidianAce/ltreesitter",
   license = "MIT",
   summary = "Treesitter bindings to Lua",
   detailed = [[Standalone Lua bindings to the Treesitter api (with full type definitions for Teal).]],
   issues_url = "https://github.com/euclidianAce/ltreesitter/issues",
}
external_dependencies = {
   TREE_SITTER = {
      header = "tree_sitter/api.h",
   },
   -- UV = {
      -- header = "uv.h",
   -- }
}
build = {
	type = "builtin",
   modules = {
      ltreesitter = {
         sources = {
            "csrc/dynamiclib.c",
            "csrc/luautils.c",
            "csrc/object.c",
            "csrc/query.c",
            "csrc/tree.c",
            "csrc/types.c",
            "csrc/ltreesitter.c",
            "csrc/node.c",
            "csrc/parser.c",
            "csrc/query_cursor.c",
            "csrc/tree_cursor.c",
         },
         incdirs = { "include", "$(TREE_SITTER_INCDIR)" },
         libraries = { "tree-sitter" },
         libdirs = { "$(TREE_SITTER_LIBDIR)" },
      },
   },
   copy_directories = {
      "docs",
      "include",
   },
}
