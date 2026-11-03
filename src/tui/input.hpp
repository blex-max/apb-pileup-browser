#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace input {

struct edit_buf_t {
  std::string text;
  size_t curs = 0;
};

struct history_t {
  std::vector<std::string> entries;
  size_t idx = 0;  // == entries.size() means at input line
  // per-slot working text while browsing (last = input line)
  // so history can be edited in place.
  // empty when not browsing; dropped on submit.
  std::vector<std::string> work;
};

// edit buffer with recallable history
struct command_line_t {
  edit_buf_t buf;
  history_t hist;
};

bool valid (const edit_buf_t& b);
void insert (edit_buf_t& b, char c);
void del_back (edit_buf_t& b);
void del_fwd (edit_buf_t& b);
void del_to_start (edit_buf_t& b);
void del_to_end (edit_buf_t& b);
void clear (edit_buf_t& b);
void move_start (edit_buf_t& b);
void move_end (edit_buf_t& b);
void move_left (edit_buf_t& b);
void move_right (edit_buf_t& b);
void move_word_left (edit_buf_t& b);
void move_word_right (edit_buf_t& b);
void del_word_left (edit_buf_t& b);
void del_word_right (edit_buf_t& b);
void set_text (edit_buf_t& b, std::string_view s);

// history_* save edits made to commands in history
void history_prev (command_line_t& c);
void history_next (command_line_t& c);

// records buffer to history, clears buf and resets history browsing.
std::string reset (command_line_t& c);

}  // namespace input
