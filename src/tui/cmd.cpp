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
  std::string_view usageArgs;
  std::span<const std::string_view> desc;
  std::string (*cmd) (std::string_view);
};


// --- HELPERS --- //

#define CMD_GENERIC_SUCCESS "OK!"

static std::string cmd_format_misuse (
    std::string_view misuseMsg, std::string_view cmdName,
    std::string_view cmdUsage
)
{
  return fmt::format (
      "Misuse - {}. Usage: {} {}", misuseMsg, cmdName, cmdUsage
  );
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
    const std::string_view args, const Cmd& cmd
)
{
  if (!args.empty()) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("expected no args", cmd.names[0], cmd.usageArgs)
    );
  }
  return {};
}
static std::expected<void, std::string> cmd_validate_ntok (
    const std::span<const std::string_view> argTok, uint8_t minNTok,
    uint8_t maxNTok, const Cmd& cmd
)
{
  TUI_ASSERT (maxNTok >= minNTok, maxNTok, minNTok);
  TUI_ASSERT (!cmd.usageArgs.empty());

  if (maxNTok == 0 && !argTok.empty()) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too many args", cmd.names[0], cmd.usageArgs)
    );
  }

  if (argTok.size() > maxNTok) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too many args", cmd.names[0], cmd.usageArgs)
    );
  }
  if (argTok.size() < minNTok) {
    return std::unexpected<std::string> (
        cmd_format_misuse ("too few args", cmd.names[0], cmd.usageArgs)
    );
  }

  return {};
}

static constexpr std::span<const Cmd* const> get_cmd_registry();
std::vector<std::string> build_cmd_ref_table()
{
  constexpr std::string_view headerIndent = "  ";
  constexpr std::string_view bodyIndent = "    ";

  std::vector<std::string> lines{" COMMAND REFERENCE"};
  for (const auto* cmd : get_cmd_registry()) {
    lines.emplace_back (
        fmt::format (
            "{}`{} {}`:", headerIndent, cmd->names[0], cmd->usageArgs
        )
    );
    for (const auto descLine : cmd->desc) {
      lines.emplace_back (fmt::format ("{}{}", bodyIndent, descLine));
    }
    if (cmd->names.size() > 1) {
      lines.emplace_back (
          fmt::format (
              "{} aliases: {}", bodyIndent,
              fmt::join (cmd->names.subspan (1), ", ")
          )
      );
    }
    lines.push_back ("");
  }
  return lines;
}

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

// --- COMMANDS --- //
// clang-format off

// NOTE: names[0] is the canonical name, the rest are aliases
// NOTE: usage text roughly follows docopt

static constexpr auto quitNames =
    std::to_array<std::string_view> ({"quit", "q", "exit"});
static constexpr auto quitDesc =
    std::to_array<std::string_view> ({"Exit the browser."});
static constexpr Cmd quitCmd{
    quitNames, "", quitDesc,
    [] (std::string_view args) -> std::string {
      if (const auto ret = cmd_validate_args_empty (args, quitCmd);
          !ret) {
        return ret.error();
      }
      tui_state::run = false;
      return "Bye!";
    }
};

static constexpr auto colNames =
    std::to_array<std::string_view> ({"col", "c"});
static constexpr auto colDesc = std::to_array<std::string_view> ({
    "Show/hide read data columns in the",
    "table pane. For a list of available",
    "columns, check the manual or run `? col`.",
});
static constexpr Cmd colCmd{
    colNames, "<field-name>...", colDesc,
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      const auto nargRet =
          cmd_validate_ntok (tokens, 1, UINT8_MAX, colCmd);
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
            colCmd.names[0], colCmd.usageArgs
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

static constexpr auto whereNames =
    std::to_array<std::string_view> ({"where", "w", "wh", "WHERE"});
static constexpr auto whereDesc = std::to_array<std::string_view> ({
    "Start a new WHERE clause, overwriting",
    "any existing condition. Omitting the",
    "[condition] argument will clear the current",
    "WHERE clause. For a reference of queryable",
    "columns, check the manual or run `? col`.",
});
static constexpr Cmd whereCmd{
    whereNames, "[condition]", whereDesc,
    [] (std::string_view args) -> std::string {
      // copy in case sql compile fails
      auto query = g_db::userQuery;
      query.where.clear();

      if (!args.empty()) {
        constexpr std::string_view orderBy{" ORDER BY "};
        if (const auto pos = cmd_find_substr (args, orderBy); pos) {
          set_where (query, args.substr (0, *pos));
          set_order (query, args.substr (*pos + orderBy.size()));
        }
        else {
          set_where (query, args);
        }
      }

      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

static constexpr auto orderNames = std::to_array<std::string_view> (
    {"order by", "o", "order", "ORDER BY", "sort"}
);
static constexpr auto orderDesc = std::to_array<std::string_view> ({
    "Sort reads by ORDER BY expression. Clears order",
    "by with no args, resetting to default ordering.",
    "When viewing multiple alignments, reads are",
    "always sorted by file as the primary key.",
});
static constexpr Cmd orderCmd{
    orderNames, "[condition]", orderDesc,
    [] (std::string_view args) -> std::string {
      auto query = g_db::userQuery;
      set_order (query, args);

      return try_apply_query_clause (
          std::move (query), args, CMD_GENERIC_SUCCESS
      );
    }
};

static constexpr auto countNames =
    std::to_array<std::string_view> ({"count", "ct"});
static constexpr auto countDesc = std::to_array<std::string_view> ({
    "Count reads matching <condition>, AND-concatenated",
    "onto the current WHERE clause if present.",
});
static constexpr Cmd countCmd{
    countNames, "<condition>", countDesc,
    [] (std::string_view args) -> std::string {
      const auto clause = sanitise_clause(args);
      if (clause.empty()) {
        return cmd_format_misuse (
            "no condition given", countCmd.names[0], countCmd.usageArgs
        );
      }
      auto query = g_db::userQuery;
      const auto curWhere = query.where;
      if (query.where.empty()) {
        set_where(query, clause);
      }
      else {
        extend_where_and (query, clause);
      }

      std::string sqlCountQuery{"SELECT COUNT(*) FROM reads "};
      if (!query.where.empty()) {
        sqlCountQuery.append ("WHERE ");
        sqlCountQuery.append (query.where);
        // add newline to avoid issues from user sql
        // e.g. trailing -- comment
        sqlCountQuery.append ("\n");
      }
      sqlCountQuery.append (";");

      // only the new args can be at fault; the rest already compiled
      const auto fail_with_args = [&clause]() {
        std::string reason = sqlite3_errmsg (g_db::conn);
        if (!clause.empty()) {
          reason = fmt::format ("{} - {}", reason, clause);
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

      auto msg = std::string{clause};
      if (curWhere.empty()) {
        msg.append (": ");
      }
      else {
        msg.append (fmt::format (" / {}: ", curWhere));
      }
      msg.append (fmt::format ("{} / {} reads", sqlite3_column_int64 (o_stmt, 0), g_db::nStmtRows));
      return msg;
    }
};

static constexpr auto countAllNames =
    std::to_array<std::string_view> ({"count all", "cta"});
static constexpr auto countAllDesc = std::to_array<std::string_view> ({
    "Count all reads matching <condition>"
});
static constexpr Cmd countAllCmd{
    countAllNames, "<condition>", countAllDesc,
    [] (std::string_view args) -> std::string {
      const auto clause = sanitise_clause(args);
      if (clause.empty()) {
        return cmd_format_misuse (
            "no condition given", countAllCmd.names[0], countAllCmd.usageArgs
        );
      }

      std::string sqlCountQuery = fmt::format("SELECT COUNT(*) FROM reads WHERE {}\n;", clause);

      // only the new args can be at fault; the rest already compiled
      const auto fail_with_args = [&clause]() {
        std::string reason = sqlite3_errmsg (g_db::conn);
        if (!clause.empty()) {
          reason = fmt::format ("{} - {}", reason, clause);
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

      return fmt::format ("{}: {} / {} reads", clause, sqlite3_column_int64 (o_stmt, 0), g_db::totalRows);
    }
};

static constexpr auto clearNames =
    std::to_array<std::string_view> ({"clear", "cl"});
static constexpr auto clearDesc = std::to_array<std::string_view> ({
    "Clear current query, resetting",
    "both WHERE and ORDER BY.",
});
static constexpr Cmd clearCmd{
    clearNames, "", clearDesc,
    [] (std::string_view args) -> std::string {
      if (const auto res = cmd_validate_args_empty (args, clearCmd);
          !res) {
        return res.error();
      };

      auto query = g_db::userQuery;
      query.where.clear();
      query.orderBy.clear();

      return try_apply_query_clause (std::move (query), {}, "Reset query");
    }
};

static constexpr auto toggleNames =
    std::to_array<std::string_view> ({"toggle", "tg"});
static constexpr auto toggleDesc = std::to_array<std::string_view> ({
    "Toggle various aspects of the display.",
    "- quality:   read quality track in",
    "             alignment pane.",
    "- insertion: read insertion track in",
    "             alignment pane.",
    "- table:     browser table pane.",
    "- sparkline: switch quality track",
    "             between Phred ASCII and a",
    "             quick-look sparkline graph.",
    "`toggle` accepts any unambiguous substring",
    "of the argument, e.g. `tg qual`.",
});
static constexpr Cmd toggleCmd{
    toggleNames, "(quality|insertion|table|sparkline)", toggleDesc,
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      if (const auto expectedNTok =
              cmd_validate_ntok (tokens, 1, 1, toggleCmd);
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
          fmt::format ("Unknown option: \"{}\"", tok), toggleCmd.names[0],
          toggleCmd.usageArgs
      );
    }
};

static constexpr auto dumpNames =
    std::to_array<std::string_view> ({"dump"});
static constexpr auto dumpDesc = std::to_array<std::string_view> (
    {"Write the pileup database to an sqlite3 dump file."}
);
static constexpr Cmd dumpCmd{
    dumpNames, "<path>", dumpDesc,
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      if (const auto expectedNTok =
              cmd_validate_ntok (tokens, 1, 1, dumpCmd);
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

enum HelpTopic : uint8_t { topicNav, topicCmd, topicCol, topicCount };
static constexpr std::array<std::string_view, HelpTopic::topicCount>
    helpTopicNames{{
        [HelpTopic::topicNav] = "nav",
        [HelpTopic::topicCmd] = "cmd",
        [HelpTopic::topicCol] = "col",
    }};

static constexpr auto helpNames =
    std::to_array<std::string_view> ({"help", "h", "?"});
static constexpr auto helpDesc = std::to_array<std::string_view> ({
    "Show help for given topic,",
    "or general help with no args.",
});
static constexpr Cmd helpCmd{
    helpNames, "[(nav|cmd|col)]", helpDesc,
    [] (std::string_view args) -> std::string {
      const auto tokens = split_whitespace (args);
      const auto nargRet = cmd_validate_ntok (tokens, 0, 1, helpCmd);
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
            fmt::format ("unknown topic {}", tokens[0]), helpCmd.names[0],
            helpCmd.usageArgs
        );
      }

      if (!size_and_set_overlay_widget (content)) {
        return cmd_format_fail ("terminal too small to display help pane");
      }
      g_ui::showOverlay = true;
      return {};
    }
};

// clang-format on

static constexpr std::span<const Cmd* const> get_cmd_registry()
{
  static constexpr std::array<const Cmd*, 11> registry{{
      /* all cmds must be added here */
      &helpCmd,
      &quitCmd,
      &whereCmd,
      &orderCmd,
      &clearCmd,
      &countCmd,
      &countAllCmd,
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

TEST_CASE ("command descriptions are <= 50 characters in width")
{
  for (const auto* cmd : get_cmd_registry()) {
    for (const auto& line : cmd->desc) {
      if (line.size() > 50) {
        FAIL (
            fmt::format (
                "{}: command has description line exceeding 50 "
                "characters:\n{}",
                cmd->names[0], line
            )
        );
      }
    }
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
