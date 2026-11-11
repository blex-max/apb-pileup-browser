#define TB_IMPL
extern "C" {
#include "termbox2.h"
}

#include "tb2_impl.hpp"

std::string_view apb_read_tb2_global_in () {
  return std::string_view{global.in.buf, global.in.len};
}
