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
   issues_url = "https://github.com/euclidianAce/ltreesitter/issues"
}
external_dependencies = {
   TREE_SITTER = {
      header = "tree_sitter/api.h"
   },
}
supported_platforms = {"unix"}
build = {
   type = "builtin",
   modules = {
      ltreesitter = {
         sources = "ltreesitter.c",
         libraries = {"tree-sitter"},
         incdirs = {"$(TREE_SITTER_INCDIR)"},
         libdirs = {"$(TREE_SITTER_LIBDIR)"},
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
