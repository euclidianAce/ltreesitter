# ltreesitter

Tree sitter bindings for Lua

## WIP

This is super duper work in progress. Only works on *nix, but should be easy enough to write a windows wrapper (if you're a windows user and you'd like to pr, search for `dlopen` and `dlclose` and make some ifdefs)

- the current bindings that this provides are very much just methods that forward data to the C methods, with little or no higher level abstractions. Eventually I aim to make this some more idiomatic to Lua, but for now its fine
	- This will change as i actually use these bindings
	- If you're curious as to the type of absractions a tl;dr of the plan is some sort of state machine with tree sitter cursors and a whole bunch of iterators to make using parsed trees easier

 - Most of the methods are shown in `test.lua`, with documentation and a `.d.tl` file Coming Eventually™
 - (The idea is that I will use this and a C tree-sitter parser to auto generate both of these from just the source code and comments)

# Installation

`ltreesitter` is avaliable on luarocks

```
$ luarocks install --server=https://luarocks.org/dev ltreesitter
```
