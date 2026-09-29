#include "log.hpp"

#include <ctime>
#include <fstream>

namespace log {

static bool toFile = false;
static bool logInit = false;
static std::ofstream logOut;
static constexpr uint16_t logSzCh = 10000;
static char logBuf[logSzCh];
static uint16_t logIdx = 0;
static char sepCh;

void init (const std::optional<std::string>& path, char sep)
{
  if (path) {
    logOut.open (path.value().c_str(), std::ios::app);
    toFile = true;
  }
  sepCh = sep;
  logInit = true;
  time_t rawTime;
  time (&rawTime);
  std::string unfmtTime{ctime (&rawTime)};
  // remove newline added by ctime
  push (unfmtTime.substr (0, unfmtTime.length() - 1));
}

LogStatusCode push (const std::string_view msg)
{
  if (!logInit) {
    return LogStatusCode::uninit;
  }
  if (msg.length() + 1 > logSzCh) {
    return LogStatusCode::msgTooLarge;
  }
  bool flushed = false;
  if (msg.length() + 1 > (logSzCh - logIdx)) {
    flush();
    flushed = true;
  }
  std::memcpy (&logBuf[logIdx], msg.data(), msg.length());
  logIdx += msg.length();
  logBuf[logIdx] = sepCh;
  logIdx++;
  return (flushed) ? LogStatusCode::flushed : LogStatusCode::none;
};

void flush()
{
  if (toFile) {
    logOut.write (logBuf, logIdx);
  }
  logIdx = 0;
}

void deinit()
{
  logOut.flush();
  logOut.close();
}

}  // namespace log
