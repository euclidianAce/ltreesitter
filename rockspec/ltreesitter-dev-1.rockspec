package = "ltreesitter"
version = "dev-1"
source = {
   url = "git://github.com/euclidianAce/ltreesitter"
}
description = {
   homepage = "https://github.com/euclidianAce/ltreesitter",
   license = "MIT",
}
external_dependencies = {
   TREE_SITTER = {
      header = "tree_sitter/api.h"
   },
}
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
}
