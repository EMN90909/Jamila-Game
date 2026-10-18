#include <json-c/json.h>
#include <iostream>
#include <string>
// Hermetic, protocol-correct MCP stdio fake. No Godot installation or npm dependency.
int main() {
    std::string line;
    while (std::getline(std::cin,line)) {
        auto* req = json_tokener_parse(line.c_str());
        if (!req) return 2;
        json_object *method=nullptr, *id=nullptr;
        json_object_object_get_ex(req,"method",&method);
        json_object_object_get_ex(req,"id",&id);
        if (!id) { json_object_put(req); continue; }
        std::string what = method ? json_object_get_string(method) : "";
        std::string result;
        if (what=="initialize") result=R"({"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"mock-godot","version":"1"}})";
        else if (what=="tools/list") result=R"({"tools":[{"name":"get_project_info","description":"Read Godot project structure","inputSchema":{"type":"object","properties":{"projectPath":{"type":"string"}},"required":["projectPath"]}},{"name":"create_scene","description":"Make new scene","inputSchema":{"type":"object","properties":{"scenePath":{"type":"string"},"projectPath":{"type":"string"}},"required":["scenePath","projectPath"]}}]})";
        else if (what=="tools/call") {
            auto* params=json_object_object_get(req,"params");
            auto* tool=params?json_object_object_get(params,"name"):nullptr;
            std::string name = tool?json_object_get_string(tool):"";
            result=std::string(R"({"content":[{"type":"text","text":")")+ (name=="get_project_info"?"mock scene tree" : "mock scene created") + R"("}],"isError":false})";
        } else result=R"({"error":"invalid method"})";
        std::cout << R"({"jsonrpc":"2.0","id":)" << json_object_get_int(id) << R"(,"result":)" << result << "}\n" << std::flush;
        json_object_put(req);
    }
}
