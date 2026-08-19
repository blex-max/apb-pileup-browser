#include "input.hpp"

#include <cctype>

bool valid (const EditBuf& b) { return b.curs <= b.text.size(); }

void insert (EditBuf& b, char c)
{
  b.text.insert (b.curs, 1, c);
  ++b.curs;
}
void del_back (EditBuf& b)
{
  if (b.curs == 0) {
    return;
  }
  b.text.erase (b.curs - 1, 1);
  --b.curs;
}

void clear (EditBuf& b)
{
  b.curs = 0;
  b.text.clear();  // n.b. won't release memory/change capacity
}

void move_start (EditBuf& b) { b.curs = 0; }

void move_end (EditBuf& b) { b.curs = b.text.size(); }

void move_left (EditBuf& b)
{
  if (b.curs == 0) {
    return;
  }
  --b.curs;
}

void move_right (EditBuf& b)
{
  if (b.curs == b.text.size()) {
    return;
  }
  ++b.curs;
}

void move_word_right (EditBuf& b)
{
  while (b.curs < b.text.size() &&
         std::isalnum (
             static_cast<unsigned char> (b.text[b.curs])
         ) == 0) {
    ++b.curs;
  }
  while (b.curs < b.text.size() &&
         std::isalnum (
             static_cast<unsigned char> (b.text[b.curs])
         ) != 0) {
    ++b.curs;
  }
}

void move_word_left (EditBuf& b)
{
  while (b.curs > 0 &&
         std::isalnum (
             static_cast<unsigned char> (b.text[b.curs - 1])
         ) == 0) {
    --b.curs;
  }
  while (b.curs > 0 &&
         std::isalnum (
             static_cast<unsigned char> (b.text[b.curs - 1])
         ) != 0) {
    --b.curs;
  }
}

void set_text (EditBuf& b, std::string s)
{
  b.text = std::move (s);
  b.curs = b.text.size();
}
