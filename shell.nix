{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    lua5_3
    lua53Packages.luarocks

    # tests
    lua53Packages.busted

    tree-sitter

    valgrind
  ];

  shellHook = ''
  export LUA_CPATH="./?.so;$LUA_CPATH"
  export LUA_PATH="./?.lua;./?/init.lua;$LUA_PATH"
  export PATH="$PWD:$PWD/lua_modules/bin:$PATH"
  '';
}
