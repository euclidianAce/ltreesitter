
local lfs <const> = require("lfs")
local ts <const> = require("ltreesitter")
local c_lang <const> = ts.require("c")
local p <const> = c_lang:parser()

local ti <const> = table.insert

local function parse_file(filename: string): ts.Tree, string
   local fh <const> = assert(io.open(filename, "r"))
   local content <const> = fh:read("*a")
   fh:close()

   return p:parse_string(content)
end

local files <const> = {}
for f in lfs.dir("csrc") do
   if f:match("%.c$") then
      ti(files, "csrc/" .. f)
   end
end

local mod_name <const> = "ltreesitter"

local dtl_name <const> = "ltreesitter.d.tl"
local html_name <const> = "index.html"

local type RecordDef = {string:string|RecordDef}
local records <const>: RecordDef = {}

local inlines <const>: {string} = {}

local type DocDef = {string:string|DocDef}
local docs <const>: DocDef = {}

local function trim(s: string): string
   return s:match("^%s*(.-)%s*$")
end

local function split(str: string, del: string, no_patt?: boolean): function(): string
   local i = 0
   local prev_i, s: integer, integer
   return function(): string
      if not i then return end
      i = i + 1
      prev_i = i
      s, i = str:find(del, i, no_patt)
      if s and i and i < s then
         error("Delimiter matched the empty string", 2)
      end
      return str:sub(prev_i, (s or 0) - 1)
   end
end

local function insert_into(
   -- This type should actually be a recursive type: T = {string:string|T}
   dest: {string:any},
   name: string,
   content: string
)
   local components <const>: {string} = {}
   for comp in split(name, ".", true) do
      ti(components, comp)
   end

   if #components > 1 then
      local base = dest[components[1]] as {string:any} or {}
      assert(type(base) == "table")
      local current = base
      local len = #components
      for i = 2, len - 1 do
         if not current[components[i]] then
            current[components[i]] = {}
         end
         current = current[components[i]] as {string:any}
         assert(type(current) == "table")
      end
      current[components[len]] = content
      dest[components[1]] = base
   else
      dest[components[1]] = content
   end
end

local interface Location
   file: string
   row: integer
   column: integer
end

local type Directive = function(Location, inner: string, bracket_content: string)

-- TODO: should probably just use the teal parser to pull out these type definitions
local directives: {string:Directive} = {
   ["@teal-export"] = function(l: Location, inner: string, documentation: string)
      local name <const>, typedef <const> = inner:match("%s*(.-)%s*:%s*(.*)")
      assert(typedef, l.file .. ":" .. l.row .. ":" .. l.column .. ": No typedef after teal-export?")
      if not documentation then
         io.stderr:write(l.file, ":", l.row, ":", l.column, ": Exported item ‘", name, "’ has no documentation!\n")
      end
      if name then
         if documentation then
            insert_into(docs, name, "<" .. typedef .. ">" .. trim(documentation))
         end
         insert_into(records, name, typedef)
      end
   end,

   ["@teal-inline"] = function(_: Location, _: string, inline_content: string)
      ti(inlines, (inline_content:gsub("[^\n]+", "   %1")))
   end,
}

local comment_query <const> = c_lang:query[[
   ((comment)+ @comments)
]]

local function comment_block_contents(location: Location, nodes: {ts.Node}): string
   local content = {}
   local reported_c89_comment = false
   for i, comment in ipairs(nodes) do
      local src = comment:source()
      if src:sub(1, 2) == "//" then
         src = src:gsub("^//+", "") .. "\n"
      else
         src = src:gsub("^/%*+", ""):gsub("%*/$", "")
         if not reported_c89_comment then
            io.stderr:write(location.file, ":", location.row, ":", location.column, ":warning: C89 style comment\n");
            reported_c89_comment = true
         end
      end
      content[i] = src
   end
   return table.concat(content)
end

local function proc_file(file: string)
   local tree <const> = parse_file(file)
   for c in comment_query:match(tree:root()) do
      local comments <const> = c.captures.comments as {ts.Node}
      local start <const> = comments[1]:start_point()
      local loc <const>: Location = {
         file = file,
         row = start.row + 1,
         column = start.column + 1,
      }
      local src <const> = comment_block_contents(loc, comments)

      local offset = 1
      while offset <= #src do
         local directive_name_start, directive_name_end = src:find("%@teal%-[-a-zA-Z]+", offset)
         if not directive_name_start then
            break
         end
         local directive <const> = src:sub(directive_name_start, directive_name_end)
         --print("directive = " .. directive)

         local eq_s, eq_e = src:find("%[()%=*()%[", directive_name_end + 1) as (integer, integer)
         if not eq_s then
            offset = directive_name_end + 1
            io.stderr:write(loc.file, ":", loc.row, ":", loc.column, ": Directive “", directive, "” has no open brackets [[\n")
            break
         end

         local inner <const> = trim(src:sub(directive_name_end + 1, eq_s - 1))
         --print("inner = «" .. inner .. "»")

         local content_start = eq_e + 1
         local equals <const> = ("="):rep(eq_e - eq_s - 1)
         local content_end, next_offset = src:find("%]" .. equals .. "%]", content_start)
         if not content_end then
            offset = content_start
            io.stderr:write(loc.file, ":", loc.row, ":", loc.column, ": Unclosed [", equals, "[\n")
            break
         end

         offset = (next_offset or #src) + 1

         local callback <const> = directives[directive]
         if callback then
            local outer = src:sub(content_start, content_end - 1)
            outer = outer:gsub("^[ \t]*\n+", "")
            local initial_indent <const> = outer:match("^ +")
            if initial_indent then
               outer = outer:gsub("\n" .. initial_indent, "\n")
            end
            outer = trim(outer)
            callback(loc, inner, outer)
         else
            io.stderr:write(loc.file, ":", loc.row, ":", loc.column, ": Unknown directive “", directive, "”\n")
         end
      end
   end
end

for _, file in ipairs(files) do
   proc_file(file)
end

-- Sort the record definitions so we don't get an ugly diff every time we regenerate docs
local record NamedRecordDef
   name: string
   def: string|{NamedRecordDef}
end
local record_def_sorter <const> = function(a: NamedRecordDef, b: NamedRecordDef): boolean
   return a.name < b.name
end

local function get_sorted_records(tree: RecordDef): {NamedRecordDef}
   local defs: {NamedRecordDef} = {}
   for entry_name, entry_def in pairs(tree) do
      if entry_def is RecordDef then
         ti(defs, {name = entry_name, def = get_sorted_records(entry_def)})
      else
         ti(defs, {name = entry_name, def = entry_def})
      end
   end
   table.sort(defs, record_def_sorter)
   return defs
end

-- generate the record definition
local output <const>: {string} = {}
local module <const>: {NamedRecordDef} = get_sorted_records(records)

local function generate_record_defs(defs: {NamedRecordDef}, indent: integer)
   if not defs then return end
   indent = indent or 0
   for _, def in ipairs(defs) do
      local rec_def <const> = def.def
      if rec_def is {NamedRecordDef} then
         ti(output, ("   "):rep(indent))
         ti(output, "record ")
         ti(output, def.name)
         ti(output, " is userdata\n")
         generate_record_defs(rec_def, indent + 1)
         ti(output, ("   "):rep(indent))
         ti(output, "end\n")
      else
         for t in split(rec_def, "&", true) do
            t = trim(t)
            ti(output, ("   "):rep(indent))
            ti(output, def.name)
            ti(output, ": ")
            ti(output, (
               t:gsub("\n +", "\n" .. ("   "):rep(indent + 1))
                  :gsub("\n[ \t]*([^\n]*)$", "\n" .. ("   "):rep(indent) .. "%1")
            ))
            ti(output, "\n")
         end
      end
   end
end

ti(output, "local record ltreesitter\n")
ti(output, "   -- Exports\n\n")
generate_record_defs(module, 1)
ti(output, "\n   -- Inlines\n")
ti(output, table.concat(inlines, "\n"))
ti(output, "\nend\n\nreturn ltreesitter")

local dtl_src <const> = "-- Autogenerated type definitions\n\n"
   .. table.concat(output) .. "\n"

do
   io.write("Generating ", dtl_name, "...\n")
   local fh <const> = io.open(dtl_name, "w")
   fh:write(dtl_src)
   fh:close()
   io.write(" Done.\n")
   io.flush()
end

local record DocInfo is {string}
   mod: string
   index: string
   doc: string
end

local function traverse_docs(t: DocDef, prefix?: string, entries?: {DocInfo}): {DocInfo}
   prefix = prefix or ""
   entries = entries or {}
   for name, v in pairs(t) do
      if v is DocDef then
         traverse_docs(v, prefix .. name .. ".", entries)
      else
         local full_name = prefix .. name
         local typ, content = v:match("^<(.-)>(.*)$")
         if not typ then
            typ = "any"
            content = v
         end
         if not content then
            content = ""
         end

         local pre <const> = '<doc-item id="' .. full_name .. '"><h2><a href="#' .. full_name .. '">❖</a>'
         local suf <const> = "</h2><div>" .. content .. "</div></doc-item>"
         local mid = {}
         for chunk in split(typ, "&", true) do
            chunk = trim(chunk)
            chunk = chunk:gsub("\n([ \t]+)([^\n]*)$", "\n%2")
            ti(mid, "<code>" .. full_name .. ": " .. chunk .. "</code><br>")
         end
         mid[#mid] = mid[#mid]:gsub("<br>$", "")

         ti(entries, {
            full_name,
            mod = prefix:sub(1, -2),
            index = string.format(
               '<a href="#%s">%s</a>',
               full_name, full_name
            ),
            doc = pre .. table.concat(mid) .. suf,
         })
      end
   end
   return entries
end

local doc_entries <const> = traverse_docs(docs)

table.sort(doc_entries, function(a: DocInfo, b: DocInfo): boolean
   if a.mod == b.mod then
      return a[1] < b[1]
   end
   return a.mod < b.mod
end)

-- dark mode lua manual sorta look
local default_style <const> = [==[<style>
html {
   font-size: 18pt;
   background-color: #16131F;
}

body {
   background-color: #181520;
   margin: auto;
   max-width: 70em;
   width: 95%;
   padding: 32px;
   font-family: Helvetica, Arial, sans-serif;
   color: #e8e6e3;
}

a {
   text-decoration: none;
   color: #3391ff;
}

h2 {
   color: #117dee;
}

pre {
   background-color: #2B2735;
   padding: 0.5em;
   border-radius: 0.5em;
   overflow-x: auto;
}

module-listing {
   padding: 2em;
   list-style: none;

   & > a {
      display: list-item;
      margin-left: 4em;
   }
}

code {
   white-space: pre;
}

doc-item {
   display: flex;
   flex-direction: column;
   margin-bottom: 2em;

   & > * {
      &:not(h2) {
          margin-left: 4em;
      }
   }

   & > h2 {
      position: relative;
      font-size: 1.2em;

      & > a {
         display: none;
         position: absolute;
         top: -0.2em; /* This is a hack */
         left: -1em;
         padding-right: 0.3em;
      }
   }

   &:hover > h2 > a {
      display: initial;
   }
}
</style>]==]

do
   io.write("Writing docs to docs/", html_name, "...")
   io.flush()
   local fh = io.open("docs/" .. html_name, "w")
   local w <const> = function(...: string) fh:write(...) fh:write("\n") end

   w"<html>"
   w(default_style)
   w"<body>"

   w "<div><h2>Note:</h2>"
   w "<ul>"
   w "<li>This documentation is for the latest main branch. <em>NOT</em> the latest release.</li>"
   w '<li>The annotated types are for <a href="https://github.com/teal-language/tl">Teal</a>, where all types may implicitly be <code>nil</code></li>'
   w "</ul></div>"

   local current_module = doc_entries[1].mod
   if current_module == "" then
      current_module = mod_name
   end

   local documentation <const> = {}

   w("<nav><module-listing><h3>", current_module, "</h3>")

   for _, entry in ipairs(doc_entries) do
      if entry.mod ~= "" and entry.mod ~= current_module then
         w("</module-listing><module-listing>")
         current_module = entry.mod
         w("<h3>", current_module, "</h3>")
         ti(documentation, "")
      end
      w(entry.index)
      ti(documentation, entry.doc)
   end

   w("</module-listing></nav><hr>")

   for _, doc in ipairs(documentation) do
      w(doc)
   end

   w"</body></html>"
   fh:close()
   io.write("\n Done.\n")
end

