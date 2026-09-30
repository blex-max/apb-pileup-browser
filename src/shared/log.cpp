#include "log.hpp"

#include <ctime>
#include <fstream>
#include <utility>

#include "shared/cleanup.hpp"

namespace apb_log {

static bool logInit = false;
static std::ofstream logOut;
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
  log_push (unfmtTime.substr (0, unfmtTime.length() - 1));
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

LogStatusCode log_push (const std::string_view msg, bool writeSep)
{
  if (!logInit) {
    return LogStatusCode::uninit;
  }
  if (msg.length() + 1 > logSzCh) {
    return LogStatusCode::msgTooLarge;
  }
  bool flushed = false;
  if (msg.length() + 1 /* +1 sep */ > (logSzCh - *curIdx)) {
    swap();
    flushed = true;
  }
  std::memcpy (&curBuf[*curIdx], msg.data(), msg.length());
  *curIdx += msg.length();
  if (writeSep) {
    curBuf[*curIdx] = sepCh;
    (*curIdx)++;
  }
  return (flushed) ? LogStatusCode::flushed : LogStatusCode::none;
};

void dump()
{
  /*
    dump logs to disk. For use in the event of a crash.
    Assumes preBuf has already been writted out if logging
    to file.
  */
  Defer switch_deinit ([]() { logInit = false; });
  if (logOut.is_open()) {
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

    dumpOut.write (crashHdr.data(), crashHdr.size());
    dumpOut.write (timeStr.c_str(), timeStr.size());
    dumpOut.write (preBuf, *preIdx);
    dumpOut.write (curBuf, *curIdx);
    dumpOut.flush();
    dumpOut.close();
  }
}

void deinit()
{
  if (logInit && logOut.is_open()) {
    logOut.write (curBuf, *curIdx);
    logOut.flush();
    logOut.close();
  }
}

}  // namespace apb_log
