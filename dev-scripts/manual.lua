-- pandoc filter for the HTML manual.
-- First H1 becomes the page title; **IMPORTANT**: paragraphs become callout boxes.

local stringify = pandoc.utils.stringify

local function is_important(b)
  return b.t == "Para" and b.content[1] and b.content[1].t == "Strong"
    and stringify(b.content[1]) == "IMPORTANT"
end

local function ends_colon(b)
  return b.t == "Para" and stringify(b):match(":%s*$") ~= nil
end

local function strip_label(para)
  local c = para.content
  c:remove(1) -- IMPORTANT
  while c[1] and (c[1].t == "Space" or c[1].t == "SoftBreak" or c[1].t == "LineBreak"
                  or (c[1].t == "Str" and c[1].text == ":")) do
    c:remove(1)
  end
  return para
end

function Pandoc(doc)
  local out = pandoc.Blocks{}
  local blocks = doc.blocks
  local i = 1
  local titled = false
  while i <= #blocks do
    local b = blocks[i]
    if not titled and b.t == "Header" and b.level == 1 then
      doc.meta.title = b.content
      titled = true
      i = i + 1
    elseif is_important(b) then
      local body = pandoc.Blocks{ strip_label(b) }
      i = i + 1
      -- pull in examples the callout introduces ("...that:" + code block)
      while blocks[i] do
        local prev, nxt = body[#body], blocks[i]
        if ends_colon(prev) or (prev.t == "CodeBlock" and ends_colon(nxt)) then
          body:insert(nxt)
          i = i + 1
        else
          break
        end
      end
      body:insert(1, pandoc.Div(pandoc.Plain{ pandoc.Str "Important" }, { class = "callout-title" }))
      out:insert(pandoc.Div(body, { class = "callout" }))
    else
      out:insert(b)
      i = i + 1
    end
  end
  doc.blocks = out
  return doc
end
