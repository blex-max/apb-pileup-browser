#include "cmd.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "app/fields.hpp"
#include "app/state.hpp"
#include "backend/PileupDB.hpp"
#include "plog/Log.h"

static std::pair<std::string_view, std::string_view>
split_first_space (std::string_view s)
{
  if (s.empty()) {
    return {};
  }
  auto pos = s.find (' ');
  if (pos == std::string_view::npos) {
    return {s, {}}; // no args
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
static CmdResult pileup_show (
    std::string_view names, AppState& state
)
{
  auto& existingRequests = state.conf.dataFieldsRequested;

  // split args
  const auto newRequests = split_whitespace (names);
  if (newRequests.empty()) {
    return {false, "needs args"};
  }

  for (const auto& req : newRequests) {
    auto it = TABLE_FIELD_LOOKUP.find (req);
    if (it == TABLE_FIELD_LOOKUP.end()) {
      return {
          false, fmt::format (
                     "Cannot show unknown "
                     "property \"{}\"",
                     req
                 )
      };
    }
    const auto* field = it->second;
    if (std::find (
            begin (existingRequests), end (existingRequests),
            field
        ) != end (existingRequests)) {
      continue;
    }
    existingRequests.push_back (field);
  }

  return {
      true,
      fmt::format ("Showing query properties: {}", newRequests)
  };
}

static CmdResult pileup_hide (
    std::string_view names, AppState& state
)
{
  auto& existingRequests = state.conf.dataFieldsRequested;

  // split args
  const auto reqsToRemove = split_whitespace (names);
  if (reqsToRemove.empty()) {
    return {false, "needs args"};
  }

  for (const auto& req : reqsToRemove) {
    auto it = TABLE_FIELD_LOOKUP.find (req);
    if (it == TABLE_FIELD_LOOKUP.end()) {
      // NOTE: silent noop
      continue;
    }
    existingRequests.remove (it->second);
  }

  return {
      true,
      fmt::format ("Hiding query properties {}", reqsToRemove)
  };
}

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
  auto prepRet = prepare_select_reads (state.db, newClause);
  if (!prepRet) {
    return {false, prepRet.error().msg};
  }
  state.query.userClause = std::move (newClause);
  state.query.stmt = std::move (*prepRet);
  state.ui.main.rowStart = 0;  // reset row view
  return {true, std::string (successMsg)};
}

// The exact repl syntax for usage of
// these is tbd.
static CmdResult init_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  auto& clauses = state.query.userClause;

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
      state, std::move (newClause), "OK!"
  );
}

static CmdResult and_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  std::string clause{"AND "};
  clause.append (args);

  auto newClause = state.query.userClause;
  newClause.where.emplace_back (clause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), "OK!"
  );
}
static CmdResult or_where (
    std::string_view args, AppState& state
)
{
  if (args.empty()) {
    return {true, ""};
  }

  std::string clause{"OR "};
  clause.append (args);

  auto newClause = state.query.userClause;
  newClause.where.emplace_back (clause);

  PLOGD << fmt::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), "OK!"
  );
}

static CmdResult remove_last_where (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Does not take args"};
  }

  auto newClause = state.query.userClause;
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

static CmdResult clear_where (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Expected no args"};
  }

  auto newClause = state.query.userClause;
  newClause.where.clear();

  return apply_query_clause (
      state, std::move (newClause), "Cleared WHERE clause"
  );
}

static CmdResult order_by (
    std::string_view rsql_clause, AppState& state
)
{
  if (rsql_clause.empty()) {
    return {true, ""};
  }

  // TODO: check clause validity?

  PLOGD << fmt::format ("User requesting sort: {}", rsql_clause);

  auto newClause = state.query.userClause;
  newClause.orderBy = rsql_clause;

  return apply_query_clause (
      state, std::move (newClause), "OK!"
  );
}

static CmdResult count (
    std::string_view rsql_clause, AppState& state
)
{
  auto where = state.query.userClause.where;
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

  auto stmtRet = prepare_count_reads (state.db, where);
  if (!stmtRet) {
    return {false, stmtRet.error().msg};
  }

  auto& stmt = *stmtRet;
  if (const int rc = sqlite3_step (stmt); rc != SQLITE_ROW) {
    return {
        false, fmt::format (
                   "Could not execute count: {}",
                   sqlite3_errmsg (state.db)
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

static CmdResult reset_query (
    std::string_view _, AppState& state
)
{
  if (!_.empty()) {
    return {false, "Expected no args"};
  }

  auto newClause = state.query.userClause;
  newClause.where.clear();
  newClause.orderBy.clear();
  // offset untouched

  return apply_query_clause (
      state, std::move (newClause), "Reset query"
  );
}

// Dump the in-memory db to a file on disk.
// NOTE: does not do any kind of query preservation. Possible
// future feature, but unlikely.
static CmdResult dump (std::string_view args, AppState& state)
{
  const auto tokens = split_whitespace (args);
  if (tokens.size() != 1) {
    return {false, "dump takes a single file path argument"};
  }

  const std::string path{tokens[0]};
  auto dumpRet = dump_to_disk (state.db, path);
  if (!dumpRet) {
    return {false, dumpRet.error().msg};
  }

  return {true, fmt::format ("Dumped database to {}", path)};
}

// TODO: consider what this should do
// static CmdResult help (std::string_view args, AppState& state)
// {
//   const auto tokens = split_whitespace(args);
//   if (tokens.size() > 1) {
//     return {false, "help takes a single argument (the command you want to display help for), or none (general help)"};
//   }
// }

// display a command reference table
static constexpr std::string_view HELPTEXT_TEST =
    "this is a placeholder";
static CmdResult show_help (std::string_view _, AppState& state)
{
  state.conf.showOverlay = true;
  state.ui.help.content = HELPTEXT_TEST;
  return {true, ""};
}

static std::unordered_map<
    std::string_view,
    std::function<CmdResult (std::string_view, AppState&)>>
    CMD_REGISTRY{
        {"q", &quit},
        {"quit", &quit},
        {"show", &pileup_show},
        {"hide", &pileup_hide},
        {"where", &init_where},
        {"w", &init_where},
        {"and", &and_where},
        {"or", &or_where},
        {"back", &remove_last_where},
        {"order", &order_by},
        {"o", &order_by},
        {"clear-where", &clear_where},
        {"cw", &clear_where},
        {"clear", &reset_query},
        {"count", &count},
        {"dump", &dump},
        {"help", &show_help}
    };

CmdResult exec_cmd (std::string_view call, AppState& state)
{
  auto [name, args] = split_first_space (call);
  if (auto it = CMD_REGISTRY.find (name);
      it != CMD_REGISTRY.end()) {
    return it->second (args, state);
  }
  else {
    return {
        false, fmt::format ("Command \"{}\" not found!", name)
    };
  }
}
