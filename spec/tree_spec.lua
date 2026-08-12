local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Tree", function()
	local c, p, t
	setup(function()
		c, p = util.load_c_parser()
		t = assert(p:parse_string[[ int main(void) { return 0; } ]])
	end)

	describe("language", function()
		it("should return a ltreesitter.Language", function()
			util.assert_userdata_type(t:language(), "ltreesitter.Language")
		end)
		it("should return the correct language", function()
			assert.are.equal(t:language(), c)
		end)
	end)

	it("copy should return a ltreesitter.Tree", function()
		util.assert_userdata_type(
			t:copy(),
			"ltreesitter.Tree"
		)
	end)
	it("root should return a ltreesitter.Node", function()
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
