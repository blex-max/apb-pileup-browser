#include "history.hpp"

void history_push (CmdHistory& h, std::string entry)
{
  h.entries.push_back (std::move (entry));
  h.idx = h.entries.size();
}

void history_prev (CmdHistory& h, EditBuf& buf)
{
  if (h.entries.empty() || h.idx == 0) {
    return;
  }
  if (h.idx == h.entries.size()) {
    h.draft = buf.text;
  }
  --h.idx;
  set_text (buf, h.entries[h.idx]);
}

void history_next (CmdHistory& h, EditBuf& buf)
{
  if (h.idx >= h.entries.size()) {
    return;
  }
  ++h.idx;
  if (h.idx == h.entries.size()) {
    set_text (buf, h.draft);
  }
  else {
    set_text (buf, h.entries[h.idx]);
  }
}
