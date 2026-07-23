#include "cmd.hpp"

#include <format>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "app/fields.hpp"
#include "app/state.hpp"
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
CmdResult quit (std::string_view, AppState& state)
{
  state.conf.run = false;
  return {true, "Bye!"};
}
CmdResult pileup_show (std::string_view names, AppState& state)
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
          false, std::format (
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
      std::format ("Showing query properties: {}", newRequests)
  };
}

CmdResult pileup_hide (std::string_view names, AppState& state)
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
      std::format ("Hiding query properties {}", reqsToRemove)
  };
}

static const std::unordered_set<std::string_view>
    VALID_CONJUNCTIONS{"AND", "and", "OR", "or"};

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
CmdResult append_where (std::string_view args, AppState& state)
{
  if (args.empty()) {
    return {true, ""};
  }

  if (!state.query.userClause.where.empty()) {
    auto connective = split_first_space (args).first;
    if (!VALID_CONJUNCTIONS.contains (connective)) {
      return {false, "Clause is missing conjunction (and/or)!"};
    }
  }

  auto newClause = state.query.userClause;
  newClause.where.emplace_back (args);

  PLOGD << std::format (
      "Attempting to compile statement with updated WHERE "
      "clause {}",
      args
  );

  return apply_query_clause (
      state, std::move (newClause), "OK!"
  );
}

CmdResult remove_last_where (std::string_view _, AppState& state)
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
      std::format ("Removed clause: {}", rmClause)
  );
};

CmdResult clear_where (std::string_view _, AppState& state)
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

CmdResult order_by (
    std::string_view rsql_clause, AppState& state
)
{
  if (rsql_clause.empty()) {
    return {true, ""};
  }

  // TODO: check clause validity?

  PLOGD << std::format ("User requesting sort: {}", rsql_clause);

  auto newClause = state.query.userClause;
  newClause.orderBy = rsql_clause;

  return apply_query_clause (
      state, std::move (newClause), "OK!"
  );
}

CmdResult reset_query (std::string_view _, AppState& state)
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

static std::unordered_map<
    std::string_view,
    std::function<CmdResult (std::string_view, AppState&)>>
    CMD_REGISTRY{
        {"q", &quit},
        {"quit", &quit},
        {"show", &pileup_show},
        {"hide", &pileup_hide},
        {"where", &append_where},
        {"w", &append_where},
        {"back", &remove_last_where},
        {"order", &order_by},
        {"o", &order_by},
        {"clear-where", &clear_where},
        {"cw", &clear_where},
        {"reset", &reset_query}
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
        false, std::format ("Command \"{}\" not found!", name)
    };
  }
}
