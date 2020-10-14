rockspec_format = "3.0"
package = "ltreesitter"
version = "0.0.1-1"
source = {
   url = "git+https://github.com/euclidianAce/ltreesitter.git",
   tag = "v0.0.1",
}
description = {
   summary = "Treesitter bindings to Lua",
   detailed = "Standalone Lua bindings to the Treesitter api.",
   homepage = "https://github.com/euclidianAce/ltreesitter",
   license = "MIT",
   issues_url = "https://github.com/euclidianAce/ltreesitter/issues",
}
supported_platforms = {
   "unix"
}
external_dependencies = {
   TREE_SITTER = {
      header = "tree_sitter/api.h"
   }
}
build = {
   type = "builtin",
   modules = {
      ltreesitter = {
         incdirs = {
            "$(TREE_SITTER_INCDIR)"
         },
         libdirs = {
            "$(TREE_SITTER_LIBDIR)"
         },
         libraries = {
            "tree-sitter"
         },
         sources = "ltreesitter.c"
      }
   },
   copy_directories = {
      "docs"
   },
   install = {
      lua = {
         ltreesitter = "ltreesitter.d.tl"
      }
   }
}
