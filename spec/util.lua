local assert = require("luassert")

local M = {}

function M.assert_userdata_type(obj, mt_name)
	assert.are.equal(type(obj), "userdata", "object is not a userdata")
	assert.are.equal(getmetatable(obj).__name, mt_name)
end

return M
