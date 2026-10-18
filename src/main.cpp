#include "jamila/core.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
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
    std::cout << "Commands: /help /new /history /read REL /write REL TEXT /run EXEC ARG... /check-nqr REL /quit\n"
              << "Anything else is sent to your configured OpenAI-compatible chat provider.\n"
              << "Tool operations are local, explicit, and require confirmation for writes/commands.\n";
}
void interactive(jamila::Agent& agent) {
    help();
    std::string line;
    while (std::cout << "\njamila> " << std::flush, std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "/quit" || line == "/exit") break;
        try {
            if (line == "/help") help();
            else if (line == "/new") { agent.clear(); std::cout << "New session.\n"; }
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
        std::string ask;
        for (int i=1; i<argc; ++i) {
            const std::string opt = argv[i];
            if (opt == "--workspace" && i + 1 < argc) cfg.workspace = argv[++i];
            else if (opt == "--ask" && i + 1 < argc) ask = argv[++i];
            else if (opt == "--gui") gui = true;
            else if (opt == "--help") { std::cout << "jamila-game [--workspace PATH] [--ask PROMPT] [--gui]\n"; return 0; }
            else throw std::invalid_argument("unknown/missing option: " + opt);
        }
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
