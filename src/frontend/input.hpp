#pragma once

#include <string>

struct EditBuf {
  std::string text;
  size_t curs = 0;
};

// TODO: consider error strategy,
// if any really needed
bool valid (const EditBuf& b);
void insert (EditBuf& b, char c);
void del_back (EditBuf& b);
void clear (EditBuf& b);
void move_start (EditBuf& b);
void move_end (EditBuf& b);
void move_left (EditBuf& b);
void move_right (EditBuf& b);
void move_word_left (EditBuf& b);
void move_word_right (EditBuf& b);
void set_text (EditBuf& b, std::string s);
