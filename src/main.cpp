#include "jamila/core.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <vector>

namespace fs = std::filesystem;
#ifdef JAMILA_WITH_X11
void launch_window(jamila::Agent& agent);
#endif
namespace {
bool approve(const std::string& action) {
    std::cout << "Approval required: " << action << "\nType YES to approve: " << std::flush;
    std::string line;
    return std::getline(std::cin, line) && line == "YES";
}
void help() {
    std::cout << "Commands: /help /new /history /doctor /tools /activity /tool NAME JSON /read REL /write REL TEXT /run EXEC ARG... /check-nqr REL /quit\n"
              << "Anything else uses your chat provider and, if connected, discovers Godot MCP tools.\n"
              << "Tool operations are local, explicit, and require confirmation for writes/commands.\n";
}
void interactive(jamila::Agent& agent) {
    agent.set_approval([](const std::string& name, const std::string& args) {
        return approve("Godot MCP " + name + " with args: " + args.substr(0, 1200));
    });
    help();
    std::string line;
    while (std::cout << "\njamila> " << std::flush, std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "/quit" || line == "/exit") break;
        try {
            if (line == "/help") help();
            else if (line == "/new") { agent.clear(); std::cout << "New session.\n"; }
            else if (line == "/doctor") {
                std::cout << "Godot project: " << (fs::exists(agent.config().workspace / "project.godot") ? "detected" : "not detected")
                          << "\nGodot MCP: " << (agent.has_mcp() ? "connected" : "not connected")
                          << "\nDiscovered tools: " << agent.mcp_tools().size() << "\n";
            }
            else if (line == "/tools") {
                if (!agent.has_mcp()) std::cout << "Not connected. Restart with --godot-mcp.\n";
                for (auto& t : agent.mcp_tools()) std::cout << " - " << t.name << "  " << t.description.substr(0, 115) << "\n";
            }
            else if (line == "/activity") { for (auto& event : agent.activity()) std::cout << event << "\n"; }
            else if (line.rfind("/tool ",0)==0) {
                auto end = line.find(' ',6);
                if (end==std::string::npos) throw std::invalid_argument("usage: /tool NAME JSON");
                std::cout << agent.call_mcp(line.substr(6,end-6),line.substr(end+1)) << "\n";
            }
            else if (line == "/history") { for (const auto& msg : agent.history()) std::cout << msg.role << ": " << msg.content << "\n"; }
            else if (line.rfind("/read ", 0) == 0) std::cout << jamila::read_text(agent.config().workspace, line.substr(6)) << "\n";
            else if (line.rfind("/write ", 0) == 0) {
                const auto split = line.find(' ', 7);
                if (split == std::string::npos) throw std::invalid_argument("usage: /write REL TEXT");
                const auto rel = line.substr(7, split - 7);
                auto dest = jamila::resolve_in_workspace(agent.config().workspace, rel);
                if (approve("write " + dest.string())) { jamila::write_text(agent.config().workspace, rel, line.substr(split + 1)); std::cout << "Written.\n"; }
                else std::cout << "Cancelled.\n";
            } else if (line.rfind("/run ", 0) == 0 || line.rfind("/check-nqr ", 0) == 0) {
                std::vector<std::string> argv;
                if (line.rfind("/check-nqr ", 0) == 0) {
                    const auto path = jamila::resolve_in_workspace(agent.config().workspace, line.substr(11));
                    argv = {"noqeri", "check", path.string()};
                } else {
                    std::istringstream stream(line.substr(5));
                    for (std::string word; stream >> std::quoted(word);) argv.push_back(word);
                }
                if (argv.empty()) throw std::invalid_argument("executable required");
                if (approve("execute " + line)) std::cout << "Exit status " << jamila::execute_argv(agent.config().workspace, argv) << "\n";
                else std::cout << "Cancelled.\n";
            } else std::cout << agent.ask(line) << "\n";
        } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; }
    }
}
}
int main(int argc, char** argv) {
    try {
        jamila::Config cfg;
        cfg.workspace = fs::current_path();
        bool gui = false;
        bool godot_mcp = false;
        std::string ask;
        for (int i=1; i<argc; ++i) {
            const std::string opt = argv[i];
            if (opt == "--workspace" && i + 1 < argc) cfg.workspace = argv[++i];
            else if (opt == "--ask" && i + 1 < argc) ask = argv[++i];
            else if (opt == "--gui") gui = true;
            else if (opt == "--godot-mcp") godot_mcp = true;
            else if (opt == "--mcp-bin" && i + 1 < argc) {
                godot_mcp = true;
                cfg.mcp_command.clear();
                cfg.mcp_command.push_back(argv[++i]);
            }
            else if (opt == "--mcp-arg" && i + 1 < argc) {
                godot_mcp = true;
                if (cfg.mcp_command.empty()) throw std::invalid_argument("--mcp-bin must precede --mcp-arg");
                cfg.mcp_command.push_back(argv[++i]);
            }
            else if (opt == "--help") { std::cout << "jamila-game [--workspace GODOT_PROJECT] [--ask PROMPT] [--gui] [--godot-mcp]\n" << "             [--mcp-bin SERVER_EXECUTABLE [--mcp-arg ARG] ...]\n"; return 0; }
            else throw std::invalid_argument("unknown/missing option: " + opt);
        }
        if (godot_mcp && cfg.mcp_command.empty()) cfg.mcp_command = {"npx", "-y", "@coding-solo/godot-mcp@0.1.1"};
        jamila::Agent agent(std::move(cfg));
        if (!ask.empty()) std::cout << agent.ask(ask) << '\n';
        else if (gui) {
#ifdef JAMILA_WITH_X11
            launch_window(agent);
#else
            throw std::runtime_error("GUI not built: X11 development package unavailable");
#endif
        } else interactive(agent);
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
