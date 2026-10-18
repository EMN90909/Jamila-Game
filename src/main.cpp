#include "jamila/core.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <vector>
#include <algorithm>

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
    std::cout << "Commands: /help /new /history /doctor /tools /activity /settings /approval human|allow-all /team on|off /skills /tool NAME JSON /read REL /write REL TEXT /run EXEC ARG... /check-nqr REL /quit\n"
              << "Anything else uses your chat provider and, if connected, discovers Godot MCP tools.\n"
              << "Tool operations are local, explicit, and require confirmation for writes/commands.\n";
}
void doctor(jamila::Agent& agent) {
    const auto& root=agent.config().workspace;
    std::cout << "Workspace: " << root << "\n";
    std::cout << "Godot project.godot: " << (fs::is_regular_file(root/"project.godot")?"found":"MISSING") << "\n";
    std::cout << "Connected MCP processes: " << agent.server_count() << "\n";
    std::cout << "Advertised MCP and native tools: " << agent.mcp_tools().size() << "\n";
    std::cout << "Local Skills: " << agent.skills().size() << "\n";
    std::cout << "Parallel advisors: " << (agent.team_mode()?"on":"off") << "\n";
    for(const auto& e:agent.connection_errors())std::cout << "Connection failure: " << e << "\n";
    std::string godot_tool;
    for(const auto& tool:agent.mcp_tools()) {
        if(tool.name=="get_godot_version" || tool.name=="godot__get_godot_version")godot_tool=tool.name;
    }
    if(!godot_tool.empty()){
        std::cout << "Godot version MCP probe (real server call): ";
        try{
            auto result=agent.call_mcp(godot_tool,"{}");
            std::cout << result.substr(0,600) << "\n";
        }catch(const std::exception& e){std::cout << "FAILED: " << e.what() << "\n";}
    }else std::cout << "Godot MCP live version probe: unavailable (not connected or tool not exposed)\n";
    std::cout << "Note: MCP discovery != authenticated GitHub/Claude access or running Godot playtests.\n";
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
            else if (line == "/settings") {
                std::cout << "Approval mode: " << (agent.approval_mode()==jamila::ApprovalMode::Human ? "Human Approval" : "Allow All")
                          << "\nConnected MCP servers: " << agent.server_count() << "\n";
            }
            else if (line == "/approval human" || line == "/approval allow-all") {
                if (line=="/approval allow-all") {
                    if (!approve("Enable ALLOW ALL? Connected MCP servers can modify files, execute processes and access external APIs without future prompts.")) {
                        std::cout << "Unchanged.\n"; continue;
                    }
                }
                agent.set_approval_mode(line=="/approval human" ? jamila::ApprovalMode::Human : jamila::ApprovalMode::AllowAll);
                std::cout << "Approval settings saved.\n";
            }
            else if (line == "/doctor") doctor(agent);
            else if (line == "/tools") {
                if (!agent.has_mcp()) std::cout << "Not connected. Use --godot-mcp or --mcp-config.\n";
                for (auto& t : agent.mcp_tools()) std::cout << " - " << t.name << "  " << t.description.substr(0, 115) << "\n";
            }
            else if (line == "/skills") {
                for(const auto& skill:agent.skills()) std::cout << skill.name << " : " << skill.description << "\n";
            }
            else if (line == "/team on" || line == "/team off") {
                agent.set_team_mode(line=="/team on");
                std::cout << (agent.team_mode()?"Parallel advisors enabled":"Parallel advisors disabled") << "\n";
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
        bool diagnostic = false;
        bool godot_mcp = false;
        std::string ask;
        std::string mcp_config;
        std::string approval_override;
        for (int i=1; i<argc; ++i) {
            const std::string opt = argv[i];
            if (opt == "--workspace" && i + 1 < argc) cfg.workspace = argv[++i];
            else if (opt == "--ask" && i + 1 < argc) ask = argv[++i];
            else if (opt == "--gui") gui = true;
            else if (opt == "--doctor") diagnostic = true;
            else if (opt == "--team") cfg.team_mode = true;
            else if (opt == "--provider" && i+1<argc) cfg.provider=argv[++i];
            else if (opt == "--no-team") cfg.team_mode = false;
            else if (opt == "--godot-mcp") godot_mcp = true;
            else if (opt == "--mcp-config" && i+1<argc) mcp_config=argv[++i];
            else if (opt == "--approval-mode" && i+1<argc) approval_override=argv[++i];
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
            else if (opt == "--help") { std::cout << "jamila-game [--workspace GODOT_PROJECT] [--ask PROMPT] [--gui] [--godot-mcp]\n"
                    << "             [--mcp-config servers.json] [--provider openai|anthropic] [--approval-mode human|allow-all] [--team] [--doctor]\n"
                    << "             [--mcp-bin SERVER_EXECUTABLE [--mcp-arg ARG] ...]\n"; return 0; }
            else throw std::invalid_argument("unknown/missing option: " + opt);
        }
        cfg.settings_file=jamila::default_settings_path();
        cfg.approval_mode=jamila::load_approval_mode(cfg.settings_file);
        if (!mcp_config.empty()) cfg.mcp_servers=jamila::load_mcp_servers(mcp_config);
        if (godot_mcp && cfg.mcp_command.empty()) {
            bool has_godot=false;
            for (const auto& server:cfg.mcp_servers) if(server.name=="godot") has_godot=true;
            if (!has_godot) cfg.mcp_servers.push_back({"godot",{"npx","-y","@coding-solo/godot-mcp@0.1.1"},{}});
        }
        if (!approval_override.empty()) {
            if (approval_override!="human" && approval_override!="allow-all") throw std::invalid_argument("approval mode must be human or allow-all");
            cfg.approval_mode=approval_override=="human" ? jamila::ApprovalMode::Human : jamila::ApprovalMode::AllowAll;
        }
        jamila::Agent agent(std::move(cfg));
        if (diagnostic) doctor(agent);
        else if (!ask.empty()) std::cout << agent.ask(ask) << '\n';
        else if (gui) {
#ifdef JAMILA_WITH_X11
            launch_window(agent);
#else
            throw std::runtime_error("GUI not built: X11 development package unavailable");
#endif
        } else interactive(agent);
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}