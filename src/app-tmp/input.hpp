#include <string>

namespace input {

struct EditBuf {
  std::string text{};
  size_t curs = 0;
};

bool valid (const EditBuf& b);
void insert (EditBuf& b, char c);
void del_back (EditBuf& b);
void del_forward (EditBuf& b);
void move_curs_r (EditBuf& b, size_t i);
void move_curs_l (EditBuf& b, size_t i);
void curs_to_start (EditBuf& b);
void curs_to_end (EditBuf& b);
void clear (EditBuf& b);

}
