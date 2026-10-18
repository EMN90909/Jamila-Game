#include "jamila/core.hpp"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace fs=std::filesystem;
template<class F> void fails(F run) { bool thrown=false; try { run(); } catch (const std::exception&) { thrown=true; } assert(thrown); }
void write_all(int socket, const std::string& data) {
    size_t off=0;
    while(off<data.size()) { auto n=::send(socket,data.data()+off,data.size()-off,0); assert(n>0); off+=n; }
}
std::string recv_request(int client) {
    std::string request;
    char buf[2048];
    while (request.find("\r\n\r\n")==std::string::npos) {
        auto n=::recv(client,buf,sizeof(buf),0); assert(n>0); request.append(buf,n);
    }
    size_t header_end=request.find("\r\n\r\n")+4;
    auto pos=request.find("Content-Length:");
    assert(pos!=std::string::npos);
    size_t count=static_cast<size_t>(std::stoul(request.substr(pos+15)));
    while(request.size()<header_end+count) {
        auto n=::recv(client,buf,sizeof(buf),0); assert(n>0); request.append(buf,n);
    }
    return request.substr(header_end,count);
}
int main(int argc,char** argv) {
    assert(argc==2);
    char pattern[]="/tmp/jamila-godot-test-XXXXXX";
    auto* temp=::mkdtemp(pattern); assert(temp);
    fs::path root=temp;
    { std::ofstream p(root/"project.godot"); p << "config_version=5\n"; }
    jamila::Agent agent({root,"http://127.0.0.1:1/v1/chat/completions","","test",{argv[1]}});
    assert(agent.has_mcp()); assert(agent.mcp_tools().size()==5);
    auto info=agent.call_mcp("get_project_info",std::string("{\"projectPath\":\"")+root.string()+"\"}");
    assert(info.find("mock scene tree")!=std::string::npos);
    assert(agent.call_mcp("create_scene",R"({"scenePath":"res://scenes/level.tscn"})").find("Denied")!=std::string::npos);
    bool approved=false;
    agent.set_approval([&](const std::string& name,const std::string&){ approved=true; return name=="create_scene"; });
    assert(agent.call_mcp("create_scene",R"({"scenePath":"scenes/level.tscn"})").find("mock scene created")!=std::string::npos);
    assert(approved);
    fails([&]{ agent.call_mcp("create_scene",R"({"scenePath":"../../etc/passwd"})"); });
    fails([&]{ agent.call_mcp("create_scene",R"({"scenePath":"/etc/passwd"})"); });
    fails([&]{ agent.call_mcp("create_scene",R"({"scenePath":"res://../secret.gd"})"); });
    fails([&]{ agent.call_mcp("create_scene",R"({"scenePath":"user://private.txt"})"); });
    fails([&]{ agent.call_mcp("unknown",R"({})"); });
    // Two model messages: tool call -> MCP -> final answer.
    int server=::socket(AF_INET,SOCK_STREAM,0); assert(server>=0);
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    int on=1; ::setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
    assert(::bind(server,(sockaddr*)&address,sizeof(address))==0);
    socklen_t len=sizeof(address); assert(::getsockname(server,(sockaddr*)&address,&len)==0);
    assert(::listen(server,3)==0);
    std::thread mock([&]{
        for(int round=0;round<2;++round) {
            int client=::accept(server,nullptr,nullptr); assert(client>=0);
            auto body=recv_request(client);
            assert(body.find("\"get_project_info\"")!=std::string::npos);
            if (round==1) { assert(body.find("tool_call_id")!=std::string::npos); assert(body.find("mock scene tree")!=std::string::npos); }
            const std::string content=round==0 ?
                R"({"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"tool_1","type":"function","function":{"name":"get_project_info","arguments":"{}"}}]}}]})" :
                R"({"choices":[{"message":{"role":"assistant","content":"Project inspected using Godot MCP."}}]})";
            write_all(client,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(content.size())+"\r\nConnection: close\r\n\r\n"+content);
            ::close(client);
        }
        ::close(server);
    });
    jamila::Agent agent2({root,"http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/v1/chat/completions","","test",{argv[1]}});
    assert(agent2.ask("Inspect my Godot project")=="Project inspected using Godot MCP.");
    assert(agent2.history().size()==2); assert(!agent2.activity().empty());
    mock.join();
    fs::remove_all(root);
    std::cout << "MCP protocol, tool approvals, workspace guard and agent loop tests passed\n";
}