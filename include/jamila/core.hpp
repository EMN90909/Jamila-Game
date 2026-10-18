#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include "jamila/mcp.hpp"
#include <string>
#include <vector>

namespace jamila {
struct Message { std::string role; std::string content; };
enum class ApprovalMode { Human, AllowAll };
struct McpServerConfig {
    std::string name;
    std::vector<std::string> command;
    std::map<std::string, std::string> env; // values supplied by process environment, not saved secrets
    std::vector<std::string> include_tools; // optional allowlist of remote tool names
};
struct Config {
    std::filesystem::path workspace;
    std::string api_url;
    std::string api_key;
    std::string model;
    std::vector<std::string> mcp_command; // legacy single-server flag
    std::vector<McpServerConfig> mcp_servers;
    ApprovalMode approval_mode = ApprovalMode::Human;
    std::filesystem::path settings_file;
};

std::filesystem::path resolve_in_workspace(const std::filesystem::path& root, const std::filesystem::path& relative);
std::string read_text(const std::filesystem::path& root, const std::filesystem::path& relative);
void write_text(const std::filesystem::path& root, const std::filesystem::path& relative, const std::string& text);
int execute_argv(const std::filesystem::path& cwd, const std::vector<std::string>& args);
unsigned long long fingerprint(const std::string& text);

class Agent {
public:
    explicit Agent(Config config);
    std::string ask(const std::string& prompt);
    // The caller must provide an approval callback before mutating/executing MCP tools.
    void set_approval(std::function<bool(const std::string&, const std::string&)> callback) { approval_ = std::move(callback); }
    bool has_mcp() const { return !servers_.empty(); }
    size_t server_count() const { return servers_.size(); }
    const std::vector<std::string>& connection_errors() const { return connection_errors_; }
    ApprovalMode approval_mode() const { return config_.approval_mode; }
    void set_approval_mode(ApprovalMode mode);
    const std::vector<McpTool>& mcp_tools() const;
    std::string call_mcp(const std::string& name, const std::string& arguments_json);
    const std::vector<std::string>& activity() const { return activity_; }
    void clear() { history_.clear(); }
    const std::vector<Message>& history() const { return history_; }
    const Config& config() const { return config_; }
private:
    Config config_;
    std::vector<Message> history_;
    struct Server { std::string name; std::unique_ptr<McpClient> client; };
    std::vector<Server> servers_;
    std::vector<McpTool> exposed_tools_;
    std::map<std::string, std::pair<size_t, std::string>> routing_;
    std::vector<std::string> connection_errors_;
    std::function<bool(const std::string&, const std::string&)> approval_;
    std::vector<std::string> activity_;
    std::string ask_godot(const std::string& prompt);
    bool authorize(const std::string& exposed_name, const std::string& arguments_json);
};
// Settings contain only approval mode, never credentials. A config file's MCP servers
// are started ONLY when explicitly selected with --mcp-config.
ApprovalMode load_approval_mode(const std::filesystem::path& settings_file);
std::vector<McpServerConfig> load_mcp_servers(const std::filesystem::path& config_file);
std::filesystem::path default_settings_path();
}