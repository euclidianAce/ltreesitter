
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

for capture in p:query[[ (comment) @a ]]:capture(tree:root()) do
	print(capture:source())
end

