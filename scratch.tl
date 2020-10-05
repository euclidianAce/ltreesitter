
local ts = require("ltreesitter")
local p = assert(ts.load(os.getenv("HOME") .. "/dev/parsers/teal.so", "teal"))

local string_to_parse = [[
-- %s
local function %s() end

]]
local t = {}
for i = 1, 26 do
	table.insert(t, string.char(i+64))
	table.insert(t, string.char(i+64))
end
local s = string_to_parse:rep(26):format(table.unpack(t))
-- print(string_to_parse)

local tree = p:parse_string(s)

local root = tree:root()
for c, group in p
	:query([[(
	(comment) @the-comment
	.
	(function_statement) @the-function) ]])
	:capture(root)
do
	collectgarbage()
	collectgarbage()
	collectgarbage()
	collectgarbage()
	print("lua!", group, s:sub(c:range()))
end

