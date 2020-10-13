local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Query", function()
	local p
	setup(function()
		p = assert(ts.require("c"))
	end)
	pending("capture", function() end)
	pending("match", function() end)
end)
