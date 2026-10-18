#pragma once
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace jamila {
struct McpTool {
    std::string name;
    std::string description;
    std::string input_schema; // JSON Schema object, as supplied by the MCP server
};

// Owns one local stdio MCP process. No shell is ever involved in spawning it.
class McpClient {
public:
    McpClient(const std::filesystem::path& workspace, const std::vector<std::string>& argv,
              const std::map<std::string, std::string>& env = {});
    ~McpClient();
    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;
    const std::vector<McpTool>& tools() const { return tools_; }
    // JSON text representing a MCP CallToolResult; throws on transport/JSON-RPC failure.
    std::string call(const std::string& name, const std::string& arguments_json);
private:
    int read_fd_ = -1;
    int write_fd_ = -1;
    int pid_ = -1;
    int next_id_ = 1;
    std::string incoming_;
    std::vector<McpTool> tools_;
    void send(const std::string& json);
    std::string receive(int id, int timeout_ms);
    std::string request(const std::string& method, const std::string& params_json, int timeout_ms);
    void discover();
};
}