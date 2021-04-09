rockspec_format = "3.0"
package = "ltreesitter"
version = "dev-1"
source = {
   url = "git+https://github.com/euclidianAce/ltreesitter.git"
}
description = {
   homepage = "https://github.com/euclidianAce/ltreesitter",
   license = "MIT",
   summary = "Treesitter bindings to Lua",
   detailed = [[Standalone Lua bindings to the Treesitter api.]],
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
   type = "make",
   build_variables = {
      CFLAGS = "$(CFLAGS)",
      LIBFLAG = "$(LIBFLAG)",
      LUA_LIBDIR = "$(LUA_LIBDIR)",
      LUA_BINDIR = "$(LUA_BINDIR)",
      LUA_INCDIR = "$(LUA_INCDIR)",
      LUA = "$(LUA)",
      TREE_SITTER_DIR = "$(TREE_SITTER_DIR)",
      TREE_SITTER_INCDIR = "$(TREE_SITTER_INCDIR)",
      TREE_SITTER_LIBDIR = "$(TREE_SITTER_LIBDIR)",
      TREE_SITTER_STATIC_LIB = "$(TREE_SITTER_STATIC_LIB)",
      USE_STATIC_TREE_SITTER = "$(USE_STATIC_TREE_SITTER)",
      USE_LIBUV = "$(USE_LIBUV)",
   },
   install_variables = {
      INST_PREFIX = "$(PREFIX)",
      INST_BINDIR = "$(BINDIR)",
      INST_LIBDIR = "$(LIBDIR)",
      INST_LUADIR = "$(LUADIR)",
      INST_CONFDIR = "$(CONFDIR)",
   },
   copy_directories = {
      "docs",
   },
}
