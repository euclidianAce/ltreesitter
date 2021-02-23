{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    lua5_3
    lua53Packages.luarocks

    # teal deps for the docgen script
    lua53Packages.inspect
    lua53Packages.argparse
    lua53Packages.luafilesystem
    lua53Packages.compat53

    # tests
    lua53Packages.busted

    tree-sitter

    valgrind
  ];

  shellHook = ''
  export LUA_CPATH="./?.so;$LUA_CPATH"
  export LUA_PATH="./?.lua;./?/init.lua;$LUA_PATH"
  '';
}
