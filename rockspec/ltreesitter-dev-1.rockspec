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
   type = "builtin",
   modules = {
      ltreesitter = {
         sources = "ltreesitter.c",
         -- TODO: is there a way to make this opt in to libuv?
         libraries = {"tree-sitter", --[["uv"]]},
         incdirs = {"$(TREE_SITTER_INCDIR)"},
         libdirs = {"$(TREE_SITTER_LIBDIR)"},
         -- defines = {"LTREESITTER_USE_LIBUV"},
      },
   },
   install = {
      lua = {
         ["ltreesitter"] = "ltreesitter.d.tl",
      },
   },
   copy_directories = {
      "docs",
   },
}
