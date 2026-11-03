#include "cmd.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "generic/db_io.hpp"
#include "generic/describe.hpp"
#include "helpblocks.hpp"
#include "input.hpp"
#include "query.hpp"
#include "shared/cleanup.hpp"
#include "state.hpp"
#include "tui_assert.hpp"
#include "widgets.hpp"

namespace g_db = tui_state::db;
namespace g_ui = tui_state::ui;

// view type into the command registry
struct Cmd {
  std::span<const std::string_view> names;
  std::string_view usage;
  std::string_view desc;
  std::string (*cmd) (std::string_view);
};


// --- HELPERS --- //

#define CMD_GENERIC_SUCCESS "OK!"

static std::string cmd_format_misuse (
    std::string_view misuseMsg, std::string_view cmdUsage
)
{
  return fmt::format ("Misuse - {}. Usage: {}", misuseMsg, cmdUsage);
}
static std::string cmd_format_fail (std::string_view failMsg)
{
  return fmt::format ("Failed - {}", failMsg);
}

// does candidate contain test
static std::optional<size_t> cmd_find_substr (
    std::string_view s, std::string_view test
)
{
  const auto* it = std::search (
      s.begin(), s.end(), test.begin(), test.end(),
      [] (unsigned char a, unsigned char b) {
        return std::tolower (a) == std::tolower (b);
      }
  );
  if (it == s.end()) {
    return std::nullopt;
  }
  return static_cast<size_t> (it - s.begin());
}

static std::pair<std::string_view, std::string_view> split_first_space (
    std::string_view s
)
{
  const auto start = s.find_first_not_of (' ');
  if (start == std::string_view::npos) {
    return {};
  }
  s.remove_prefix (start);
  auto pos = s.find (' ');
  if (pos == std::string_view::npos) {
    return {s, {}};  // no args
  }
  return {s.substr (0, pos), s.substr (pos + 1)};
}

static std::vector<std::string_view> split_whitespace (std::string_view s)
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


static std::expected<void, std::string> cmd_validate_args_empty (
    const std::string_view args, const std::string_view cmdUsage
)
{
  if (!args.empty()) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("expected no args", cmdUsage)
    );
  }
  return {};
}
static std::expected<void, std::string> cmd_validate_ntok (
    const std::span<const std::string_view> argTok, uint8_t minNTok,
    uint8_t maxNTok, const std::string_view usage
)
{
  TUI_ASSERT (maxNTok >= minNTok, maxNTok, minNTok);
  TUI_ASSERT (!usage.empty());

  if (maxNTok == 0 && !argTok.empty()) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too many args", usage)
    );
  }

  if (argTok.size() > maxNTok) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too many args", usage)
    );
  }
  if (argTok.size() < minNTok) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too few args", usage)
    );
  }

  return {};
}

// --- COMMANDS --- //

// NOTE: names[0] is the canonical name, the rest are aliases
// NOTE: usage text roughly follows docopt

static constexpr std::array<std::string_view, 3> quitNames{
    "quit", "q", "exit"
};
static constexpr Cmd quitCmd{
    quitNames, "quit", "Exit the browser.",
    [] (std::string_view args) -> std::string {
      if (const auto ret = cmd_validate_args_empty (args, quitCmd.usage);
          !ret) {
        return ret.error();
      }
      tui_state::run = false;
      return "Bye!";
    }
};

static constexpr std::array<std::string_view, 2> colNames{"col", "c"};
static constexpr Cmd colCmd{
    colNames, "col <field-name>...",
    "Show/hide read data columns in the table pane. "
    "For a list of available columns, check the "
    "manual or run `? col`.",
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      const auto nargRet =
          cmd_validate_ntok (tokens, 1, UINT8_MAX, colCmd.usage);
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
        return cmd_format_misuse (
            fmt::format ("duplicated arg/s {}", fmt::join (dups, ", ")),
            colCmd.usage
        );
      }

      auto& tableCols = g_ui::tableCols;
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
          return cmd_format_fail (
              fmt::format (
                  "Column \"{}\" not known or "
                  "unavailable for display",
                  tok
              )
          );
        }
      }

      std::string outMsg;
      if (!nowVisible.empty()) {
        outMsg +=
            fmt::format ("Showing: {}", fmt::join (nowVisible, ", "));
      }
      if (!nowHidden.empty()) {
        if (!outMsg.empty()) {
          outMsg += "|";
        }
        outMsg += fmt::format ("Hiding: {}", fmt::join (nowHidden, ", "));
      }

      return outMsg;
    }
};

// incomingSql: the user sql that has been added to query, for
// display on failure. Any earlier sql already compiled,
// so can't be at fault and therefore should not be displayed on
// failure.
static std::string try_apply_query_clause (
    QueryFragments query, std::string_view incomingSql,
    std::string_view successMsg
)
{
  std::string failReason;
  switch (g_db::set_query (std::move (query))) {
    case g_db::SetQueryStatus::success:
      return std::string (successMsg);
    case g_db::SetQueryStatus::guardFail:
      // FIXME: I bet there are semicolons in tags...
      failReason = "semicolon in query!";
      break;
    case g_db::SetQueryStatus::s3Fail:
      // FIXME: report token errors with sqlite3_error_offset + arithmetic
      // FIXME: in principle, errcode can return null
      failReason = sqlite3_errstr (sqlite3_extended_errcode (g_db::conn));
      break;
  }
  if (!incomingSql.empty()) {
    failReason = fmt::format ("{} - {}", failReason, incomingSql);
  }
  return cmd_format_fail (failReason);
}

static constexpr std::array<std::string_view, 4> whereNames{
    "where", "w", "wh", "WHERE"
};
static constexpr Cmd whereCmd{
    whereNames, "where [condition]",
    "Start a new WHERE clause, overwriting any existing "
    "condition. Omitting the [condition] argument will clear the current "
    "WHERE clause. For a reference of queryable columns, check the "
    "manual or run `? col`.",
    [] (std::string_view args) -> std::string {
      // copy in case sql compile fails
      auto query = g_db::userQuery;
      query.where.clear();

      if (!args.empty()) {
        constexpr std::string_view orderBy{" ORDER BY "};
        if (const auto pos = cmd_find_substr (args, orderBy); pos) {
          update_where (query, args.substr (0, *pos));
          update_order (query, args.substr (*pos + orderBy.size()));
        }
        else {
          update_where (query, args);
        }
      }

      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

static constexpr std::array<std::string_view, 2> andNames{"and", "AND"};
static constexpr Cmd andCmd{
    andNames, "and <condition>",
    "Extend current WHERE clause with an AND condition.",
    [] (std::string_view args) -> std::string {
      if (args.empty()) {
        return cmd_format_misuse ("no condition given", andCmd.usage);
      }

      if (g_db::userQuery.where.empty()) {
        return cmd_format_fail ("WHERE clause empty; cannot add term");
      }

      if (cmd_find_substr (args, "ORDER BY")) {
        return cmd_format_fail ("AND clause cannot contain ORDER BY");
      }

      auto query = g_db::userQuery;

      update_where (query, args, Conjunctions::AND);

      // validates clause
      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

static constexpr std::array<std::string_view, 2> orNames{"or", "OR"};
static constexpr Cmd orCmd{
    orNames, "or <condition>", "Extend the query with an OR condition.",
    [] (std::string_view args) -> std::string {
      if (args.empty()) {
        return cmd_format_misuse ("no condition given", orCmd.usage);
      }

      if (g_db::userQuery.where.empty()) {
        return cmd_format_fail ("WHERE clause empty; cannot add term");
      }

      if (cmd_find_substr (args, "ORDER BY")) {
        return cmd_format_fail ("OR clause cannot contain ORDER BY");
      }

      auto query = g_db::userQuery;

      update_where (query, args, Conjunctions::OR);

      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

// FIXME: this should be order as primary name, sort as secondary...
static constexpr std::array<std::string_view, 5> orderNames{
    "order by", "o", "order", "ORDER BY", "sort"
};
static constexpr Cmd orderCmd{
    orderNames, "sort [condition]",
    "Sort reads by ORDER BY expression. Clears order by with no args, "
    "resetting to default ordering. When viewing multiple "
    "alignments, reads are always sorted by file as the primary key.",
    [] (std::string_view args) -> std::string {
      auto query = g_db::userQuery;
      update_order (query, args);

      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

static constexpr std::array<std::string_view, 2> countNames{"count", "ct"};
static constexpr Cmd countCmd{
    countNames, "count <condition>",
    "Count reads matching <condition>, AND-concatenated onto the current "
    "WHERE clause if present.",
    [] (std::string_view args) -> std::string {
      if (args.empty()) {
        return cmd_format_misuse ("no condition given", countCmd.usage);
      }
      auto query = g_db::userQuery;
      update_where (query, args, Conjunctions::AND);

      std::string sqlCountQuery{"SELECT COUNT(*) FROM reads"};
      if (!query.where.empty()) {
        sqlCountQuery.append (" WHERE ");
        sqlCountQuery.append (query.where);
        // add newline to avoid issues from user sql
        // e.g. trailing -- comment
        sqlCountQuery.append ("\n");
      }
      sqlCountQuery.append (";");

      // only the new args can be at fault; the rest already compiled
      const auto fail_with_args = [&args]() {
        std::string reason = sqlite3_errmsg (g_db::conn);
        if (!args.empty()) {
          reason = fmt::format ("{} - {}", reason, args);
        }
        return cmd_format_fail (reason);
      };

      sqlite3_stmt* o_stmt = nullptr;
      if (sqlite3_prepare_v2 (
              g_db::conn, sqlCountQuery.data(), -1, &o_stmt, NULL
          ) != SQLITE_OK) {
        return fail_with_args();
      }
      Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

      if (const int rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
        return fail_with_args();
      }

      return fmt::format (
          "{}: {} reads", query.where, sqlite3_column_int64 (o_stmt, 0)
      );
    }
};

static constexpr std::array<std::string_view, 2> clearNames{"clear", "cl"};
static constexpr Cmd clearCmd{
    clearNames, "clear",
    "Clear current query, resetting both WHERE and ORDER BY.",
    [] (std::string_view args) -> std::string {
      if (const auto res = cmd_validate_args_empty (args, clearCmd.usage);
          !res) {
        return res.error();
      };

      auto query = g_db::userQuery;
      query.where.clear();
      query.orderBy.clear();

      return try_apply_query_clause (std::move (query), {}, "Reset query");
    }
};

static constexpr std::array<std::string_view, 2> toggleNames{
    "toggle", "tg"
};
static constexpr Cmd toggleCmd{
    toggleNames, "toggle (quality|insertion|table|sparkline)",
    "Toggle various aspects of the display. quality and insertion toggle "
    "display of their respective tracks in the alignment pane. table "
    "toggles display of the table pane. sparkline toggles display of the "
    "quality track between Phred ASCII and a quick-look sparkline graph. "
    "Accepts any unambiguous substring of the argument to toggle, e.g. "
    "`tg qual`.",
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      if (const auto expectedNTok =
              cmd_validate_ntok (tokens, 1, 1, toggleCmd.usage);
          !expectedNTok) {
        return expectedNTok.error();
      }
      // get single arg
      const auto& tok = tokens[0];

      // NOTE: all unambiguous from the first char (so far)
      static constexpr std::array<std::string_view, 4> opts{
          "quality", "insertion", "table", "sparkline"
      };
      for (const auto& opt : opts) {
        if (tok == opt.substr (0, tok.length())) {
          if (opt == "quality") {
            g_ui::drawQualTrack = !g_ui::drawQualTrack;
          }
          else if (opt == "insertion") {
            g_ui::drawInsTrack = !g_ui::drawInsTrack;
          }
          else if (opt == "table") {
            g_ui::drawTable = !g_ui::drawTable;
          }
          else if (opt == "sparkline") {
            g_ui::drawSparkline = !g_ui::drawSparkline;
          }
          else {
            TUI_UNREACHABLE ("toggle command malformed, unknown opt", opt);
          }
          return CMD_GENERIC_SUCCESS;
        }
      }
      return cmd_format_misuse (
          fmt::format ("Unknown option: \"{}\"", tok), toggleCmd.usage
      );
    }
};

static constexpr std::array<std::string_view, 1> dumpNames{"dump"};
static constexpr Cmd dumpCmd{
    dumpNames, "dump <path>",
    "Write the pileup database to an sqlite3 dump file.",
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      if (const auto expectedNTok =
              cmd_validate_ntok (tokens, 1, 1, dumpCmd.usage);
          !expectedNTok) {
        return expectedNTok.error();
      }

      const std::string path{tokens[0]};
      if (const auto ret = dump_to_disk (g_db::conn, path); !ret) {
        return apb_describe_sqlite_failure (
            "Failed to dump database to disk", ret.error().rc,
            ret.error().msg
        );
      }
      return fmt::format ("Dumped database to {}", path);
    }
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


static std::span<const Cmd* const> get_cmd_registry();
std::vector<std::string> build_cmd_ref_table()
{
  constexpr size_t width = 52;
  constexpr std::string_view headerIndent = "  ";
  constexpr std::string_view bodyIndent = "    ";

  std::vector<std::string> lines{" COMMAND REFERENCE"};
  for (const auto* cmd : get_cmd_registry()) {
    append_wrapped (
        lines, fmt::format ("`{}`:", cmd->usage), headerIndent, width
    );
    append_wrapped (lines, cmd->desc, bodyIndent, width);
    if (cmd->names.size() > 1) {
      append_wrapped (
          lines,
          fmt::format (
              "alias: {}", fmt::join (cmd->names.subspan (1), ", ")
          ),
          bodyIndent, width
      );
    }
    lines.push_back ("");
  }
  return lines;
}

enum HelpTopic : uint8_t { topicNav, topicCmd, topicCol, topicCount };
static constexpr std::array<std::string_view, HelpTopic::topicCount>
    helpTopicNames{{
        [HelpTopic::topicNav] = "nav",
        [HelpTopic::topicCmd] = "cmd",
        [HelpTopic::topicCol] = "col",
    }};

static constexpr std::array<std::string_view, 3> helpNames{
    "help", "h", "?"
};
static constexpr Cmd helpCmd{
    helpNames, "help [(nav|cmd|col)]",
    "Show help for given topic, or general help with no args.",
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      const auto nargRet = cmd_validate_ntok (tokens, 0, 1, helpCmd.usage);
      if (!nargRet) {
        return nargRet.error();
      }

      helpblocks::TextBlockRef content;
      if (tokens.empty()) {
        static std::vector<std::string> appBlock;
        static std::vector<std::string_view> appBlockView;
        appBlock.assign (helpblocks::app.begin(), helpblocks::app.end());
        appBlock.push_back ("");
        appBlock.push_back (fmt::format (" apb version {}", APB_VERSION));
        appBlockView.assign (appBlock.begin(), appBlock.end());
        content = appBlockView;
      }
      else if (std::ranges::contains (helpTopicNames, tokens[0])) {
        const auto topic = tokens[0];
        if (topic == helpTopicNames[HelpTopic::topicNav]) {
          content = helpblocks::navigation;
        }
        else if (topic == helpTopicNames[HelpTopic::topicCmd]) {
          static std::vector<std::string> cmdBlock;
          static std::vector<std::string_view> cmdBlockView;
          cmdBlock = build_cmd_ref_table();
          cmdBlockView.assign (cmdBlock.begin(), cmdBlock.end());
          content = cmdBlockView;
        }
        else if (topic == helpTopicNames[HelpTopic::topicCol]) {
          content = helpblocks::tableColumns;
        }
        else {
          TUI_UNREACHABLE ("unrecognised help topic");
        }
      }
      else {
        return cmd_format_misuse (
            fmt::format ("unknown topic {}", tokens[0]), helpCmd.usage
        );
      }

      if (!size_and_set_overlay_widget (content)) {
        return cmd_format_fail ("terminal too small to display help pane");
      }
      g_ui::showOverlay = true;
      return {};
    }
};

static std::span<const Cmd* const> get_cmd_registry()
{
  static constexpr std::array<const Cmd*, 11> registry{{
      /* all cmds must be added here */
      &helpCmd,
      &quitCmd,
      &whereCmd,
      &andCmd,
      &orCmd,
      &orderCmd,
      &clearCmd,
      &countCmd,
      &colCmd,
      &toggleCmd,
      &dumpCmd,
  }};
  return registry;
}

// get vector of pairs of all command names/aliases
// in length order, for safe prefix matching by
// longest first
static auto get_cmd_name_map()
{
  static const auto nameMap = []() {
    std::vector<std::pair<std::string_view, const Cmd*>> nameArr;
    for (const auto* cmd : get_cmd_registry()) {
      for (const auto& name : cmd->names) {
        nameArr.emplace_back (name, cmd);
      }
    }
    std::sort (
        begin (nameArr), end (nameArr), [] (const auto& a, const auto& b) {
          return a.first.size() > b.first.size();
        }
    );
    return nameArr;
  }();
  return nameMap;
}

std::string exec_cmd (std::string_view call)
{
  for (const auto& [name, cmd] : get_cmd_name_map()) {
    if (call == name) {
      return cmd->cmd ("");
    }
    if (call.starts_with (std::string{name} + " ")) {
      auto args = call.substr (name.size() + 1);
      args.remove_prefix (
          std::min (args.find_first_not_of (" \t"), args.size())
      );
      return cmd->cmd (args);
    }
  }
  return fmt::format ("Unknown command: {}", call);
}


#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

TEST_CASE ("command registry has no overlapping names")
{
  std::vector<std::string_view> names;
  for (const auto* cmd : get_cmd_registry()) {
    names.insert (names.end(), cmd->names.begin(), cmd->names.end());
  }
  std::ranges::sort (names);
  const auto dup = std::ranges::adjacent_find (names);
  if (dup != names.end()) {
    FAIL ("duplicate command name: ", *dup);
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
