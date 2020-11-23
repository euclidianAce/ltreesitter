local ts = require("ltreesitter")
local assert = require("luassert")

local M = {}

function M.assert_userdata_type(obj, mt_name, message)
	assert(mt_name, "You forgot to put a __name for the metatable you dingus.")
	assert.are.equal(type(obj), "userdata", "object is not a userdata")
	assert.are.equal(getmetatable(obj).__name, mt_name, message)
	return obj
end

local ok, c_parser = pcall(ts.require, "c")
if not ok then
	error("The ltreesitter test suite requires a C parser in your LUA_CPATH")
end
M.c_parser = c_parser

return M
