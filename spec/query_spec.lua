local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Query", function()
	local p
	setup(function()
		p = assert(ts.require("c"))
	end)
	describe("match", function()
		it("should return an iterator", function()
			local tree = assert(p:parse_string[[
				#include <stdio.h>
				// hello

				int main(void) {
					// world
					printf("Hello World\n");
					return 0;
				}
			]])
			local m = p:query[[ (comment) @a ]]:match(tree:root())
			assert.is["function"](m)
		end)
		it("should never be empty", function()
			local tree = assert(p:parse_string[[
				#include <stdio.h>
				// hello

				int main(void) {
					// world
					printf("Hello World\n");
					return 0;
				}
			]])
			for match in p
				:query[[ (comment) @a ]]
				:match(tree:root())
			do
				assert.are_not.same(match, {})
				assert.is_not["nil"](match.captures.a)
			end
		end)
		it("should not have its internals get garbage collected during iteration", function()
			local tree = assert(p:parse_string[[
				// hello
				// world
				// hello
				// world
				// hello
				// world
			]])
			for match in p
				:query[[ (comment) @a ]]
				:match(tree:root())
			do
				assert.are_not.same(match, {})
				assert.is_not["nil"](match.captures.a)
				collectgarbage()
				collectgarbage()
				collectgarbage()
				collectgarbage()
				collectgarbage()
			end
		end)
	end)
	describe("capture", function()
		it("should return a function", function()
			local tree = assert(p:parse_string[[]])
			local c = p
				:query[[ (comment) @a ]]
				:capture(tree:root())
			assert.is["function"](c)
		end)
		it("should not have its internals get garbage collected during iteration", function()
			local tree = assert(p:parse_string[[
				// hello
				// world
				// hello
				// world
				// hello
				// world
			]])
			for c in p
				:query[[ (comment) @a ]]
				:capture(tree:root())
			do
				assert.is_not["nil"](c)
				collectgarbage()
				collectgarbage()
				collectgarbage()
				collectgarbage()
				collectgarbage()
			end

		end)
		it("should return the name of the capture", function()
			local tree = assert(p:parse_string[[
				// hello
				// world
				// hello
				// world
				// hello
				// world
			]])
			for _, name in p
				:query[[ (comment) @a ]]
				:capture(tree:root())
			do
				assert.are.equal(name, "a")
			end
		end)
	end)
	describe("predicates", function()
		describe("eq?", function()
			local root_node
			setup(function()
				root_node = assert(p:parse_string[[
					// hello
					// world
					// world
					// hello
					// world
					// hello
					// world
				]]):root()
			end)
			it("should work with trivially equal literals", function()
				local done_something = false
				for _ in p
					:query[[(
						(comment)
						(#eq? "a" "a")
					)]]
					:match(root_node)
				do
					done_something = true
				end
				assert(done_something, 'Trivial equality failed ("a" == "a")')
			end)
			it("should work with trivially inequal literals", function()
				for _ in p
					:query[[(
						(comment) @a
						(#eq? "a" "b")
					)]]
					:match(root_node)
				do
					assert(false, 'Trivial inequality failed ("a" ~= "b")')
				end
			end)
			it("should work with @capture and a literal", function()
				local count = 0
				for _ in p
					:query[[(
						(comment) @a
						(#eq? @a "// hello")
						(#eq? "// hello" @a)
					)]]
					:match(root_node)
				do
					count = count + 1
				end
				assert.are.equal(count, 3)
			end)
			it("should work with multiple @captures", function()
				local count = 0
				for _ in p
					:query[[(
						(comment) @a
						.
						(comment) @b
						(#eq? @a @b)
					)]]
					:match(root_node)
				do
					count = count + 1
				end
				assert.are.equal(count, 1)
			end)
			it("should fail when only 1 argument is given", function()
				assert.is["false"](pcall(function()
					p:query[[(
						(comment) @a
						(#eq? @a)
					)]]
					:match(root_node)()
				end))
			end)
		end)
		describe("match?", function()
			local root_node
			setup(function()
				root_node = assert(p:parse_string[[
					// foo
					// bar
					// baz
					// bang
					// blah
					// hoop
				]]):root()
			end)
			it("matches basic patterns", function()
				local res = {}
				for c in p:query[[ ((comment) @a (#match? @a ".a.")) ]]:capture(root_node) do
					table.insert(res, c:source())
				end
				assert.are.same(res, {"// bar", "// baz", "// bang", "// blah"})
			end)
		end)
		pending("[user-defined]", function()
			local root_node
			setup(function()
				root_node = assert(p:parse_string[[
					// foo
					// bar
					// baz
					// bang
					// blah
					// hoop
				]]):root()
			end)
		end)
	end)
	describe("exec", function()
		it("should run the query", function()
			local root_node = assert(p:parse_string[[
				// foo
				// bar
				// baz
				// bang
				// blah
				// hoop
				]]):root()
			local res = {}
			p:query[[ ((comment) @a (#insert! @a)) ]]
				:with{["insert!"] = function(a) table.insert(res, a) end}
				:exec(root_node)
			assert.are.same(res, {
				"// foo",
				"// bar",
				"// baz",
				"// bang",
				"// blah",
				"// hoop",
			})
		end)
	end)
end)
