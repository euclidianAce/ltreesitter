local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Parser", function()
	local p
	setup(function()
		p = util.c_parser
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
		it("should error out gracefully when the provided function errors", function()
			assert.has.errors(function()
				p:parse_with(function()
					error("lolno")
				end)
			end)
		end)
		it("should error out gracefully when the provided function returns a non-string value", function()
			assert.has.errors(function()
				p:parse_with(function()
					return 1
				end)
			end)
		end)
		it("should use the given function to parse chunks of text", function()
			local lines = {
				"#include <stdio.h>",
				"int main(void) {",
				"    printf(\"hello world\\n\");",
				"    return 0;",
				"}",
			}
			local function read_lines(_byte_idx, point)
				local ln = lines[point.row + 1]
				if ln then
					return ln:sub(point.column + 1, -1) .. "\n"
				end
			end
			local tree = p:parse_with(read_lines)
			assert.are.equal(
				util.assert_userdata_type(
					util.assert_userdata_type(
						tree, "ltreesitter.TSTree"
					):root(), "ltreesitter.TSNode"
				):child_count(),
				2
			)
		end)
	end)
end)
