
local ts      = require "ltreesitter"
local inspect = require "inspect"

print("Testing expected errors...")
do
	local p, err = ts.load("./non_existent_parser.so", "teal")
	if not p then
		print(err)
	end
end
do
	local p, err = ts.load("./teal_parser.so", "lua")
	if not p then
		print(err)
	end
end
print("Done with expected errors\n")

local teal_parser = assert(ts.load("./teal_parser.so", "teal"))

print("Teal parser: ", inspect(teal_parser))
print("Teal parser metatable: ", inspect(getmetatable(teal_parser)))
local string_to_parse = [[local x: {string:boolean} = { blah = true }]]
local tree = teal_parser:parse_string(string_to_parse)
local root = tree:get_root()
print(string.format("Parsing string %q", string_to_parse))
print(tree)
print()
print("Tree:")
print(inspect(tree))
print("metatable: ", inspect(getmetatable(tree)))
print()
print("Root node:")
print(inspect(root))
print("metatable: ", inspect(getmetatable(root)))
print("node type:", root:type())
print("root: ", root)

print("node child_count:",       root:get_child_count())
print("node named_child_count:", root:get_named_child_count())

print("node child 1:",       root:get_child(0))
print("node named_child 1:", root:get_named_child(0))

print("node start point: ", inspect(root:get_start_point()))
print("node end point: ",   inspect(root:get_end_point()))

print("node start byte: ", inspect(root:get_start_byte()))
print("node end byte: ",   inspect(root:get_end_byte()))

print("node next sibling", inspect(root:get_next_sibling()))

print("iteration: over ", root:get_child_count(), "children")
for child in root:children() do
	print(child)
	print("all children:");
	for sub_child in child:children() do
		print("", sub_child:name(), sub_child)
	end
	print("named children:")
	for sub_child in child:named_children() do
		print("", child:name(), sub_child)
	end
end
