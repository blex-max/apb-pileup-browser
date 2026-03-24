#include "cmd.hpp"

#include <format>
#include <unordered_map>

#include "GlobalContext.hpp"
#include "PileupContext.hpp"

namespace cmd {

std::pair<std::string_view, std::string_view>
split_first_space (std::string_view s) {
    if (s.empty()) {
        return {};
    }
    auto pos = s.find(' ');
    if (pos == std::string_view::npos) {
        return {s, {}}; // no args
    }
    return {
        s.substr(0, pos),
        s.substr(pos + 1)
    };
}

std::vector<std::string_view>
split_whitespace (std::string_view s) {
    std::vector<std::string_view> out;
    auto [f, rest] = split_first_space(s);
    while (!f.empty()) {
        out.push_back(f);
        const auto tmp = split_first_space(rest);
        f = tmp.first;
        rest = tmp.second;
    }
    return out;
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

CmdResult pileup_show (std::string_view names) {
    auto& show_requested = ctx::get<PileupContext>().config.bam_props_request;

    // split args
    const auto new_reqs = split_whitespace(names);
    if (new_reqs.empty()) {
        return {false, "needs args"};
    }

    for (const auto& req : new_reqs) {
        const auto& it = BAM_RENDER_CALLBACKS.find(req);
        if (it == BAM_RENDER_CALLBACKS.end()) {
            return {false, std::format ("Cannot show unknown property \"{}\"", req)};
        }
        show_requested.emplace(req);
    }

    PLOGD << std::format("User requesting to view properties: {}", show_requested);

    return {true, std::format ("Showing query properties {}", names)};
}
CmdResult pileup_hide (std::string_view names) {
    auto& show_requested = ctx::get<PileupContext>().config.bam_props_request;

    // split args
    const auto new_reqs = split_whitespace(names);
    if (new_reqs.empty()) {
        return {false, "needs args"};
    }

    for (const auto& req: new_reqs) {
        // noop if not found
        show_requested.erase(std::string{req});   // convert to string...
    }

    PLOGD << std::format("User requesting to view properties: {}", show_requested);

    return {true, std::format ("Hiding query properties {}", names)};
}

static std::unordered_map<std::string_view, CmdResult(*)(std::string_view)> CMD_REGISTRY{
    {"q", &quit},
    {"quit", &quit},
    {"debug-show", &debug_show},
    {"debug-hide", &debug_hide},
    {"show", &pileup_show},
    {"hide", &pileup_hide}
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
