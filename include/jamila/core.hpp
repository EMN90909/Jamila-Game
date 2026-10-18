#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace jamila {
struct Message { std::string role; std::string content; };
struct Config {
    std::filesystem::path workspace;
    std::string api_url;
    std::string api_key;
    std::string model;
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
    void clear() { history_.clear(); }
    const std::vector<Message>& history() const { return history_; }
    const Config& config() const { return config_; }
private:
    Config config_;
    std::vector<Message> history_;
};
}
