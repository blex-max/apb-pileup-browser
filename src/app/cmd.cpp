#include "cmd.hpp"

#include <fmt/compile.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <plog/Log.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <unordered_set>
#include <utility>

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
      "Misuse - {}. Usage: {}", misuseMsg, cmdUsage
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
// NOTE: usage text roughly follows docopt

struct QuitCmd {
  constexpr static std::string_view call{"quit"};
  constexpr static std::array<std::string_view, 2> callAlias{
      "q", "exit"
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

struct ShowTableColCmd {
  constexpr static std::string_view call{"col"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "c"
  };
  inline static const std::string usage =
      fmt::format ("{} <field-name>...", call);
  constexpr static std::string_view desc{
      "Show/hide read data columns in the table pane."
      "For a list of available columns, check the "
      "manual or run `? table`."
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
                  "duplicated arg/s {}", fmt::join (dups, ", ")
              ),
              usage
          )
      };
    }

    auto& tableCols = state.conf.displayTableCols;
    std::vector<std::string> nowVisible;
    std::vector<std::string> nowHidden;
    for (const auto& tok : tokens) {
      bool tokMatch = false;
      for (auto& col : tableCols) {
        if (tok == col.fieldName) {
          tokMatch = true;
          if (col.visible) {
            nowHidden.emplace_back (tok);
          }
          else {
            nowVisible.emplace_back (tok);
          }
          col.visible = !col.visible;
          continue;
        }
      }
      if (!tokMatch) {
        return {
            false, cmd_format_fail (
                       fmt::format ("unknown field {}", tok)
                   )
        };
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
      "clause. For a reference of queryable columns, check "
      "the manual or run `? table`."
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
      call, {}, &operator(), usage, desc
  };
};

struct OrCmd {
  constexpr static std::string_view call{"or"};
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
      call, {}, &operator(), usage, desc
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
      "present."
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
      "Clear WHERE clause, retaining ORDER BY."
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
  constexpr static std::array<std::string_view, 1> callAlias{
      "ct"
  };
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
      call, callAlias, &operator(), usage, desc
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

// NOTE: kept for now for future reference
// struct ShowPaneCmd {
//   enum Pane : uint8_t { aln, table, COUNT };
//   constexpr static std::array<std::string_view, Pane::COUNT>
//       paneNames{{[Pane::aln] = "aln", [Pane::table] = "table"}};
//   constexpr static std::array<std::string_view, Pane::COUNT>
//       paneFullNames{
//           {[Pane::aln] = "alignment", [Pane::table] = "table"}
//       };

//   constexpr static std::string_view call{"pane"};
//   constexpr static std::array<std::string_view, 1> callAlias{
//       "p"
//   };
//   inline static const std::string usage =
//       fmt::format ("{} [{}]", call, fmt::join (paneNames, "|"));
//   constexpr static std::string_view desc{
//       "Show/hide either of the alignment or table panes, or "
//       "reset to default with no args."
//   };

//   static CmdResult operator() (
//       std::string_view args, AppState& state
//   )
//   {
//     auto& switches = state.conf.drawPaneSwitches;
//     const auto tokens = split_whitespace (args);

//     if (tokens.size() > 1) {
//       return {
//           false,
//           cmd_format_misuse ("specify a single pane only", usage)
//       };
//     }

//     std::string msg;
//     if (tokens.empty()) {
//       switches.table = true;
//       msg = "Reset view to default";
//     }
//     else if (tokens[0] == paneNames[Pane::aln]) {
//       switches.aln = !switches.aln;
//       if (!switches.aln && !switches.table) {
//         switches.table = true;
//       }
//       msg = fmt::format (
//           "{} {} pane", (switches.aln) ? "Unfolded" : "Folded",
//           paneFullNames[Pane::aln]
//       );
//     }
//     else if (tokens[0] == paneNames[Pane::table]) {
//       switches.table = !switches.table;
//       if (!switches.table && !switches.aln) {
//         switches.aln = true;
//       }
//       msg = fmt::format (
//           "{} {} pane", (switches.table) ? "Unfolded" : "Folded",
//           paneFullNames[Pane::table]
//       );
//     }
//     else {
//       return {
//           false,
//           cmd_format_misuse (
//               fmt::format ("unknown pane {}", tokens[0]), usage
//           )
//       };
//     }

//     return {true, msg};
//   }

//   inline static const CmdView view{
//       call, callAlias, &operator(), usage, desc
//   };
// };

struct ShowTableCmd {
  constexpr static std::string_view call{"show-table"};
  constexpr static std::array<std::string_view, 1> callAlias{
      "st"
  };
  constexpr static std::string_view usage = call;
  constexpr static std::string_view desc{"Show/hide table pane"};

  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    const auto valRet = cmd_validate_args_empty (args, usage);
    if (!valRet) {
      return valRet.error();
    }

    state.conf.drawPaneSwitches.table =
        !state.conf.drawPaneSwitches.table;
    return {
        true,
        fmt::format (
            "{} table pane", (state.conf.drawPaneSwitches.table)
                                 ? "Unfolded"
                                 : "Folded"
        )
    };
  }

  inline static const CmdView view{
      call, callAlias, &operator(), usage, desc
  };
};

struct ShowTrackCmd {
  enum TrackID : uint8_t { qual, ins };
  constexpr static std::array<std::string_view, 2> trackNames{
      {[qual] = "quality", [ins] = "insertion"}
  };
  static constexpr std::string_view track_by_name (
      std::string_view name
  ) noexcept
  {
    // NOTE: this works because the track names
    // are entirely unambiguous from the first character
    for (const auto& trackName : trackNames) {
      if (name == trackName.substr (0, name.length())) {
        return trackName;
      }
    }
    return {};
  }

  constexpr static std::string_view call{"track"};
  constexpr static std::array<std::string_view, 2> callAlias{
      "t", "tr"
  };
  inline static const std::string usage = fmt::format (
      "{} [({})...]", call, fmt::join (trackNames, "|")
  );
  constexpr static std::string_view desc{
      "Show/hide insertion and quality score tracks in "
      "alignment pane, or reset to default with no args. "
      "Any unambiguous substring of the track name may"
      "be used, e.g. `track qual`."
  };


  static CmdResult operator() (
      std::string_view args, AppState& state
  )
  {
    auto& switches = state.conf.drawTrackSwitches;

    if (args.empty()) {
      // reset
      switches.qual = false;
      switches.ins = true;
      return {
          true, fmt::format (
                    "{} Reset track display to default",
                    CMD_GENERIC_SUCCESS
                )
      };
    }
    const auto tokens = split_whitespace (args);

    if (const auto expectedNTok = cmd_validate_ntok (
            tokens, 1, trackNames.size(), usage
        );
        !expectedNTok) {
      return expectedNTok.error();
    }

    std::unordered_set<std::string_view> seen;
    for (const auto& tok : tokens) {
      if (!seen.insert (tok).second) {
        return {
            false,
            cmd_format_misuse (
                fmt::format ("duplicated token \"{}\"", tok),
                usage
            )
        };
      }
    }

    // verify tokens are legtimate track names
    std::vector<std::string_view> tracksToToggle;
    for (const auto& tok : tokens) {
      tracksToToggle.emplace_back (track_by_name (tok));
      if (tracksToToggle.back() == "") {
        return {
            false,
            cmd_format_misuse (
                fmt::format ("unknown track \"{}\"", tok), usage
            )
        };
      }
    }

    // switch drawing behaviour
    std::vector<std::string_view> nowShown;
    std::vector<std::string_view> nowHidden;
    for (const auto& track : tracksToToggle) {
      if (track == trackNames[TrackID::qual]) {
        switches.qual = !switches.qual;
        (switches.qual ? nowShown : nowHidden).push_back (track);
      }
      else if (track == trackNames[TrackID::ins]) {
        switches.ins = !switches.ins;
        (switches.ins ? nowShown : nowHidden).push_back (track);
      }
      else {
        // we have already verified the tokens,
        std::unreachable();
      }
    }

    std::string outMsg;
    if (!nowShown.empty()) {
      outMsg += fmt::format (
          "Showing track/s: {}", fmt::join (nowShown, ", ")
      );
    }
    if (!nowHidden.empty()) {
      if (!outMsg.empty()) {
        outMsg += " | ";
      }
      outMsg += fmt::format (
          "Hiding track/s: {}", fmt::join (nowHidden, ", ")
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
      call, {}, &operator(), usage, desc
  };
};

static std::vector<std::string> word_wrap (
    std::string_view text, size_t width
)
{
  std::vector<std::string> lines;
  std::string cur;
  for (const auto& word : split_whitespace (text)) {
    if (cur.empty()) {
      cur.assign (word);
    }
    else if (cur.size() + 1 + word.size() <= width) {
      cur += ' ';
      cur += word;
    }
    else {
      lines.push_back (std::move (cur));
      cur.assign (word);
    }
  }
  lines.push_back (std::move (cur));
  return lines;
}

static void append_wrapped (
    std::vector<std::string>& out, std::string_view text,
    std::string_view indent, size_t width
)
{
  for (auto& line : word_wrap (text, width - indent.size())) {
    out.push_back (fmt::format ("{}{}", indent, line));
  }
}


static std::span<const CmdView* const> get_cmd_registry();
std::vector<std::string> build_cmd_ref_table()
{
  constexpr size_t width = 52;
  constexpr std::string_view headerIndent = "  ";
  constexpr std::string_view bodyIndent = "    ";

  std::vector<std::string> lines{" COMMAND REFERENCE"};
  for (const auto* cmd : get_cmd_registry()) {
    append_wrapped (
        lines, fmt::format ("`{}`:", cmd->usage), headerIndent,
        width
    );
    append_wrapped (lines, cmd->desc, bodyIndent, width);
    if (!cmd->alias.empty()) {
      append_wrapped (
          lines,
          fmt::format (
              "alias: {}", fmt::join (cmd->alias, ", ")
          ),
          bodyIndent, width
      );
    }
  }
  return lines;
}

struct HelpCmd {
  constexpr static std::string_view call{"help"};
  constexpr static std::array<std::string_view, 2> alias{
      "h", "?"
  };

  enum Topic : uint8_t { nav, cmd, table, COUNT };
  constexpr static std::array<std::string_view, Topic::COUNT>
      topicNames{{
          [Topic::nav] = "nav",
          [Topic::cmd] = "cmd",
          [Topic::table] = "table",
      }};

  inline static const std::string usage = fmt::format (
      "{} [({})]", call, fmt::join (topicNames, "|")
  );
  constexpr static std::string_view desc{
      "Show help for given topic, or general help with no args."
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
      size_and_set_overlay_widget (state.ui, helpblocks::app);
      out.success = true;
    }
    else if (std::ranges::contains (topicNames, tokens[0])) {
      const auto topic = tokens[0];
      if (topic == topicNames[Topic::nav]) {
        state.conf.showOverlay = true;
        size_and_set_overlay_widget (
            state.ui, helpblocks::navigation
        );
        out.success = true;
      }
      else if (topic == topicNames[Topic::cmd]) {
        static std::vector<std::string> cmdTable;
        static std::vector<std::string_view> tableView;
        cmdTable = build_cmd_ref_table();
        tableView.assign (cmdTable.begin(), cmdTable.end());

        state.conf.showOverlay = true;
        size_and_set_overlay_widget (state.ui, tableView);
        out.success = true;
      }
      else if (topic == topicNames[Topic::table]) {
        state.conf.showOverlay = true;
        size_and_set_overlay_widget (
            state.ui, helpblocks::table
        );
        out.success = true;
      }
      else {
        std::unreachable();
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

static constexpr std::array<const CmdView*, 14> cmdRegistry_SH{{
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
    &ShowTableCmd::view,
    &ShowTrackCmd::view,
    &ShowTableColCmd::view,
    &CountCmd::view,
}};

// `view`s are not constexpr, so here's somewhat horrible
// solution for compile time overlap checking. Keep in
// sync with above!
static constexpr std::array<
    std::span<const std::string_view>, 14>
    cmdAliases_SH{
        {HelpCmd::alias,
         QuitCmd::callAlias,
         WhereCmd::callAlias,
         {},
         {},
         BackCmd::callAlias,
         ClearWhereCmd::callAlias,
         OrderCmd::callAlias,
         ClearCmd::callAlias,
         {},
         ShowTableCmd::callAlias,
         ShowTrackCmd::callAlias,
         ShowTableColCmd::callAlias,
         CountCmd::callAlias}
    };
static constexpr bool all_aliases_unique()
{
  for (size_t i = 0; i < cmdAliases_SH.size(); i++) {
    const auto& iAliases = cmdAliases_SH[i];
    for (size_t j = i + 1; j < cmdAliases_SH.size(); j++) {
      const auto& jAliases = cmdAliases_SH[j];
      for (const auto& iAlias : iAliases) {
        for (const auto& jAlias : jAliases) {
          if (iAlias == jAlias) {
            return false;
          }
        }
      }
    }
  }
  return true;
}
static_assert (
    all_aliases_unique(),
    "Command registry contains overlapping aliases!"
);


static std::span<const CmdView* const> get_cmd_registry()
{
  return cmdRegistry_SH;
}

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
