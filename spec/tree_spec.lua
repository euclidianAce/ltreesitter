local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Tree", function()
	local p, t
	setup(function()
		p = util.c_parser
		t = assert(p:parse_string[[ int main(void) { return 0; } ]])
	end)
	it("copy should return a ltreesitter.TSTree", function()
		util.assert_userdata_type(
			t:copy(),
			"ltreesitter.Tree"
		)
	end)
	it("root should return a ltreesitter.TSNode", function()
		util.assert_userdata_type(
			t:root(),
			"ltreesitter.Node"
		)
	end)
end)
