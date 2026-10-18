#include "jamila/core.hpp"
#include <json-c/json.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <regex>
#include <stdexcept>

namespace jamila {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
Json read_json(const std::filesystem::path& file) {
    if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("configuration is not a regular file");
    if (std::filesystem::file_size(file)>256*1024) throw std::runtime_error("configuration too large");
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot read configuration");
    std::string contents((std::istreambuf_iterator<char>(in)),{});
    json_tokener* tok=json_tokener_new();
    auto* ptr=json_tokener_parse_ex(tok,contents.data(),static_cast<int>(contents.size()));
    bool ok=json_tokener_get_error(tok)==json_tokener_success &&
        static_cast<size_t>(tok->char_offset)==contents.size();
    json_tokener_free(tok);
    if (!ptr || !ok || !json_object_is_type(ptr,json_type_object)) {
        if (ptr) json_object_put(ptr);
        throw std::runtime_error("configuration must contain a JSON object");
    }
    return Json(ptr,json_object_put);
}
json_object* member(json_object* o,const char* key) {
    json_object* result=nullptr;
    if (o) json_object_object_get_ex(o,key,&result);
    return result;
}
std::string required_string(json_object* v,const std::string& label) {
    if (!v || !json_object_is_type(v,json_type_string)) throw std::runtime_error(label+" must be a string");
    return json_object_get_string(v);
}
}
std::filesystem::path default_settings_path() {
    const char* override_file=std::getenv("JAMILA_SETTINGS_PATH");
    if (override_file && *override_file) return override_file;
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::filesystem::path(xdg)/"jamila-game"/"settings.json";
    const char* home=std::getenv("HOME");
    if (!home || !*home) return {};
    return std::filesystem::path(home)/".config"/"jamila-game"/"settings.json";
}
ApprovalMode load_approval_mode(const std::filesystem::path& path) {
    if (path.empty() || !std::filesystem::exists(path)) return ApprovalMode::Human;
    auto root=read_json(path);
    auto* raw=member(root.get(),"approvalMode");
    if (!raw) return ApprovalMode::Human;
    auto str=required_string(raw,"approvalMode");
    if (str=="human") return ApprovalMode::Human;
    if (str=="allow-all") return ApprovalMode::AllowAll;
    throw std::runtime_error("unknown approvalMode (expected human or allow-all)");
}
std::vector<McpServerConfig> load_mcp_servers(const std::filesystem::path& path) {
    auto root=read_json(path);
    auto* servers=member(root.get(),"mcpServers");
    if (!servers || !json_object_is_type(servers,json_type_object))
        throw std::runtime_error("MCP config requires mcpServers object");
    std::vector<McpServerConfig> result;
    if (json_object_object_length(servers)>12) throw std::runtime_error("maximum twelve MCP servers");
    json_object_object_foreach(servers,name,server) {
        const std::string label=name;
        if (!std::regex_match(label,std::regex("[A-Za-z][A-Za-z0-9_-]{0,31}")))
            throw std::runtime_error("invalid MCP server name: "+label);
        if (!json_object_is_type(server,json_type_object)) throw std::runtime_error("MCP server must be object");
        auto* enabled=member(server,"disabled");
        if (enabled && !json_object_is_type(enabled,json_type_boolean)) throw std::runtime_error("disabled must be boolean");
        if (enabled && json_object_get_boolean(enabled)) continue;
        if (member(server,"url") || member(server,"type"))
            throw std::runtime_error("only local stdio MCP servers are supported; remove url/type");
        McpServerConfig spec; spec.name=label;
        spec.command.push_back(required_string(member(server,"command"),"command"));
        if (spec.command[0].empty() || spec.command[0].size()>4096) throw std::runtime_error("invalid MCP command");
        if (auto* args=member(server,"args")) {
            if (!json_object_is_type(args,json_type_array) || json_object_array_length(args)>30)
                throw std::runtime_error("args must be array of at most 30 strings");
            for (size_t i=0;i<json_object_array_length(args);++i) {
                auto value=required_string(json_object_array_get_idx(args,i),"args item");
                if (value.size()>4096) throw std::runtime_error("MCP argument exceeds 4096 bytes");
                spec.command.push_back(value);
            }
        }
        if (auto* selected=member(server,"includeTools")) {
            if (!json_object_is_type(selected,json_type_array) || json_object_array_length(selected)>128)
                throw std::runtime_error("includeTools must be an array of at most 128 tool names");
            for(size_t i=0;i<json_object_array_length(selected);++i) {
                std::string tool=required_string(json_object_array_get_idx(selected,i),"includeTools entry");
                if(tool.empty() || tool.size()>128) throw std::runtime_error("invalid included tool name");
                spec.include_tools.push_back(tool);
            }
        }
        if (auto* env=member(server,"env")) {
            if (!json_object_is_type(env,json_type_object) || json_object_object_length(env)>32)
                throw std::runtime_error("env must be an object of at most 32 variables");
            json_object_object_foreach(env,key,value) {
                std::string var=key;
                if (!std::regex_match(var,std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    throw std::runtime_error("invalid environment key");
                auto ref=required_string(value,"env value");
                // Never store secret values in JSON; resolve from this process environment.
                if (ref.size()<4 || ref.substr(0,2)!="${" || ref.back()!='}')
                    throw std::runtime_error("env values must use ${VARIABLE} references, not literal secrets");
                auto source=ref.substr(2,ref.size()-3);
                if (!std::regex_match(source,std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    throw std::runtime_error("invalid env reference");
                const char* actual=std::getenv(source.c_str());
                if (!actual) throw std::runtime_error("missing environment variable: "+source);
                spec.env[var]=actual;
            }
        }
        result.push_back(std::move(spec));
    }
    return result;
}
}