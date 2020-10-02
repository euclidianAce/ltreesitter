
-- local inspect = require "inspect"
local ts = require "ltreesitter"
local p = assert(ts.load("./teal_parser.so", "teal"))

local string_to_parse = [[
-- this is a comment that is not followed by a function
local x: number = 5

-- thing
local y: number = 1


-- this is the comment that i care about
local function other_thing(): function()
   local function blah()
   end
   return blah
end
]]

local tree = p:parse_string(string_to_parse)

print("The tree: ", tostring(tree):gsub(" ", "\n"), "\n")
local root = tree:get_root()

for c in p:query[[ (comment) @the-comment ]]:match(root) do
   print("matches:")
   print("\"" .. string_to_parse:sub(c:range()):gsub("\n", "\\n") .. "\"")
   print("next sibling: ", c:get_next_sibling())
end

