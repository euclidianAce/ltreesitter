local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Parser", function()
	local p
	setup(function()
		p = ts.require("c")
		assert(p)
	end)
	it("parse_string should return a ltreesitter.TSTree", function()
		util.assert_userdata_type(
			p:parse_string[[ int main(void) { return 0; } ]],
			"ltreesitter.TSTree"
		)
	end)
	it("query should return a ltreesitter.TSQuery", function()
		util.assert_userdata_type(
			p:query[[ (comment) ]],
			"ltreesitter.TSQuery"
		)
	end)
	describe("parse_with", function()
		pending("should error out gracefully when the provided function errors", function()

		end)
		pending("should error out gracefully when the provided function returns a non-string value", function()

		end)
		pending("should use the given function to parse chunks of text", function()

		end)
	end)
end)
