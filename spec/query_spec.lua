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
end)
