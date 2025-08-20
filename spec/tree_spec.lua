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
	it("get_changed_ranges should return changed ranges", function()
		t:edit_s {
			start_byte    = 18,
			old_end_byte  = 18,
			new_end_byte  = 25,
			start_point   = { row = 0, column = 18 },
			old_end_point = { row = 0, column = 18 },
			new_end_point = { row = 0, column = 25 },
		}
		local u = p:parse_string([[ int main(void) { int a; return 0; } ]], nil, t)
		local c = t:get_changed_ranges(u)
		assert.are.same({{
			start_byte  = 18,
			end_byte    = 24,
			start_point = { row = 0, column = 18 },
			end_point   = { row = 0, column = 24 },
		}}, c)
	end)
end)
