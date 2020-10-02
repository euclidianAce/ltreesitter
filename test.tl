
local r = require
local ts      = require "ltreesitter"
local inspect = r "inspect" as function(any, table): string -- TODO: is this .d.tl really not written yet???

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
-- for some reason inspect doesn't print the metatable of userdata...
print("Teal parser metatable: ", inspect(getmetatable(teal_parser)))

local string_to_parse = [[local x: {string:boolean} = { blah = true }]]
local tree = teal_parser:parse_string(string_to_parse)
local root = tree:root()
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

print("node child_count:",       root:child_count())
print("node named_child_count:", root:named_child_count())

-- api is 0 indexed, may have lua index it from one?
print("node child 1:",        root:child(0))
print("node named_child 1:",  root:named_child(0))

print("node start point: ",   inspect(root:start_point()))
print("node end point: ",     inspect(root:end_point()))

print("node start byte: ",    inspect(root:start_byte()))
print("node end byte: ",      inspect(root:end_byte()))

print("node next sibling",    inspect(root:next_sibling()))

-- Some convenient iterators over a node's children
print("iteration: over ", root:child_count(), "children")
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
