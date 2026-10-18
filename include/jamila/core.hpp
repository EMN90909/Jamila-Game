#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include "jamila/mcp.hpp"
#include <string>
#include <vector>

namespace jamila {
struct Message { std::string role; std::string content; };
struct Config {
    std::filesystem::path workspace;
    std::string api_url;
    std::string api_key;
    std::string model;
    std::vector<std::string> mcp_command;  // explicit opt-in local Godot MCP server argv
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
    bool has_mcp() const { return static_cast<bool>(mcp_); }
    const std::vector<McpTool>& mcp_tools() const;
    std::string call_mcp(const std::string& name, const std::string& arguments_json);
    const std::vector<std::string>& activity() const { return activity_; }
    void clear() { history_.clear(); }
    const std::vector<Message>& history() const { return history_; }
    const Config& config() const { return config_; }
private:
    Config config_;
    std::vector<Message> history_;
    std::unique_ptr<McpClient> mcp_;
    std::function<bool(const std::string&, const std::string&)> approval_;
    std::vector<std::string> activity_;
    std::string ask_godot(const std::string& prompt);
    bool authorize(const std::string& name, const std::string& arguments_json);
};
}
