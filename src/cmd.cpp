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


// Commands
CmdResult quit (std::string_view) {
    ctx::get<GlobalContext>().conf.run = false;
    return {true, "Bye!"};
}
CmdResult debug_show (std::string_view names) {
    auto& show_requested = ctx::get<GlobalContext>().conf.debug_request;

    // split args
    const auto new_reqs = split_whitespace(names);
    if (new_reqs.empty()) {
        return {false, "needs args"};
    }

    for (const auto& req : new_reqs) {
        const auto& it = DEBUG_CALLBACKS.find(req);
        if (it == DEBUG_CALLBACKS.end()) {
            return {false, std::format ("Cannot show unknown debug property \"{}\"", req)};
        }
        // don't duplicate
        if (*std::find(begin(show_requested), end(show_requested), req) == req) {
            continue;
        }
        show_requested.emplace_back(req);
    }

    PLOGD << std::format("User requesting to view debug properties: {}", new_reqs);
    return {true, std::format ("Showing debug properties: {}", new_reqs)};
}
CmdResult debug_hide (std::string_view names) {
    // works in principle, but note that
    // the text hangs around until drawn over right now
    // as it is not cleared!
    auto& show_requested = ctx::get<GlobalContext>().conf.debug_request;

    // split args
    const auto new_reqs = split_whitespace(names);
    if (new_reqs.empty()) {
        return {false, "needs args"};
    }

    for (const auto& req: new_reqs) {
        // noop if not found
        show_requested.remove(std::string{req});   // convert to string...
    }

    PLOGD << std::format("User requesting to hide debug properties: {}", new_reqs);
    return {true, std::format ("Hiding debug properties: {}", new_reqs)};
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
        // don't duplicate
        if (*std::find(begin(show_requested), end(show_requested), req) == req) {
            continue;
        }
        show_requested.emplace_back(req);
    }

    PLOGD << std::format("User requesting to view properties: {}", show_requested);
    return {true, std::format ("Showing query properties: {}", new_reqs)};
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
        show_requested.remove(std::string{req});  // convert from string_view
    }

    PLOGD << std::format("User requesting to hide properties: {}", new_reqs);
    return {true, std::format ("Hiding query properties {}", new_reqs)};
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
