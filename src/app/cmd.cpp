#include "cmd.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <plog/Log.h>

#include <cstdint>
#include <expected>
#include <fstream>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "app/data_table_cols.hpp"
#include "app/state.hpp"
#include "app/text_blocks.hpp"
#include "app/widgets.hpp"
#include "backend/PileupDB.hpp"


// --- HELPERS --- //

#define CMD_GENERIC_SUCCESS "OK!"

static std::string cmd_format_misuse (
    std::string_view misuseMsg, std::string_view cmdUsage
)
{
  return fmt::format (
      "Bad call - {}. Usage: {}", misuseMsg, cmdUsage
  );
}
static std::string cmd_format_fail (std::string_view failMsg)
{
  return fmt::format ("Failed - {}", failMsg);
}


static std::pair<std::string_view, std::string_view>
split_first_space (std::string_view s)
{
  if (s.empty()) {
    return {};
  }
  auto pos = s.find (' ');
  if (pos == std::string_view::npos) {
    return {s, {}};  // no args
  }
  return {s.substr (0, pos), s.substr (pos + 1)};
}

static std::vector<std::string_view> split_whitespace (
    std::string_view s
)
{
  std::vector<std::string_view> out;
  auto [f, rest] = split_first_space (s);
  while (!f.empty()) {
    out.push_back (f);
    const auto tmp = split_first_space (rest);
    f = tmp.first;
    rest = tmp.second;
  }
  return out;
}


static std::expected<void, CmdResult> cmd_validate_args_empty (
    const std::string_view args, const std::string_view cmdUsage
)
{
  if (!args.empty()) {
    return std::unexpected<CmdResult> (
        {false, cmd_format_misuse ("expected no args", cmdUsage)}
    );
  }
  return {};
}
static std::expected<void, CmdResult> cmd_validate_ntok (
    const std::span<const std::string_view> argTok,
    uint8_t minNTok, uint8_t maxNTok,
    const std::string_view usage
)
{
  assert (maxNTok >= minNTok);
  assert (!usage.empty());

  if (maxNTok == 0 && !argTok.empty()) {
    return std::unexpected<CmdResult> (
        {false, cmd_format_misuse ("too many args", usage)}
    );
  }

  if (argTok.size() > maxNTok) {
    return std::unexpected<CmdResult> (
        {false, cmd_format_misuse ("too many args", usage)}
    );
  }
  if (argTok.size() < minNTok) {
    return std::unexpected<CmdResult> (
        {false, cmd_format_misuse ("too few args", usage)}
    );
  }

  return {};
}

// --- COMMANDS --- //

// TODO: commentary on implementation

// view type to hold in registry
struct CmdView {
  std::string_view call;
  std::span<const std::string_view> alias;  // empty if none
  CmdResult (*exec) (std::string_view, AppState&);
  std::string_view usage;
  std::string_view desc;
};

struct QuitCmd {
  constexpr static std::string_view call{"quit"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "q"
  };
  constexpr static std::string_view usage{call};
  constexpr static std::string_view desc{"Exit the browser."};

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (const auto ret = cmd_validate_args_empty (args, usage);
        !ret) {
      return ret.error();
    }
    state.conf.run = false;
    return {true, "Bye!"};
  }

  constexpr static CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct ShowReadFieldsCmd {
  constexpr static std::string_view call{"field"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "f"
  };
  inline static const std::string usage =
      fmt::format ("{} <field-name>...", call);
  constexpr static std::string_view desc{
      "Toggle display of read data fields to the tabular display"
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    const auto tokens = split_whitespace (args);
    const auto nargRet =
        cmd_validate_ntok (tokens, 0, UINT8_MAX, usage);
    if (!nargRet) {
      return nargRet.error();
    }

    // check dups
    std::unordered_set<std::string_view> seen;
    std::unordered_set<std::string_view> dups;
    for (const auto& tok : tokens) {
      if (!seen.insert (tok).second) {
        dups.insert (tok);
      }
    }
    if (!dups.empty()) {
      return {
          false,
          cmd_format_misuse (
              fmt::format (
                  "duplicated tokens {}", fmt::join (dups, ", ")
              ),
              usage
          )
      };
    }

    auto& existingRequests = state.conf.displayTableCols;
    std::vector<std::string> nowVisible;
    std::vector<std::string> nowHidden;
    for (const auto& tok : tokens) {
      const auto getRet = TableCol::get_id_by_name (tok);
      if (!getRet) {
        return {
            false, cmd_format_fail (
                       fmt::format ("unknown field {}", tok)
                   )
        };
      }
      const auto id = *getRet;
      if (std::find (
              begin (existingRequests), end (existingRequests),
              id
          ) != end (existingRequests)) {
        // remove
        std::erase (existingRequests, id);
        nowHidden.emplace_back (tok);
      }
      else {
        // add
        existingRequests.push_back (id);
        nowVisible.emplace_back (tok);
      }
    }

    std::string outMsg;
    if (!nowVisible.empty()) {
      outMsg += fmt::format (
          "Showing: {}", fmt::join (nowVisible, ", ")
      );
    }
    if (!nowHidden.empty()) {
      if (!outMsg.empty()) {
        outMsg += "|";
      }
      outMsg += fmt::format (
          "Hiding: {}", fmt::join (nowHidden, ", ")
      );
    }

    return {true, outMsg};
  };

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

static const std::unordered_set<std::string_view>
    sh_validConjunctions{"AND", "and", "OR", "or"};

static std::string stringify_where (
    const std::vector<std::string>& where
)
{
  std::string out;
  for (size_t i = 0; i < where.size(); ++i) {
    out.append (where[i]);
    if (i != (where.size() - 1)) {
      out.append (" ");
    }
  }
  return out;
}

static CmdResult try_apply_query_clause (
    AppState& state, DynamicFragments newClause,
    std::string_view successMsg
)
{
  auto prepRet = prepare_select_reads (state.db.db, newClause);
  if (!prepRet) {
    return {false, cmd_format_fail (prepRet.error().msg)};
  }
  auto newStmt = std::move (*prepRet);
  uint32_t nRow = 0;
  for (;; ++nRow) {
    const auto nrRet = next_read (newStmt, state.db.db);
    if (!nrRet) {
      // poor error handling policy
      return {false, cmd_format_fail (prepRet.error().msg)};
    }
    if (!(*nrRet)) {
      break;  // reads exhausted
    }
  }
  state.db.stmt = std::move (newStmt);
  state.db.userClause = std::move (newClause);
  state.db.stmtRowScrollOffset = 0;  // reset row view
  state.db.nStmtRows = nRow;
  return {true, std::string (successMsg)};
}

struct WhereCmd {
  constexpr static std::string_view call{"where"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "wh"
  };
  inline static const std::string usage =
      fmt::format ("{} <clause>", call);
  constexpr static std::string_view desc{
      "Start a new WHERE clause, overwriting any existing "
      "clause."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    // copy in case sql compile fails
    auto newClause = state.db.userClause;
    newClause.where.clear();
    newClause.where.emplace_back (args);

    PLOGD << fmt::format (
        "Attempting to compile statement with updated WHERE "
        "clause {}",
        args
    );

    return try_apply_query_clause (
        state, std::move (newClause), CMD_GENERIC_SUCCESS
    );
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct AndCmd {
  constexpr static std::string_view call{"and"};
  constexpr static std::array<std::string_view, 0> callAlias{};
  inline static const std::string usage =
      fmt::format ("{} <clause>", call);
  constexpr static std::string_view desc{
      "Extend current WHERE clause with an AND condition."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (args.empty()) {
      return {
          false, cmd_format_misuse ("no condition given", usage)
      };
    }

    if (state.db.userClause.where.empty()) {
      return {
          false,
          cmd_format_fail ("WHERE clause empty; cannot add term")
      };
    }

    auto newClause = state.db.userClause;
    std::string newCond{"AND "};
    newCond.append (args);

    newClause.where.emplace_back (newCond);

    PLOGD << fmt::format (
        "Attempting to compile statement with updated WHERE "
        "clause {}",
        args
    );

    // validates clause
    return try_apply_query_clause (
        state, std::move (newClause), CMD_GENERIC_SUCCESS
    );
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct OrCmd {
  constexpr static std::string_view call{"or"};
  constexpr static std::array<std::string_view, 0> callAlias{};
  inline static const std::string usage =
      fmt::format ("{} <clause>", call);
  constexpr static std::string_view desc{
      "Extend the query with an OR condition."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (args.empty()) {
      return {
          false, cmd_format_misuse ("no condition given", usage)
      };
    }

    if (state.db.userClause.where.empty()) {
      return {false, "WHERE clause empty; cannot add term"};
    }
    auto newClause = state.db.userClause;

    std::string newCond{"OR "};
    newCond.append (args);

    newClause.where.emplace_back (newCond);

    PLOGD << fmt::format (
        "Attempting to compile statement with updated WHERE "
        "clause {}",
        args
    );

    return try_apply_query_clause (
        state, std::move (newClause), CMD_GENERIC_SUCCESS
    );
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct BackCmd {
  constexpr static std::string_view call{"back"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "bk"
  };
  constexpr static std::string_view usage{call};
  constexpr static std::string_view desc{
      "Drop the most recently added condition from the query "
      "WHERE clause, or clear if only a single condition is "
      "present"
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (const auto res = cmd_validate_args_empty (args, usage);
        !res) {
      return res.error();
    };

    auto& curClause = state.db.userClause;
    if (curClause.where.empty()) {
      return {false, cmd_format_fail ("WHERE clause empty")};
    }
    auto newClause = curClause;
    auto rmClauseElem = newClause.where.back();
    newClause.where.pop_back();

    return try_apply_query_clause (
        state, std::move (newClause),
        fmt::format ("Removed clause: {}", rmClauseElem)
    );
  }

  constexpr static CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct ClearWhereCmd {
  constexpr static std::string_view call{"clear-where"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "cw"
  };
  constexpr static std::string_view usage{call};
  constexpr static std::string_view desc{
      "clear WHERE clause, retaining ORDER BY"
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (const auto res = cmd_validate_args_empty (args, usage);
        !res) {
      return res.error();
    };

    auto newClause = state.db.userClause;
    newClause.where.clear();

    return try_apply_query_clause (
        state, std::move (newClause), "Cleared WHERE clause"
    );
  }

  constexpr static CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct OrderCmd {
  constexpr static std::string_view call{"order-by"};
  constexpr static std::array<std::string_view, 2> callAlias{
      "order", "ob"
  };
  inline static const std::string usage =
      fmt::format ("{} <clause>", call);
  constexpr static std::string_view desc{
      "Sort reads by ORDER BY expression."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (args.empty()) {
      return {
          false, cmd_format_misuse ("no clause given", usage)
      };
    }

    PLOGD << fmt::format ("User requesting sort: {}", args);

    auto newClause = state.db.userClause;
    newClause.orderBy = args;

    return try_apply_query_clause (
        state, std::move (newClause), CMD_GENERIC_SUCCESS
    );
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct CountCmd {
  constexpr static std::string_view call{"count"};
  constexpr static std::array<std::string_view, 1> alias{"ct"};
  inline static const std::string usage =
      fmt::format ("{} [clause]", call);
  constexpr static std::string_view desc{
      "Count reads matching current query. If provided, the "
      "optional clause will be AND-concatenated onto the "
      "existing WHERE clause for the count query. If no WHERE "
      "clause is present, the optional clause will be used as "
      "the count WHERE clause alone."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    auto where = state.db.userClause.where;
    if (!args.empty()) {
      if (where.empty()) {
        where.emplace_back (args);
      }
      else {
        std::string clause{"AND "};
        clause.append (args);
        where.emplace_back (clause);
      }
    }

    auto stmtRet = prepare_count_reads (state.db.db, where);
    if (!stmtRet) {
      return {false, stmtRet.error().msg};
    }

    auto& stmt = *stmtRet;
    if (const int rc = sqlite3_step (stmt); rc != SQLITE_ROW) {
      return {
          false, fmt::format (
                     "Could not execute count: {}",
                     sqlite3_errmsg (state.db.db)
                 )
      };
    }

    return {
        true, fmt::format (
                  "{}: {} reads", stringify_where (where),
                  sqlite3_column_int64 (stmt, 0)
              )
    };
  }

  inline static const CmdView view{
      call, alias, &operator(), usage, desc
  };
};

struct ClearCmd {
  constexpr static std::string_view call{"clear"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "cl"
  };
  constexpr static std::string_view usage{call};
  constexpr static std::string_view desc{"Clear current query."};

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    if (const auto res = cmd_validate_args_empty (args, usage);
        !res) {
      return res.error();
    };

    auto newClause = state.db.userClause;
    newClause.where.clear();
    newClause.orderBy.clear();

    return try_apply_query_clause (
        state, std::move (newClause), "Reset query"
    );
  }

  constexpr static CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct ShowPaneCmd {
  enum Pane : uint8_t { aln, table, COUNT };
  constexpr static std::array<std::string_view, Pane::COUNT>
      paneNames{{[Pane::aln] = "aln", [Pane::table] = "table"}};
  constexpr static std::array<double, Pane::COUNT> kFoldedFracs{
      {[Pane::aln] = 0.01, [Pane::table] = 0.99}
  };
  constexpr static double kDefaultFrac = 0.5;

  constexpr static std::string_view call{"pane"};
  constexpr static std::array<std::string_view, 0> callAlias{};
  inline static const std::string usage =
      fmt::format ("{} [{}]", call, fmt::join (paneNames, "|"));
  constexpr static std::string_view desc{
      "show/hide either of the alignment or table panes, or "
      "reset "
      "to default with no args"
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    // janky, but
    // not worth fretting over until vcf pane implementation is in
    // TODO: it would be much better if this simply set
    // config flags that were handled elsewhere
    auto& qbf = state.conf.seqPaneFrac;
    const auto tokens = split_whitespace (args);

    if (tokens.empty()) {
      if (qbf == kDefaultFrac) {
        return {
            false,
            "View already at default, specify arg (seq, data) "
            "to change"
        };
      }
      qbf = kDefaultFrac;
      size_browser_panes (state.ui.browsr, qbf);
      return {true, "Reset view to default"};
    }

    if (tokens.size() > 1) {
      return {
          false,
          cmd_format_misuse ("specify a single pane only", usage)
      };
    }

    Pane chosen;
    if (tokens[0] == paneNames[Pane::aln]) {
      chosen = Pane::aln;
    }
    else if (tokens[0] == paneNames[Pane::table]) {
      chosen = Pane::table;
    }
    else {
      return {
          false,
          cmd_format_misuse (
              fmt::format ("unknown pane {}", tokens[0]), usage
          )
      };
    }

    const std::string_view paneWord =
        (chosen == Pane::aln) ? "alignment" : "table";
    const double foldedFrac = kFoldedFracs[chosen];

    std::string msg;
    if (qbf == foldedFrac) {
      qbf = kDefaultFrac;
      msg = fmt::format ("Unfolded {} pane", paneWord);
    }
    else {
      qbf = foldedFrac;
      msg = fmt::format ("Folded {} pane", paneWord);
    }

    size_browser_panes (state.ui.browsr, qbf);
    return {true, msg};
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct ShowTrackCmd {
  enum Track : uint8_t { qual, ins, insQual, COUNT };
  // TODO: shorthands?
  constexpr static std::array<std::string_view, Track::COUNT>
      trackNames{
          {[Track::qual] = "qual",
           [Track::ins] = "ins",
           [Track::insQual] = "ins-qual"}
      };
  constexpr static std::array<std::string_view, Track::COUNT>
      trackFullNames{
          {[Track::qual] = "quality",
           [Track::ins] = "insertion",
           [Track::insQual] = "insertion quality"}
      };

  constexpr static std::string_view call{"track"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "tr"
  };
  inline static const std::string usage = fmt::format (
      "{} [({})...] - nargs: 0 - {}", call,
      fmt::join (trackNames, "|"), trackNames.size()
  );
  constexpr static std::string_view desc{
      "toggle display of additional tracks in browser alignment "
      "pane, or reset to default with no args"
  };


  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    auto& conf = state.conf;

    if (args.empty()) {
      // reset
      conf.drawQualTrack = false;
      conf.drawInsTrack = true;
      conf.drawInsQualTrack = false;
      return {
          true, fmt::format (
                    "{} Reset track display to default",
                    CMD_GENERIC_SUCCESS
                )
      };
    }
    const auto tokens = split_whitespace (args);

    if (const auto expectedNTok =
            cmd_validate_ntok (tokens, 1, Track::COUNT, usage);
        !expectedNTok) {
      return expectedNTok.error();
    }

    std::vector<Track> tracksToToggle;
    for (const auto& tok : tokens) {
      // validate tokens
      bool matchFound = false;
      for (uint8_t id = 0;
           id < static_cast<uint8_t> (Track::COUNT); ++id) {
        if (trackNames[id] == tok) {
          const auto trackId = static_cast<Track> (id);
          if (std::ranges::contains (tracksToToggle, trackId)) {
            return {
                false, cmd_format_misuse (
                           fmt::format (
                               "{} specified more than once", tok
                           ),
                           usage
                       )
            };
          }
          tracksToToggle.push_back (trackId);
          matchFound = true;
        }
      }
      if (!matchFound) {
        return {
            false,
            cmd_format_misuse (
                fmt::format ("unknown pane {}", tok), usage
            )
        };
      }
    }

    std::vector<std::string_view> nowShown;
    std::vector<std::string_view> nowHidden;
    for (const auto& id : tracksToToggle) {
      switch (id) {
        case Track::qual:
          conf.drawQualTrack = !conf.drawQualTrack;
          (conf.drawQualTrack ? nowShown : nowHidden)
              .push_back (trackFullNames[id]);
          break;
        case Track::ins:
          conf.drawInsTrack = !conf.drawInsTrack;
          (conf.drawInsTrack ? nowShown : nowHidden)
              .push_back (trackFullNames[id]);
          break;
        case Track::insQual:
          conf.drawInsQualTrack = !conf.drawInsQualTrack;
          (conf.drawInsQualTrack ? nowShown : nowHidden)
              .push_back (trackFullNames[id]);
          break;
        case Track::COUNT:
          assert (false && "COUNT is not a real value");
          std::unreachable();
      }
    }

    std::string outMsg;
    if (!nowShown.empty()) {
      outMsg += fmt::format (
          "Showing: {}", fmt::join (nowShown, ", ")
      );
    }
    if (!nowHidden.empty()) {
      if (!outMsg.empty()) {
        outMsg += "|";
      }
      outMsg += fmt::format (
          "Hiding: {}", fmt::join (nowHidden, ", ")
      );
    }

    return {true, outMsg};
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};


struct DumpCmd {
  constexpr static std::string_view call{"dump"};
  constexpr static std::array<std::string_view, 0> callAlias{};
  inline static const std::string usage =
      fmt::format ("{} <path>", call);
  constexpr static std::string_view desc{
      "Write the in-memory database to a file. Takes a single "
      "path. The current query is not preserved."
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    const auto tokens = split_whitespace (args);
    if (const auto expectedNTok =
            cmd_validate_ntok (tokens, 1, 1, usage);
        !expectedNTok) {
      return expectedNTok.error();
    }

    const std::string path{tokens[0]};
    auto dumpRet = dump_to_disk (state.db.db, path);
    if (!dumpRet) {
      return {false, dumpRet.error().msg};
    }

    return {true, fmt::format ("Dumped database to {}", path)};
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct DumpReadmeCmd {
  constexpr static std::string_view call{"readme"};
  constexpr static std::array<std::string_view, 0> callAlias{};
  inline static const std::string usage =
      fmt::format ("{} [directory-path]", call);
  constexpr static std::string_view desc{
      "Dumps readme shipped with repo to a provided directory, "
      "or "
      "the working directory if no path is given.",
  };

  static CmdResult operator() (std::string_view args, AppState&)
  {
    static constexpr std::string sh_readmeFilename =
        "APB-README.md";
    const auto tokens = split_whitespace (args);
    if (const auto expectedNTok =
            cmd_validate_ntok (tokens, 0, 1, usage);
        !expectedNTok) {
      return expectedNTok.error();
    }

    std::string outPath;
    if (tokens.empty()) {
      outPath = "./";
      outPath += sh_readmeFilename;
    }
    else {
      outPath = tokens[0];
      outPath += "/";
      outPath += sh_readmeFilename;
    }

    std::ofstream outStream{outPath, std::ios::binary};
    if (!outStream) {
      return {
          false,
          cmd_format_fail (
              fmt::format (
                  "could not open {}; failed to dump readme",
                  outPath
              )
          )
      };
    }
    auto readme = get_readme();
    outStream.write (
        readme.data(), static_cast<int> (readme.size())
    );
    if (!outStream) {
      return {
          false,
          cmd_format_fail (
              fmt::format (
                  "failed during write readme at {}", outPath
              )
          )
      };
    }
    return {true, fmt::format ("readme written to {}", outPath)};
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct HelpCmd {
  constexpr static std::string_view call{"help"};
  constexpr static std::array<std::string_view, 2> alias{
      "h", "?"
  };

  enum Topic : uint8_t { nav, cmd, COUNT };
  constexpr static std::array<std::string_view, Topic::COUNT>
      topicNames{{[Topic::nav] = "nav", [Topic::cmd] = "cmd"}};

  inline static const std::string usage = fmt::format (
      "{} [({})]", call, fmt::join (topicNames, "|")
  );
  constexpr static std::string_view desc{
      "Show help for given topic, or general help with no args"
  };

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    const auto tokens = split_whitespace (args);
    const auto nargRet = cmd_validate_ntok (tokens, 0, 1, usage);
    if (!nargRet) {
      return nargRet.error();
    }

    CmdResult out;
    if (tokens.empty()) {
      state.conf.showOverlay = true;
      set_overlay_widget (
          state.ui, get_text_block (TxtBlockId::generalHelp)
      );
      out.success = true;
    }
    else if (std::ranges::contains (topicNames, tokens[0])) {
      const auto topic = tokens[0];
      if (topic == topicNames[Topic::nav]) {
        // TODO: make nav keys into little
        // metadata objects similar to cmd and construct
        // help from metadata
        state.conf.showOverlay = true;
        set_overlay_widget (
            state.ui, get_text_block (TxtBlockId::navHelp)
        );
        out.success = true;
      }
      else if (topic == topicNames[Topic::cmd]) {
        // TODO: construct cmd reference table
        // from CmdView objects directly
        state.conf.showOverlay = true;
        set_overlay_widget (
            state.ui, get_text_block (TxtBlockId::cmdRef)
        );
        out.success = true;
      }
    }
    else {
      out.msg = cmd_format_misuse (
          fmt::format ("unknown topic {}", tokens[0]), usage
      );
      out.success = false;
    }
    return out;
  }

  inline static const CmdView view{
      call, alias, &operator(), usage, desc
  };
};

// TODO: add query saving cmd/functionality (?)

// TODO: add compile time assertion that no
// aliases overlap
static constexpr const CmdView* cmdRegistry_SH[]{
    &HelpCmd::view,
    &QuitCmd::view,
    &WhereCmd::view,
    &AndCmd::view,
    &OrCmd::view,
    &BackCmd::view,
    &ClearWhereCmd::view,
    &OrderCmd::view,
    &ClearCmd::view,
    &DumpCmd::view,
    &ShowPaneCmd::view,
    &ShowTrackCmd::view,
    &ShowReadFieldsCmd::view,
    &CountCmd::view,
    &DumpReadmeCmd::view,
};

static const CmdView* find_cmd (std::string_view name)
{
  for (const auto* br_cmd : cmdRegistry_SH) {
    if (br_cmd->call == name ||
        (!br_cmd->alias.empty() &&
         std::ranges::contains (br_cmd->alias, name))) {
      return br_cmd;
    }
  }
  return nullptr;
}

CmdResult exec_cmd (std::string_view call, AppState& state)
{
  auto [name, args] = split_first_space (call);
  if (const auto* br_cmd = find_cmd (name)) {
    return br_cmd->exec (args, state);
  }
  return {
      false, fmt::format ("Command \"{}\" not found!", name)
  };
}
