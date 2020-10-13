
local ts = require("ltreesitter")
local p = ts.require("teal")

local tree = p:parse_string[[
local x: number = 10

local function thing(... :any): boolean
	print(...)
	return true
end

print("hi")
]]

for child in tree:root():children() do
	print(child)
end


