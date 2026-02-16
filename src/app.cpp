#include <format>
#include <unordered_map>

#include "app.hpp"

namespace app {

CmdResult cmd_quit (Context& ctx) {
    ctx.run = false;
    return {true, "Bye!"};
}


static std::unordered_map <std::string_view, Cmd> CMD_REGISTRY{
    {"q", &cmd_quit},
    {"quit", &cmd_quit}
};
CmdResult exec_cmd (std::string_view cmd_name, Context& ctx) {
    if (auto it = CMD_REGISTRY.find(cmd_name); it != CMD_REGISTRY.end()) {
        return it->second(ctx);
    } else {
        return {false, std::format("Command \"{}\" not found!", cmd_name)};
    }
}

}
