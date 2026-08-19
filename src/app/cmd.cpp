#include "cmd.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <plog/Log.h>

#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "app/text_blocks.hpp"
#include "app/widgets.hpp"
#include "backend/PileupDB.hpp"

// A command is a function paired with the name and help text
// bundled with it, declared alongside the function. sh_cmdTable at
// the foot of the file is the registry of those pairings.
struct Command {
  std::string_view name;
  std::string_view alias;  // "" if none
  std::string_view usage;
  CmdResult (*run) (std::string_view, AppState&);
};

// TODO: add generic failure,
// on failure, emit generic failure + cmd usage
static constexpr std::string sh_cmdGenericSuccess = "OK!";

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

// Commands

static CmdResult quit (std::string_view, AppState& state)
{
  state.conf.run = false;
  return {true, "Bye!"};
}
static constexpr Command sh_cmdQuit{
    "quit", "q", "Exit the browser.", &quit
};

static CmdResult pileup_show (
    std::string_view names, AppState& state
)
{
  auto& existingRequests = state.conf.colsRequested;

  // split args
  const auto newRequests = split_whitespace (names);
  if (newRequests.empty()) {
    return {false, "needs args"};
  }

  for (const auto& req : newRequests) {
    const auto* br_reqCol = find_cols (req);
    if (br_reqCol == nullptr) {
      return {
          false, fmt::format (
                     "Cannot show unknown "
                     "column \"{}\"",
                     req
                 )
      };
    }
    if (std::find (
            begin (existingRequests), end (existingRequests),
            br_reqCol
        ) != end (existingRequests)) {
      continue;
    }
    existingRequests.push_back (br_reqCol);
  }

  return {
      true,
      fmt::format ("Showing query properties: {}", newRequests)
  };
}
static constexpr Command sh_cmdShow{
    "show", "",
    "Add read properties to the pileup table. Takes one or more "
    "property names separated by spaces.",
    &pileup_show
};

static CmdResult pileup_hide (
    std::string_view names, AppState& state
)
{
  auto& existingRequests = state.conf.colsRequested;

  // split args
  const auto reqsToRemove = split_whitespace (names);
  if (reqsToRemove.empty()) {
    return {false, "needs args"};
  }

  for (const auto& req : reqsToRemove) {
    const auto* br_reqCol = find_cols (req);
    if (br_reqCol == nullptr) {
      // NOTE: silent noop
      continue;
    }
    existingRequests.remove (br_reqCol);
  }

  return {
      true,
      fmt::format ("Hiding query properties {}", reqsToRemove)
  };
}
static constexpr Command sh_cmdHide{
    "hide", "",
    "Remove read properties from the pileup table. Names that "
    "are not properties are ignored.",
    &pileup_hide
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

// Recompile `newClause` and, on success, install it as the active query
// and reset the scroll position. Callers own their own clause mutation
// and success message; this only owns the repeated recompile/swap tail.
static CmdResult apply_query_clause (
    AppState& state, DynamicFragments newClause,
    std::string_view successMsg
)
{
  auto prepRet = prepare_select_reads (state.db.db, newClause);
  if (!prepRet) {
    return {false, prepRet.error().msg};
  }
  state.db.userClause = std::move (newClause);
  state.db.stmt = std::move (*prepRet);
  state.ui.browsr.rowStart = 0;  // reset row view
  return {true, std::string (successMsg)};
}

// should perhaps override existing
// clause. But then does back work?
static CmdResult init_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  auto& clauses = state.db.userClause;

  if (!clauses.where.empty()) {
    return {false, "Query present, clear to start a new query"};
  }

  auto newClause = clauses;
  newClause.where.emplace_back (args);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), sh_cmdGenericSuccess
  );
}
static constexpr Command sh_cmdWhere{
    "where", "w",
    "Start a query with an SQL WHERE condition. Fails if a "
    "query is already present; extend it with and/or, or clear "
    "it first.",
    &init_where
};

static CmdResult and_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  auto curClauseCopy = state.db.userClause;
  if (curClauseCopy.where.empty()) {
    return {false, "WHERE clause empty; cannot add term"};
  }

  std::string newClause{"AND "};
  newClause.append (args);

  curClauseCopy.where.emplace_back (newClause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (curClauseCopy), sh_cmdGenericSuccess
  );
}
static constexpr Command sh_cmdAnd{
    "and", "", "Extend the query with an AND condition.",
    &and_where
};

static CmdResult or_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  auto curClauseCopy = state.db.userClause;
  if (curClauseCopy.where.empty()) {
    return {false, "WHERE clause empty; cannot add term"};
  }

  std::string clause{"OR "};
  clause.append (args);

  curClauseCopy.where.emplace_back (clause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (curClauseCopy), sh_cmdGenericSuccess
  );
}
static constexpr Command sh_cmdOr{
    "or", "", "Extend the query with an OR condition.", &or_where
};

// TODO: extend capabilities,
// keep buffer of queries
static CmdResult remove_last_where (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Does not take args"};
  }

  auto newClause = state.db.userClause;
  if (newClause.where.empty()) {
    return {true, ""};
  }
  auto rmClause = newClause.where.back();
  newClause.where.pop_back();

  return apply_query_clause (
      state, std::move (newClause),
      fmt::format ("Removed clause: {}", rmClause)
  );
};
static constexpr Command sh_cmdBack{
    "back", "",
    "Drop the most recently added condition from the query.",
    &remove_last_where
};

static CmdResult clear_where (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Expected no args"};
  }

  auto newClause = state.db.userClause;
  newClause.where.clear();

  return apply_query_clause (
      state, std::move (newClause), "Cleared WHERE clause"
  );
}
static constexpr Command sh_cmdClearWhere{
    "clear-where", "cw",
    "Drop every condition from the query, keeping the sort "
    "order.",
    &clear_where
};

static CmdResult order_by (
    std::string_view rsql_clause, AppState& state
)
{
  if (rsql_clause.empty()) {
    return {true, ""};
  }

  // TODO: check clause validity?

  PLOGD << fmt::format ("User requesting sort: {}", rsql_clause);

  auto newClause = state.db.userClause;
  newClause.orderBy = rsql_clause;

  return apply_query_clause (
      state, std::move (newClause), sh_cmdGenericSuccess
  );
}
static constexpr Command sh_cmdOrder{
    "order", "o", "Sort rows by an SQL ORDER BY expression.",
    &order_by
};

static CmdResult count (
    std::string_view rsql_clause, AppState& state
)
{
  auto where = state.db.userClause.where;
  if (!rsql_clause.empty()) {
    std::string clause{"AND "};
    if (!where.empty()) {
      auto connective = split_first_space (rsql_clause).first;
      if (sh_validConjunctions.contains (connective)) {
        return {
            false,
            "Count clauses should not be prepended with "
            "conjuctions (and/or). They are interpreted as AND "
            "with the existing statement."
        };
      }
    }
    clause.append (rsql_clause);
    where.emplace_back (clause);
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
static constexpr Command sh_cmdCount{
    "count", "",
    "Count the reads matching the query. An optional condition "
    "is ANDed with the query for this count only, and must not "
    "be prefixed with and/or.",
    &count
};

static CmdResult reset_query (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Expected no args"};
  }

  auto newClause = state.db.userClause;
  newClause.where.clear();
  newClause.orderBy.clear();
  // offset untouched

  return apply_query_clause (
      state, std::move (newClause), "Reset query"
  );
}
static constexpr Command sh_cmdClear{
    "clear", "",
    "Reset the query: drop every condition and the sort order.",
    &reset_query
};

static CmdResult fold_pane (
    std::string_view args, AppState& state
)
{
  // NOTE: needs improvement.
  // Very graceless. Folding the data
  // pane is nice, you just see the pane
  // separator next to the rh border.
  // The same is not true of folding
  // the seq pane. Also, should definitely
  // assemble readme and cmd reference table
  // from cmd structs, and stick to a single error
  // msg per misuse of a fn! TODO!
  CmdResult out;
  auto& qbf = state.conf.seqPaneFrac;
  static std::unordered_map<
      std::string_view, std::function<void (CmdResult&)>>
      paneSpecifiers{
          {"seq",
           [&qbf] (CmdResult& out) {
             if (qbf == 0.01) {
               qbf = 0.5;
               out.msg = "Unfolded sequence pane";
             }
             else {
               qbf = 0.01;
               out.msg = "Folded sequence pane";
             };
           }},
          {"data", [&qbf] (CmdResult& out) {
             if (qbf == 1.0) {
               qbf = 0.5;
               out.msg = "Unfolded data pane";
             }
             else {
               qbf = 1.0;
               out.msg = "Folded data pane";
             };
           }},
      };

  const auto tokens = split_whitespace (args);
  if (tokens.empty()) {
    if (qbf != 0.5) {
      qbf = 0.5;
      size_browser_panes (
          state.ui.browsr, state.conf.seqPaneFrac
      );  // shouldn't error in this context (I hope)
      out.msg = "Reset view to default";
      out.success = true;
    }
    else {
      out.msg =
          "View already at default, specify arg (seq, data) "
          "to change";
      out.success = false;
    }
    return out;
  }
  if (tokens.size() > 1) {
    out.success = false;
    out.msg = "Specify a single pane only (seq, data)";
    return out;
  }

  const auto pane_arg = tokens[0];

  if (const auto& it = paneSpecifiers.find (pane_arg);
      it != paneSpecifiers.end()) {
    it->second (out);
    size_browser_panes (
        state.ui.browsr, state.conf.seqPaneFrac
    );  // shouldn't error in this context (I hope)
    out.success = true;
  }
  else {
    out.success = false;
    out.msg = "Unknown pane \"";
    out.msg += pane_arg;
    out.msg += "\" - valid panes: seq, data";
  }

  return out;
}
// TODO: is `pane` a better name?
static constexpr Command sh_cmdFoldPane{
    "fold", "",
    "fold [seq, data] - show/hide either of the sequence or "
    "data panes, or reset to default with no args",
    &fold_pane
};

// Dump the in-memory db to a file on disk.
// NOTE: does not do any kind of query preservation. Possible
// future feature, but unlikely.
static CmdResult dump_db (std::string_view args, AppState& state)
{
  const auto tokens = split_whitespace (args);
  if (tokens.size() != 1) {
    return {false, "dump takes a single file path argument"};
  }

  const std::string path{tokens[0]};
  auto dumpRet = dump_to_disk (state.db.db, path);
  if (!dumpRet) {
    return {false, dumpRet.error().msg};
  }

  return {true, fmt::format ("Dumped database to {}", path)};
}
static constexpr Command sh_cmdDump{
    "dump", "",
    "Write the in-memory database to a file. Takes a single "
    "path. The current query is not preserved.",
    &dump_db
};

static CmdResult dump_readme (std::string_view args, AppState&)
{
  static constexpr std::string sh_readmeFilename =
      "APB-README.md";
  std::string outPath;
  const auto tokens = split_whitespace (args);
  if (tokens.empty()) {
    outPath = "./";
    outPath += sh_readmeFilename;
  }
  else if (tokens.size() == 1) {
    outPath = tokens[0];
    outPath += "/";
    outPath += sh_readmeFilename;
  }
  else {
    return {
        false,
        "takes a single directory path, or no args for cwd"
    };
  }

  std::ofstream outStream{outPath, std::ios::binary};
  if (!outStream) {
    return {
        false,
        "could not open " + outPath + "; failed to dump readme"
    };
  }
  auto readme = get_readme();
  outStream.write (
      readme.data(), static_cast<int> (readme.size())
  );
  if (!outStream) {
    return {false, "failed during write readme at " + outPath};
  }
  return {true, "readme written to " + outPath};
}
static constexpr Command sh_cmdReadme{
    "readme", "",
    "Dumps readme shipped with repo to a provided directory, or "
    "the working directory if no path is given.",
    &dump_readme
};

static const Command* find_cmd (std::string_view name);
static CmdResult show_help (
    std::string_view args, AppState& state
)
{
  CmdResult out;
  const auto tokens = split_whitespace (args);
  if (tokens.empty()) {
    // show general help
    state.conf.showOverlay = true;
    set_overlay_widget (
        state.ui, get_text_block (TxtBlockId::generalHelp)
    );
    out.success = true;
  }
  else if (tokens.size() == 1) {
    if (tokens[0] == "nav") {
      state.conf.showOverlay = true;
      set_overlay_widget (
          state.ui, get_text_block (TxtBlockId::navHelp)
      );
      out.success = true;
    }
    else if (tokens[0] == "cmd") {
      state.conf.showOverlay = true;
      set_overlay_widget (
          state.ui, get_text_block (TxtBlockId::cmdRef)
      );
      out.success = true;
    }
    else if (const auto* br_cmd = find_cmd (tokens[0])) {
      // not sure this branch adds value
      out.msg = br_cmd->usage;
      out.success = !out.msg.empty();
    }
    else {
      out.msg =
          fmt::format ("Unknown command \"{}\"", tokens[0]);
      out.success = false;
    }
  }
  else {
    out.success = false;
    out.msg =
        "takes 0 args for command reference, or 1 command name "
        "for usage";
  }
  return out;
}
static constexpr Command sh_cmdHelp{
    "help", "?", "Show this help.", &show_help
};

// Registry
static constexpr const Command* sh_cmdTable[]{
    &sh_cmdQuit,   &sh_cmdShow,  &sh_cmdHide,
    &sh_cmdWhere,  &sh_cmdAnd,   &sh_cmdOr,
    &sh_cmdBack,   &sh_cmdOrder, &sh_cmdClearWhere,
    &sh_cmdClear,  &sh_cmdCount, &sh_cmdDump,
    &sh_cmdReadme, &sh_cmdHelp,  &sh_cmdFoldPane
};

static const Command* find_cmd (std::string_view name)
{
  for (const Command* br_cmd : sh_cmdTable) {
    if (br_cmd->name == name ||
        (!br_cmd->alias.empty() && br_cmd->alias == name)) {
      return br_cmd;
    }
  }
  return nullptr;
}

CmdResult exec_cmd (std::string_view call, AppState& state)
{
  auto [name, args] = split_first_space (call);
  if (const Command* br_cmd = find_cmd (name)) {
    return br_cmd->run (args, state);
  }
  return {
      false, fmt::format ("Command \"{}\" not found!", name)
  };
}
