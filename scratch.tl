
local ts = require "ltreesitter"
local p = assert(ts.load(os.getenv("HOME") .. "/dev/parsers/teal.so", "teal"))

local string_to_parse = [[
local function thing(): {a:b}
end

local function blah(): number
end
]]

local tree = p:parse_string(string_to_parse)

print("The tree: ", tostring(tree):gsub(" ", "\n"), "\n")
local root = tree:root()
local cursor = root:create_cursor()
cursor:goto_first_child()
repeat
	print(cursor:current_node())
until not cursor:goto_next_sibling()
