#include "cmd.hpp"

#include <format>
#include <functional>
#include <unordered_map>

static std::string debug_print_frame (const AppState& state)
{
  return std::format ("frame: {}", state.mData.c_frame);
}
static std::unordered_map<
    std::string_view,
    std::function<std::string (AppState& state)>>
    DEBUG_CALLBACKS{{"frame", debug_print_frame}};

std::optional<std::string> get_debug_text (
    std::string_view name, AppState& state
)
{
  auto it = DEBUG_CALLBACKS.find (name);
  if (it == DEBUG_CALLBACKS.end()) {
    return std::nullopt;
  }
  return it->second (state);
}

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
// CmdResult debug_show (std::string_view names, AppState& state)
// {
//   auto& show_requested =
//       state.conf.debug_request;

//     // split args
//   const auto new_reqs = split_whitespace (names);
//   if (new_reqs.empty()) {
//     return {false, "needs args"};
//   }

//   for (const auto& req : new_reqs) {
//     const auto& it = DEBUG_CALLBACKS.find (req);
//     if (it == DEBUG_CALLBACKS.end()) {
//       return {
//           false, std::format (
//                      "Cannot show unknown "
//                      "debug property \"{}\"",
//                      req
//                  )
//       };
//     }
//     if (std::find (
//             begin (show_requested), end (show_requested), req
//         ) != end (show_requested)) {
//       continue;
//     }
//     show_requested.emplace_back (req);
//   }

//   PLOGD << std::format (
//       "User requesting to view debug properties: "
//       "{}",
//       new_reqs
//   );
//   return {
//       true,
//       std::format ("Showing debug properties: {}", new_reqs)
//   };
// }
// CmdResult debug_hide (std::string_view names, AppState& state)
// {
//     // works in principle, but note that
//   // the text hangs around until drawn over right now
//   // as it is not cleared!
//   auto& show_requested =
//       state.conf.debug_request;

//   // split args
//   const auto new_reqs = split_whitespace (names);
//   if (new_reqs.empty()) {
//     return {false, "needs args"};
//   }

//   for (const auto& req : new_reqs) {
//     // noop if not found
//     show_requested.remove (
//         std::string{req}
//     );  // convert to string...
//   }

//   PLOGD << std::format (
//       "User requesting to hide debug properties: "
//       "{}",
//       new_reqs
//   );
//   return {
//       true, std::format ("Hiding debug properties: {}", new_reqs)
//   };
// }

// CmdResult pileup_show (std::string_view names)
// {
//   auto& show_requested =
//       ctx::get<PileupContext>().config.bam_props_request;

//   // split args
//   const auto new_reqs = split_whitespace (names);
//   if (new_reqs.empty()) {
//     return {false, "needs args"};
//   }

//   for (const auto& req : new_reqs) {
//     auto cb = get_pileup_text_callback (req);
//     if (!cb) {
//       return {
//           false, std::format (
//                      "Cannot show unknown "
//                      "property \"{}\"",
//                      req
//                  )
//       };
//     }
//     auto already = std::find_if (
//         begin (show_requested), end (show_requested),
//         [&req] (const auto& p) { return p.name == req; }
//     );
//     if (already != end (show_requested)) {
//       continue;
//     }
//     show_requested.push_back ({std::string{req}, cb});
//   }

//   PLOGD << std::format (
//       "User requesting to view properties: {}", new_reqs
//   );
//   return {
//       true,
//       std::format ("Showing query properties: {}", new_reqs)
//   };
// }
// CmdResult pileup_hide (std::string_view names)
// {
//   auto& show_requested =
//       ctx::get<PileupContext>().config.bam_props_request;

//   // split args
//   const auto new_reqs = split_whitespace (names);
//   if (new_reqs.empty()) {
//     return {false, "needs args"};
//   }

//   for (const auto& req : new_reqs) {
//     show_requested.remove_if ([&req] (const auto& p) {
//       return p.name == req;
//     });
//   }

//   PLOGD << std::format (
//       "User requesting to hide properties: {}", new_reqs
//   );
//   return {
//       true, std::format ("Hiding query properties {}", new_reqs)
//   };
// }

static std::unordered_map<
    std::string_view,
    std::function<CmdResult (std::string_view, AppState&)>>
    CMD_REGISTRY{
        {"q", &quit},
        {"quit", &quit},
        // {"debug-show", &debug_show},
        // {"debug-hide", &debug_hide},
        // {"show", &pileup_show},
        // {"hide", &pileup_hide}
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
