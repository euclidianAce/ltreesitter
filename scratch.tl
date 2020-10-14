
local ts = require("ltreesitter")
local p = ts.require("teal")

local tree = p:parse_string[[
local x: number = 10

-- hello
local function thing(... :any): boolean
	print(...)
	return true
end

-- hi
print("hi")
]]

for match in p:query[[ (comment) @a ]]:match(tree:root()) do
	print(require"inspect"(match))
end

