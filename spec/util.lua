local assert = require("luassert")

local M = {}

function M.assert_userdata_type(obj, mt_name, message)
	assert(mt_name, "You forgot to put a __name for the metatable you dingus.")
	assert.are.equal(type(obj), "userdata", "object is not a userdata")
	assert.are.equal(getmetatable(obj).__name, mt_name, message)
end

return M
