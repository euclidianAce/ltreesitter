local ts = require("ltreesitter")
local assert = require("luassert")

local util = {}

function util.assert_userdata_type(obj, mt_name, message)
	assert(mt_name, "You forgot to put a __name for the metatable you dingus.")
	assert.is("userdata", type(obj), "object is not a userdata")
	assert.are.equal(mt_name, getmetatable(obj).__name, message)
	return obj
end

function util.load_c_parser()
	package.cpath = package.cpath .. ";" .. os.getenv "HOME" .. "/.tree-sitter/bin/?.so"
	local ok, c_language = pcall(ts.require, "c")
	if not ok then
		error("The ltreesitter test suite requires a C parser in your LUA_CPATH\n\n" .. tostring(c_parser))
	end
	return c_language, c_language:parser()
end

return util
