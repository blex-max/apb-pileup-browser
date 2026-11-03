#include "input.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace input {

bool valid (const edit_buf_t& b) { return b.curs <= b.text.size(); }

void insert (edit_buf_t& b, char c)
{
  b.text.insert (b.curs, 1, c);
  ++b.curs;
}
void del_back (edit_buf_t& b)
{
  if (b.curs == 0) {
    return;
  }
  b.text.erase (b.curs - 1, 1);
  --b.curs;
}

void del_fwd (edit_buf_t& b)
{
  if (b.curs == b.text.size()) {
    return;
  }
  b.text.erase (b.curs, 1);
}

void del_to_start (edit_buf_t& b)
{
  b.text.erase (0, b.curs);
  b.curs = 0;
}

void del_to_end (edit_buf_t& b) { b.text.erase (b.curs); }

void clear (edit_buf_t& b)
{
  b.curs = 0;
  b.text.clear();  // n.b. won't release memory/change capacity
}

void move_start (edit_buf_t& b) { b.curs = 0; }

void move_end (edit_buf_t& b) { b.curs = b.text.size(); }

void move_left (edit_buf_t& b)
{
  if (b.curs == 0) {
    return;
  }
  --b.curs;
}

void move_right (edit_buf_t& b)
{
  if (b.curs == b.text.size()) {
    return;
  }
  ++b.curs;
}

enum class CharClass { space, word, punct };

static CharClass classify (char c)
{
  const auto u = static_cast<unsigned char> (c);
  if (std::isspace (u) != 0) {
    return CharClass::space;
  }
  return (std::isalnum (u) != 0 || c == '_') ? CharClass::word
                                             : CharClass::punct;
}

// skip spaces, then one run of a single class
void move_word_right (edit_buf_t& b)
{
  const auto n = b.text.size();
  while (b.curs < n && classify (b.text[b.curs]) == CharClass::space) {
    ++b.curs;
  }
  if (b.curs == n) {
    return;
  }
  const auto k = classify (b.text[b.curs]);
  while (b.curs < n && classify (b.text[b.curs]) == k) {
    ++b.curs;
  }
}

void move_word_left (edit_buf_t& b)
{
  while (b.curs > 0 && classify (b.text[b.curs - 1]) == CharClass::space) {
    --b.curs;
  }
  if (b.curs == 0) {
    return;
  }
  const auto k = classify (b.text[b.curs - 1]);
  while (b.curs > 0 && classify (b.text[b.curs - 1]) == k) {
    --b.curs;
  }
}

void del_word_left (edit_buf_t& b)
{
  const auto end = b.curs;
  move_word_left (b);
  b.text.erase (b.curs, end - b.curs);
}

void del_word_right (edit_buf_t& b)
{
  const auto start = b.curs;
  move_word_right (b);
  b.text.erase (start, b.curs - start);
  b.curs = start;
}

void set_text (edit_buf_t& b, std::string s)
{
  b.text = std::move (s);
  b.curs = b.text.size();
}

static void goto_slot (command_line_t& c, size_t i)
{
  auto& h = c.hist;
  h.work[h.idx] = c.buf.text;
  h.idx = i;
  set_text (c.buf, h.work[i]);
}

void history_prev (command_line_t& c)
{
  auto& h = c.hist;
  if (h.idx == 0) {
    return;
  }
  if (h.work.empty()) {
    h.work = h.entries;
    // save edits
    h.work.push_back (c.buf.text);
  }
  goto_slot (c, h.idx - 1);
}

void history_next (command_line_t& c)
{
  if (c.hist.idx >= c.hist.entries.size()) {
    return;
  }
  goto_slot (c, c.hist.idx + 1);
}

std::string reset (command_line_t& c)
{
  std::string text = std::move (c.buf.text);
  clear (c.buf);
  if (!text.empty()) {
    c.hist.entries.push_back (text);
  }
  c.hist.work.clear();
  c.hist.idx = c.hist.entries.size();
  return text;
}

}  // namespace input
