{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    lua5_3
    lua53Packages.luarocks
    lua53Packages.busted

    # luajit
    # luajitPackages.luarocks
    # luajitPackages.busted

    tree-sitter

    valgrind

    # clang-format
    clang_12
  ];

  shellHook = ''
  export LUA_CPATH="./?.so;$LUA_CPATH"
  export LUA_PATH="./?.lua;./?/init.lua;$LUA_PATH"
  export PATH="$PWD:$PWD/lua_modules/bin:$PATH"
  '';
}
