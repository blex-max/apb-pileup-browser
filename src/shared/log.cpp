#include "log.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>


namespace apb_log {

static bool logInit = false;
static std::ofstream logOut;
// assuming a sentence is on average roughly 100 characters
// `logSzCh` of 5000 means 50 messages per buffer.
static constexpr uint16_t logSzCh = 5000;
static char logBuf1[logSzCh];
static char logBuf2[logSzCh];
static uint16_t logIdx1 = 0;
static uint16_t logIdx2 = 0;
static auto* curBuf = logBuf1;
static auto* preBuf = logBuf2;
static auto* curIdx = &logIdx1;
static auto* preIdx = &logIdx2;
static char sepCh;

void init (const std::optional<std::string>& logPath, char sep)
{
  if (logPath) {
    logOut.open (logPath.value().c_str(), std::ios::app);
  }
  sepCh = sep;
  logInit = true;
  time_t rawTime;
  time (&rawTime);
  std::string unfmtTime{ctime (&rawTime)};
  // remove newline added by ctime
  log_push (unfmtTime.substr (0, unfmtTime.length() - 1), false);
  log_push (": apb log");
}

static void swap()
{
  /*
    swap log buffers, preserving older logs in memory in case
    they are later dumped. If flushing to file, strictly
    unnecessary. But keeping logs in memory allows
    reliably dumping a crash report if NOT logging to file.
  */
  std::swap (curBuf, preBuf);
  std::swap (curIdx, preIdx);
  *curIdx = 0;

  if (logOut.is_open()) {
    logOut.write (preBuf, *preIdx);
  }
}

bool log_push (const std::string_view msg, bool writeSep)
{
  if (!logInit) {
    return false;
  }
  if (msg.length() + 1 > logSzCh) {
    return false;
  }

  if (msg.length() + 1 /* +1 sep */ > (logSzCh - *curIdx)) {
    swap();
  }
  std::memcpy (&curBuf[*curIdx], msg.data(), msg.length());
  *curIdx += msg.length();
  if (writeSep) {
    curBuf[*curIdx] = sepCh;
    (*curIdx)++;
  }
  return true;
};

void crash_dump()
{
  /* Assumes preBuf has already been writted out if logging to file */

  if (!logInit) {
    return;
  }
  if (logOut.is_open()) {
    std::cerr << fmt::format (
        "Attempting to dump crash log to log output"
    );
    logOut.write (curBuf, *curIdx);
    logOut.flush();
    logOut.close();
  }
  else {
    if (*preIdx == 0 && *curIdx == 0) {
      return;
    }
    std::ofstream dumpOut;
    dumpOut.open (crashLogPath, std::ios::app);
    static constexpr std::string_view crashHdr =
        "-- apb crash report --\nLog contents near crash occurence.\n";
    time_t rawTime;
    time (&rawTime);
    std::string timeStr{ctime (&rawTime)};

    std::cerr << fmt::format (
        "Attempting to dump crash log to {}", crashLogPath
    );
    dumpOut.write (crashHdr.data(), crashHdr.size());
    dumpOut.write (timeStr.c_str(), timeStr.size());
    dumpOut.write (preBuf, *preIdx);
    dumpOut.write (curBuf, *curIdx);
    dumpOut.flush();
    dumpOut.close();
  }
  logInit = false;
}

void deinit()
{
  if (logInit && logOut.is_open()) {
    logOut.write (curBuf, *curIdx);
    logOut.flush();
    logOut.close();
  }
  logInit = false;
}

}  // namespace apb_log
