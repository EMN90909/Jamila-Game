#include "jamila/core.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
namespace fs=std::filesystem;

template<typename F> void fails(F fn) { bool caught=false; try { fn(); } catch(const std::exception&) { caught=true; } assert(caught); }
int main(int argc,char** argv) {
    assert(argc==2);
    char name[]="/tmp/jamila-multi-XXXXXX";
    auto dir=::mkdtemp(name); assert(dir!=nullptr);
    auto root=fs::path(dir);
    {std::ofstream f(root/"project.godot"); f << "config_version=5\n";}
    auto cfgfile=root/"servers.json";
    auto settings=root/"settings.json";
    ::setenv("JAMILA_MCP_TEST_KEY","inherited-not-written",1);
    {std::ofstream f(cfgfile); f << R"({"mcpServers":{"godot":{"command":")" << argv[1] << R"(","includeTools":["get_project_info","create_scene"]},"github":{"command":")" << argv[1] << R"(","env":{"TEST_KEY":"${JAMILA_MCP_TEST_KEY}"},"includeTools":["create_scene"]},"ignored":{"command":"/bin/false","disabled":true}}})"; }
    auto specs=jamila::load_mcp_servers(cfgfile);
    assert(specs.size()==2);
    assert(specs[1].env.at("TEST_KEY")=="inherited-not-written");
    assert(jamila::load_approval_mode(settings)==jamila::ApprovalMode::Human);
    jamila::Config cfg;
    cfg.workspace=root; cfg.mcp_servers=specs; cfg.settings_file=settings;
    jamila::Agent agent(cfg);
    assert(agent.server_count()==2 && agent.mcp_tools().size()==3);
    assert(agent.call_mcp("godot__get_project_info",R"({})").find("mock scene tree")!=std::string::npos);
    assert(agent.call_mcp("github__create_scene",R"({"path":"/outside/of/the/project"})").find("Denied")!=std::string::npos);
    int approvals=0;
    agent.set_approval([&](const std::string& tool,const std::string& args){
        ++approvals;
        assert(tool=="github__create_scene");
        assert(args.find("/outside")!=std::string::npos);
        return false;
    });
    assert(agent.call_mcp("github__create_scene",R"({"path":"/outside/of/the/project"})").find("Denied")!=std::string::npos);
    assert(approvals==1);
    agent.set_approval_mode(jamila::ApprovalMode::AllowAll);
    assert(jamila::load_approval_mode(settings)==jamila::ApprovalMode::AllowAll);
    assert(agent.call_mcp("github__create_scene",R"({"path":"/outside/of/the/project"})").find("mock scene created")!=std::string::npos);
    assert(approvals==1); // No human prompt in Allow All.
    fails([&]{ agent.call_mcp("godot__create_scene",R"({"scenePath":"../../outside"})"); });
    assert(agent.call_mcp("godot__create_scene",R"({"scenePath":"res://levels/start.tscn"})").find("mock scene created")!=std::string::npos);
    agent.set_approval_mode(jamila::ApprovalMode::Human);
    assert(jamila::load_approval_mode(settings)==jamila::ApprovalMode::Human);
    fails([&]{ agent.call_mcp("no-such-tool",R"({})"); });
    {std::ofstream f(cfgfile); f << R"({"mcpServers":{"bad":{"command":"/bin/echo","env":{"KEY":"plaintext-secret"}}}})";}
    fails([&]{ jamila::load_mcp_servers(cfgfile); });
    {std::ofstream f(cfgfile); f << R"({"mcpServers":{"remote":{"command":"x","url":"https://example.com/mcp"}}})";}
    fails([&]{ jamila::load_mcp_servers(cfgfile); });
    fs::remove_all(root);
    std::cout << "Multi-MCP routing, server settings, approval modes, and security tests passed\n";
}