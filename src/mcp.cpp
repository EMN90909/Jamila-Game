#include "jamila/mcp.hpp"
#include <json-c/json.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace jamila {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
constexpr size_t MAX_MESSAGE = 2 * 1024 * 1024;
json_object* field(json_object* obj, const char* key) {
    json_object* value = nullptr;
    if (obj) json_object_object_get_ex(obj, key, &value);
    return value;
}
std::string json_text(json_object* o) {
    return o ? json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN) : "null";
}
std::string string_field(json_object* obj, const char* key) {
    auto* o = field(obj, key);
    return o && json_object_is_type(o, json_type_string) ? json_object_get_string(o) : "";
}
Json parse(const std::string& s) {
    json_tokener* tok = json_tokener_new();
    json_object* p = json_tokener_parse_ex(tok, s.data(), static_cast<int>(s.size()));
    const bool valid = json_tokener_get_error(tok) == json_tokener_success &&
        static_cast<size_t>(tok->char_offset) == s.size();
    json_tokener_free(tok);
    if (!valid || !p) { if (p) json_object_put(p); throw std::runtime_error("invalid MCP JSON message"); }
    return Json(p, json_object_put);
}
void close_fd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }
}
McpClient::McpClient(const std::filesystem::path& workspace, const std::vector<std::string>& argv,
                     const std::map<std::string, std::string>& env) {
    if (argv.empty() || argv.front().empty() || argv.size() > 32)
        throw std::invalid_argument("MCP executable and at most 31 arguments required");
    int to_child[2], from_child[2];
    if (::pipe(to_child) != 0) throw std::runtime_error("MCP pipe failed");
    if (::pipe(from_child) != 0) {
        ::close(to_child[0]); ::close(to_child[1]);
        throw std::runtime_error("MCP pipe failed");
    }
    pid_t child = ::fork();
    if (child < 0) {
        for (int fd : to_child) ::close(fd);
        for (int fd : from_child) ::close(fd);
        throw std::runtime_error("MCP fork failed");
    }
    if (child == 0) {
        ::close(to_child[1]); ::close(from_child[0]);
        if (::dup2(to_child[0], STDIN_FILENO) < 0 || ::dup2(from_child[1], STDOUT_FILENO) < 0 ||
            ::chdir(workspace.c_str()) != 0) _exit(126);
        ::close(to_child[0]); ::close(from_child[1]);
        // Third-party servers may log on stderr, never mix it with protocol stdout.
        for (const auto& [name, value] : env) {
            if (::setenv(name.c_str(), value.c_str(), 1) != 0) _exit(125);
        }
        std::vector<char*> args;
        for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);
        ::execvp(args[0], args.data());
        _exit(127);
    }
    ::signal(SIGPIPE, SIG_IGN); // A crashed server must not terminate the native client.
    pid_ = child;
    read_fd_ = from_child[0]; write_fd_ = to_child[1];
    ::close(to_child[0]); ::close(from_child[1]);
    try {
        const std::string init = request("initialize",
            R"({"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"jamila-game","version":"0.2.0"}})", 25000);
        auto parsed = parse(init);
        if (string_field(parsed.get(), "protocolVersion").empty())
            throw std::runtime_error("MCP server missing protocolVersion");
        send(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
        discover();
    } catch (...) {
        close_fd(write_fd_); close_fd(read_fd_);
        ::kill(child, SIGTERM); ::waitpid(child, nullptr, 0); pid_ = -1;
        throw;
    }
}
McpClient::~McpClient() {
    close_fd(write_fd_); close_fd(read_fd_);
    if (pid_ > 0) {
        int status = 0;
        if (::waitpid(pid_, &status, WNOHANG) == 0) {
            ::kill(pid_, SIGTERM);
            for (int i = 0; i < 20; ++i) {
                if (::waitpid(pid_, &status, WNOHANG) != 0) return;
                ::usleep(10000);
            }
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
        }
    }
}
void McpClient::send(const std::string& json) {
    if (json.size() > MAX_MESSAGE) throw std::runtime_error("MCP outgoing message too large");
    std::string line = json + "\n";
    size_t pos = 0;
    while (pos < line.size()) {
        struct pollfd p{write_fd_, POLLOUT, 0};
        if (::poll(&p, 1, 10000) <= 0 || (p.revents & (POLLERR | POLLHUP | POLLNVAL)))
            throw std::runtime_error("MCP process not accepting input");
        const ssize_t n = ::write(write_fd_, line.data() + pos, line.size() - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("MCP write failure");
        pos += static_cast<size_t>(n);
    }
}
std::string McpClient::receive(int id, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto nl = incoming_.find('\n');
        if (nl != std::string::npos) {
            std::string line = incoming_.substr(0, nl);
            incoming_.erase(0, nl + 1);
            auto msg = parse(line);
            json_object* id_obj = field(msg.get(), "id");
            if (!id_obj || !json_object_is_type(id_obj, json_type_int) || json_object_get_int(id_obj) != id)
                continue; // MCP notifications and unrelated messages are not responses.
            if (auto* err = field(msg.get(), "error"))
                throw std::runtime_error("MCP JSON-RPC error: " + json_text(err).substr(0, 700));
            json_object* result = field(msg.get(), "result");
            if (!result) throw std::runtime_error("MCP response lacks result");
            return json_text(result);
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        struct pollfd p{read_fd_, POLLIN, 0};
        int rc = ::poll(&p, 1, static_cast<int>(std::max<int64_t>(1, ms)));
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) throw std::runtime_error("MCP poll failed");
        if (rc == 0) break;
        char buffer[8192];
        const ssize_t n = ::read(read_fd_, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("MCP server closed communication");
        if (incoming_.size() + static_cast<size_t>(n) > MAX_MESSAGE)
            throw std::runtime_error("MCP incoming message exceeds 2 MiB");
        incoming_.append(buffer, static_cast<size_t>(n));
    }
    throw std::runtime_error("MCP tool timed out");
}
std::string McpClient::request(const std::string& method, const std::string& params_json, int timeout_ms) {
    const int id = next_id_++;
    auto params = parse(params_json);
    Json msg(json_object_new_object(), json_object_put);
    json_object_object_add(msg.get(), "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(msg.get(), "id", json_object_new_int(id));
    json_object_object_add(msg.get(), "method", json_object_new_string(method.c_str()));
    json_object_object_add(msg.get(), "params", json_object_get(params.get()));
    send(json_text(msg.get()));
    return receive(id, timeout_ms);
}
void McpClient::discover() {
    std::string cursor;
    for (int page = 0; page < 12; ++page) {
        Json params(json_object_new_object(), json_object_put);
        if (!cursor.empty()) json_object_object_add(params.get(), "cursor", json_object_new_string(cursor.c_str()));
        auto response = parse(request("tools/list", json_text(params.get()), 25000));
        auto* list = field(response.get(), "tools");
        if (!list || !json_object_is_type(list, json_type_array))
            throw std::runtime_error("MCP tools/list missing tools array");
        for (size_t i = 0; i < json_object_array_length(list); ++i) {
            auto* obj = json_object_array_get_idx(list, i);
            auto name = string_field(obj, "name");
            if (name.empty()) continue;
            auto* schema = field(obj, "inputSchema");
            if (!schema || !json_object_is_type(schema, json_type_object)) continue;
            if (tools_.size() >= 128) throw std::runtime_error("MCP tool catalog too large (>128)");
            tools_.push_back({name, string_field(obj, "description").substr(0, 1400), json_text(schema)});
        }
        cursor = string_field(response.get(), "nextCursor");
        if (cursor.empty()) return;
    }
    throw std::runtime_error("MCP tools/list pagination limit exceeded");
}
std::string McpClient::call(const std::string& name, const std::string& arguments_json) {
    const auto found = std::find_if(tools_.begin(), tools_.end(), [&](const McpTool& t){ return t.name == name; });
    if (found == tools_.end()) throw std::invalid_argument("MCP tool not advertised: " + name);
    auto args = parse(arguments_json);
    if (!json_object_is_type(args.get(), json_type_object))
        throw std::invalid_argument("MCP tool arguments must be JSON object");
    Json params(json_object_new_object(), json_object_put);
    json_object_object_add(params.get(), "name", json_object_new_string(name.c_str()));
    json_object_object_add(params.get(), "arguments", json_object_get(args.get()));
    return request("tools/call", json_text(params.get()), 90000);
}
}