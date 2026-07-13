#include "input.hpp"

#include <format>

namespace input {

std::string EditBuf_to_string(const EditBuf& b)
{
  return std::format(
      "contents: {}, len: {}, curs: {}", b.text, b.text.size(),
      b.curs
  );
}

bool valid(const EditBuf& b)
{
  if (b.curs > b.text.size()) {
    return false;
  }
  return true;
}

void throw_if_invalid(const EditBuf& b)
{
  if (!valid(b)) {
    throw std::runtime_error(
        "EditBuf corrupted! " + EditBuf_to_string(b)
    );
  }
}

void insert(EditBuf& b, char c)
{
  throw_if_invalid(b);
  b.text.insert(b.curs, 1, c);
  ++b.curs;
}
void del_back(EditBuf& b)
{
  throw_if_invalid(b);
  if (b.curs == 0) {
    return;
  }
  b.text.erase(b.curs - 1, 1);
  --b.curs;
}

void clear(EditBuf& b)
{
  b.curs = 0;
  b.text.clear();  // n.b. won't release memory
}

}  // namespace input
