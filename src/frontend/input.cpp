#include "input.hpp"

#include <format>

static std::string to_string (const EditBuf& b)
{
  return std::format (
      "contents: {}, len: {}, curs: {}", b.text, b.text.size(),
      b.curs
  );
}

bool valid (const EditBuf& b)
{
  if (b.curs > b.text.size()) {
    return false;
  }
  return true;
}

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
