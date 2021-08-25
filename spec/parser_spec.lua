local assert = require("luassert")
-- local ts = require("ltreesitter")
local util = require("spec.util")

describe("Parser", function()
	local p
	setup(function()
		p = util.c_parser
	end)
	it("parse_string should return a ltreesitter.Tree", function()
		util.assert_userdata_type(
			p:parse_string[[ int main(void) { return 0; } ]],
			"ltreesitter.Tree"
		)
	end)
	it("query should return a ltreesitter.Query", function()
		util.assert_userdata_type(
			p:query[[ (comment) ]],
			"ltreesitter.Query"
		)
	end)
	describe("set_ranges", function()
		it("should return a boolean", function()
			assert.is.boolean(p:set_ranges())
		end)
		it("should error out when ranges overlap", function()
			assert.has.errors(function()
				p:set_ranges{
					{
						start_byte = 0, end_byte = 5,
						start_point = { row = 0, column = 0 },
						end_point   = { row = 0, column = 5 },
					},
					{
						start_byte = 3, end_byte = 10,
						start_point = { row = 0, column = 3 },
						end_point   = { row = 0, column = 10 },
					}
				}
			end)
		end)
	end)
	describe("get_ranges", function()
		it("should return table", function()
			assert.is.boolean(p:set_ranges())
		end)
		it("should return an array of Points", function()
			local ranges = p:get_ranges()
			assert(#ranges > 0)
			for _, r in ipairs(ranges) do
				assert.is.number(r.start_byte)
				assert.is.number(r.end_byte)
				assert.is.table(r.start_point)
				assert.is.number(r.start_point.row)
				assert.is.number(r.start_point.column)
				assert.is.table(r.end_point)
				assert.is.number(r.end_point.row)
				assert.is.number(r.end_point.column)
			end
		end)
		it("should return the values from set_ranges", function()
			local ranges = {
				{
					start_byte = 0, end_byte = 5,
					start_point = { row = 0, column = 0 },
					end_point   = { row = 0, column = 5 },
				},
				{
					start_byte = 6, end_byte = 10,
					start_point = { row = 0, column = 6 },
					end_point   = { row = 0, column = 10 },
				}
			}
			assert(p:set_ranges(ranges))
			local got = p:get_ranges()
			p:set_ranges()
			assert.are.same(got, ranges)
		end)
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
						tree, "ltreesitter.Tree"
					):root(), "ltreesitter.Node"
				):child_count(),
				2
			)
		end)
	end)
end)
