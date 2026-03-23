#include "cmd.hpp"

#include <format>
#include <unordered_map>

#include "GlobalContext.hpp"

namespace cmd {

std::pair<std::string_view, std::string_view>
split_first_space (std::string_view s) {
    auto pos = s.find(' ');
    if (pos == std::string_view::npos) {
        return {s, {}}; // no args
    }
    return {
        s.substr(0, pos),
        s.substr(pos + 1)
    };
}

std::string debug_print_frame () {
  auto frame = ctx::get<GlobalContext>().data.frame;
  return std::format("frame: {}", frame);
}
static std::unordered_map<std::string_view, std::string(*)()> DEBUG_CB_REG {
  {"frame", debug_print_frame}
};


// Commands
CmdResult quit (std::string_view) {
    auto& gdata= ctx::get<GlobalContext>().data;
    gdata.run = false;
    return {true, "Bye!"};
}
CmdResult debug_show (std::string_view name) {
    auto& debug = ctx::get<GlobalContext>().data.debug;
    if (auto it = DEBUG_CB_REG.find(name); it != DEBUG_CB_REG.end()) {
        debug.try_emplace(it->first, it->second);
        return {true, std::format ("Showing debug information for \"{}\"", name)};
    }
    return {false, std::format ("Cannot display debug information for \"{}\"", name)};
}
CmdResult debug_hide (std::string_view name) {
    // works in principle, but note that
    // the text hangs around until drawn over right now
    // as it is not cleared!
    auto& debug = ctx::get<GlobalContext>().data.debug;
    if (auto it = debug.find(name); it != debug.end()) {
        debug.erase(it);
        return {true, std::format ("Hiding debug display for \"{}\"", name)};
    }
    return {false, std::format ("Cannot hide debug information for \"{}\"", name)};
}
static std::unordered_map<std::string_view, CmdResult(*)(std::string_view)> CMD_REGISTRY{
    {"q", &quit},
    {"quit", &quit},
    {"debug-show", &debug_show},
    {"debug-hide", &debug_hide}
};

CmdResult exec_cmd (std::string_view call) {
    auto [name, args] = split_first_space(call);
    if (auto it = CMD_REGISTRY.find(name); it != CMD_REGISTRY.end()) {
        return it->second(args);
    } else {
        return {false, std::format("Command \"{}\" not found!", name)};
    }
}

}
