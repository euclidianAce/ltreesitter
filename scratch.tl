
local ts = require "ltreesitter"
local p = assert(ts.load(os.getenv("HOME") .. "/dev/parsers/teal.so", "teal"))

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
local root = tree:root()

local q = p:query[[ (comment) @the-comment ]]

-- TODO: what is the difference???
print("matches:")
for c in q:match(root) do
   print("\"" .. string_to_parse:sub(c:range()):gsub("\n", "\\n") .. "\"")
end

print("captures:")
for c in q:capture(root) do
   print("\"" .. string_to_parse:sub(c:range()):gsub("\n", "\\n") .. "\"")
end

