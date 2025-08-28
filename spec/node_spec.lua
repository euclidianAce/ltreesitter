local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Node", function()
	local str = {
		[[ /* hello world */ ]],
		[[ /* hello world */ int main(void) { return 0; } ]],
		[[ const int x = 1; int main(void) { return 1; } ]],
	}
	local tree = {}
	local root = {}
	local c_lang, p
	setup(function()
		c_lang, p = util.load_c_parser()
		for i, v in ipairs(str) do
			tree[i] = assert(p:parse_string(v), "Failed parsing string: " .. v)
			root[i] = assert(tree[i]:root(), "Failed getting root of tree " .. tostring(i))
		end
	end)
	it("name should return the name of the node", function()
		assert.are.equal(root[1]:name(), "translation_unit")
	end)
	it("type should return the type of the node", function()
		assert.are.equal(root[1]:type(), "translation_unit")
	end)
	it("child should return a Node (and the correct child)", function()
		local n = root[1]:child(0)
		util.assert_userdata_type(n, "ltreesitter.Node")
		assert.are.equal(n:name(), "comment")
	end)
	it("child_count should return the correct number of children", function()
		local n = root[2]:child_count()
		assert.are.equal(type(n), "number")
		assert.are.equal(n, 2, "incorrect number of children")
	end)

	describe("child_by_field_name", function()
		it("should return an ltreesitter.Node", function()
			local n = assert(root[3]:child(0))
			util.assert_userdata_type(n:child_by_field_name("type"), "ltreesitter.Node")
		end)
		it("should return the correct node", function()
			local n = assert(root[3]:child(0))
			assert.are.equal(n:child_by_field_name("type"):type(), "primitive_type")
		end)
	end)
	describe("child_by_field_id", function()
		it("should return an ltreesitter.Node", function()
			local n = assert(root[3]:child(0))
			local id = assert(c_lang:field_id_for_name("type"))
			util.assert_userdata_type(n:child_by_field_id(id), "ltreesitter.Node")
		end)
		it("should return the correct node", function()
			local n = assert(root[3]:child(0))
			local id = assert(c_lang:field_id_for_name("type"))
			assert.are.equal(n:child_by_field_id(id):type(), "primitive_type")
		end)
	end)

	it("children should iterate over all the children of a node", function()
		-- TODO: find a case where children and named_children differ
		local actual_child_names = {}
		local actual_child_types = {}
		for child in assert(root[2]:child(1)):children() do
			util.assert_userdata_type(child, "ltreesitter.Node")
			table.insert(actual_child_names, child:name())
			table.insert(actual_child_types, child:type())
		end
		assert.are.same(actual_child_names, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
		assert.are.same(actual_child_types, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
	end)
	it("named_children should iterate over all the named children of a node", function()
		local actual_child_names = {}
		local actual_child_types = {}
		for child in assert(root[2]:child(1)):named_children() do
			util.assert_userdata_type(child, "ltreesitter.Node")
			table.insert(actual_child_names, child:name())
			table.insert(actual_child_types, child:type())
		end
		assert.are.same(actual_child_names, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
		assert.are.same(actual_child_types, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
	end)

	it("create_cursor should return an ltreesitter.TreeCursor", function()
		util.assert_userdata_type(root[1]:create_cursor(), "ltreesitter.TreeCursor")
	end)
	describe("start_byte", function()
		it("should return a number", function()
			assert.is.number(root[1]:start_byte())
		end)
		it("should return the correct value", function()
			assert.are.equal(root[1]:start_byte(), 1, "start_byte is not the same")
		end)
	end)
	describe("end_byte", function()
		it("should return a number", function()
			assert.is.number(root[1]:end_byte())
		end)
		it("should return the correct value", function()
			assert.are.equal(root[1]:end_byte(), 19, "end_byte is not the same")
		end)
	end)

	describe("start_point", function()
		it("should return a table with a row: number and column: number field", function()
			local point = root[1]:start_point()
			assert.is.table(point)
			assert.is.number(point.row)
			assert.is.number(point.column)
		end)
	end)
	describe("end_point", function()
		it("should return a table with a row: number and column: number field", function()
			local point = root[1]:end_point()
			assert.is.table(point)
			assert.is.number(point.row)
			assert.is.number(point.column)
		end)
	end)
	describe("source", function()
		it("should return a string", function()
			local src = root[1]:source()
			assert.is.string(src)
		end)
		it("should return the correct string", function()
			local src = assert(root[2]:child(0), "Error in Node:child"):source()
			assert.are.equal(src, "/* hello world */")
		end)

		it("should work when the tree was parsed with a reader function", function()
			local lines = {
				"#include <stdio.h>\n",
				"int main(void) {\n",
				"    printf(\"hello world\\n\");\n",
				"    return 0;\n",
				"}\n",
			}
			local function read_by_line(_byte_idx, zero_indexed_point)
				local point = { row = zero_indexed_point.row + 1, column = zero_indexed_point.column + 1 }
				local ln = lines[point.row]
				while ln and point.column > #ln do
					point.row = point.row + 1
					point.column = 1
					ln = lines[point.row]
				end
				if not ln then return end
				local len = math.random(1, 10)
				local result = ln:sub(point.column, point.column + len - 1)
				return result
			end

			local tree_with_reader = p:parse_with(read_by_line)

			local root = tree_with_reader:root()
			assert.are.equal("#include <stdio.h>\n", root:child(0):source())
			local c = root:child(1):child(2):child(2)
			assert.are.equal("return 0;", c:source())
		end)
	end)

	-- TODO: assert the correct values for these
	it("is_extra should return a boolean", function()
		assert.is.boolean(root[1]:is_extra())
	end)
	it("is_missing should return a boolean", function()
		assert.is.boolean(root[1]:is_missing())
	end)
	it("is_named should return a boolean", function()
		assert.is.boolean(root[1]:is_named())
	end)

	describe("named_child", function()
		it("should return an ltreesitter.Node", function()
			util.assert_userdata_type(root[1]:named_child(0), "ltreesitter.Node")
		end)
		it("should return the correct node", function()
			local n = assert(root[1]:named_child(0))
			assert.are.equal(n:type(), "comment")
		end)
	end)

	describe("named_child_count", function()
		it("should return a number", function()
			-- TODO: assert the correct number
			assert.is.number(root[1]:named_child_count())
		end)
	end)

	describe("next_named_sibling", function()
		it("should return an ltreesitter.Node", function()
			util.assert_userdata_type(root[2]:child(0):next_named_sibling(), "ltreesitter.Node")
		end)
	end)
	describe("next_sibling", function()
		it("should return an ltreesitter.Node", function()
			util.assert_userdata_type(root[2]:child(0):next_sibling(), "ltreesitter.Node")
		end)
	end)
	describe("prev_named_sibling", function()
		it("should return an ltreesitter.Node", function()
			util.assert_userdata_type(root[2]:child(1):prev_named_sibling(), "ltreesitter.Node")
		end)
	end)
	describe("prev_sibling", function()
		it("should return an ltreesitter.Node", function()
			util.assert_userdata_type(root[2]:child(1):prev_sibling(), "ltreesitter.Node")
		end)
	end)

	describe("parse_state", function()
		it("should return an integer", function()
			assert.is.number(root[1]:parse_state())
		end)
	end)
	describe("next_parse_state", function()
		it("should return an integer", function()
			assert.is.number(root[1]:next_parse_state())
		end)
	end)

	describe("symbol", function()
		it("should return an integer", function()
			assert.is.number(root[1]:symbol())
		end)
	end)

	describe("grammar_symbol", function()
		it("should return an integer", function()
			assert.is.number(root[1]:grammar_symbol())
		end)
	end)

	describe("grammar_type", function()
		it("should return a string", function()
			assert.is.string(root[1]:grammar_type())
		end)
	end)
end)
