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
// bundled with it, declared alongside the function. CMD_TABLE at
// the foot of the file is the registry of those pairings.
struct Command {
  std::string_view name;
  std::string_view alias;  // "" if none
  std::string_view usage;
  CmdResult (*run) (std::string_view, AppState&);
};

static constexpr std::string CMD_GENERIC_SUCCESS = "OK!";

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
static constexpr Command CMD_QUIT{
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
    const auto* reqCol = find_col (req);
    if (reqCol == nullptr) {
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
            reqCol
        ) != end (existingRequests)) {
      continue;
    }
    existingRequests.push_back (reqCol);
  }

  return {
      true,
      fmt::format ("Showing query properties: {}", newRequests)
  };
}
static constexpr Command CMD_SHOW{
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
    const auto* reqCol = find_col (req);
    if (reqCol == nullptr) {
      // NOTE: silent noop
      continue;
    }
    existingRequests.remove (reqCol);
  }

  return {
      true,
      fmt::format ("Hiding query properties {}", reqsToRemove)
  };
}
static constexpr Command CMD_HIDE{
    "hide", "",
    "Remove read properties from the pileup table. Names that "
    "are not properties are ignored.",
    &pileup_hide
};

static const std::unordered_set<std::string_view>
    VALID_CONJUNCTIONS{"AND", "and", "OR", "or"};

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
  state.ui.main.rowStart = 0;  // reset row view
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
      state, std::move (newClause), CMD_GENERIC_SUCCESS
  );
}
static constexpr Command CMD_WHERE{
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

  std::string clause{"AND "};
  clause.append (args);

  auto newClause = state.db.userClause;
  newClause.where.emplace_back (clause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), CMD_GENERIC_SUCCESS
  );
}
static constexpr Command CMD_AND{
    "and", "",
    "Extend the query with a further condition, joined by AND.",
    &and_where
};

static CmdResult or_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  std::string clause{"OR "};
  clause.append (args);

  auto newClause = state.db.userClause;
  newClause.where.emplace_back (clause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), CMD_GENERIC_SUCCESS
  );
}
static constexpr Command CMD_OR{
    "or", "",
    "Extend the query with a further condition, joined by OR.",
    &or_where
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
static constexpr Command CMD_BACK{
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
static constexpr Command CMD_CLEAR_WHERE{
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
      state, std::move (newClause), CMD_GENERIC_SUCCESS
  );
}
static constexpr Command CMD_ORDER{
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
      if (VALID_CONJUNCTIONS.contains (connective)) {
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
static constexpr Command CMD_COUNT{
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
static constexpr Command CMD_CLEAR{
    "clear", "",
    "Reset the query: drop every condition and the sort order.",
    &reset_query
};

static CmdResult fold_pane (
    std::string_view args, AppState& state
)
{
  // fragile!
  CmdResult out;
  auto& qbf = state.conf.seqPaneFrac;
  static std::unordered_map<
      std::string_view, std::function<void (CmdResult&)>>
      PANE_SPECIFIERS{
          {"seq",
           [&qbf] (CmdResult& out) {
             if (qbf == 0.0) {
               qbf = 0.5;
               out.msg = "Unfolded sequence pane";
             }
             else {
               qbf = 0.0;
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
          state.ui.main, state.conf.seqPaneFrac
      );  // shouldn't error in this context (I hope)
      out.msg = "Reset view to default";
      out.success = true;
    }
    else {
      out.msg =
          "View already at default, specify arg (browser, data) "
          "to change";
      out.success = false;
    }
    return out;
  }
  if (tokens.size() > 1) {
    out.success = false;
    out.msg = "Specify a single pane only (browser, data)";
    return out;
  }

  const auto pane_arg = tokens[0];

  if (const auto& it = PANE_SPECIFIERS.find (pane_arg);
      it != PANE_SPECIFIERS.end()) {
    it->second (out);
    size_browser_panes (
        state.ui.main, state.conf.seqPaneFrac
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
static constexpr Command CMD_FOLD_PANE{
    "pane", "",
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
static constexpr Command CMD_DUMP{
    "dump", "",
    "Write the in-memory database to a file. Takes a single "
    "path. The current query is not preserved.",
    &dump_db
};

static CmdResult dump_readme (std::string_view args, AppState&)
{
  static constexpr std::string k_readmeFileName =
      "APB-README.md";
  std::string outPath;
  const auto tokens = split_whitespace (args);
  if (tokens.empty()) {
    outPath = "./";
    outPath += k_readmeFileName;
  }
  else if (tokens.size() == 1) {
    outPath = tokens[0];
    outPath += "/";
    outPath += k_readmeFileName;
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
static constexpr Command CMD_README{
    "readme", "",
    "Dumps readme shipped with repo to a provided directory, or "
    "the working directory if no path is given.",
    &dump_readme
};

// NOTE: must have navigation help in overlay
// maybe help should show that, and give a second
// command which will show the command reference
// table. I think probably!
/* PLAN: this will show command reference table,
   and instruct to read/dump the readme for more
   info.
   Much less work than trying to build essentially
   my own internal man window, and minimises sources
   of truth by prioritising readme.
   If an arg is passed, will show usage. */
// forward declare find_cmd for lookup in help
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
    else {
      // not sure if this branch is necessary?
      out.msg = find_cmd (tokens[0])->usage.data();
      out.success = !out.msg.empty();
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
static constexpr Command CMD_HELP{
    "help", "?", "Show this help.", &show_help
};

// Registry
static constexpr const Command* CMD_TABLE[]{
    &CMD_QUIT,        &CMD_SHOW,  &CMD_HIDE,     &CMD_WHERE,
    &CMD_AND,         &CMD_OR,    &CMD_BACK,     &CMD_ORDER,
    &CMD_CLEAR_WHERE, &CMD_CLEAR, &CMD_COUNT,    &CMD_DUMP,
    &CMD_README,      &CMD_HELP,  &CMD_FOLD_PANE
};

static const Command* find_cmd (std::string_view name)
{
  for (const Command* cmd : CMD_TABLE) {
    if (cmd->name == name ||
        (!cmd->alias.empty() && cmd->alias == name)) {
      return cmd;
    }
  }
  return nullptr;
}

CmdResult exec_cmd (std::string_view call, AppState& state)
{
  auto [name, args] = split_first_space (call);
  if (const Command* cmd = find_cmd (name)) {
    return cmd->run (args, state);
  }
  return {
      false, fmt::format ("Command \"{}\" not found!", name)
  };
}
