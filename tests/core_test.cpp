#include "jamila/core.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

namespace fs=std::filesystem;
template <typename Fn> void must_throw(Fn action) {
    bool failed=false;
    try { action(); } catch(const std::exception&) { failed=true; }
    assert(failed);
}
int main() {
    char pattern[]="/tmp/jamila-test-XXXXXX";
    char* dir=::mkdtemp(pattern);
    assert(dir);
    fs::path root=dir;
    fs::create_directories(root/"src");
    jamila::write_text(root,"src/test.txt","Hello");
    assert(jamila::read_text(root,"src/test.txt")=="Hello");
    must_throw([&]{jamila::read_text(root,"../etc/passwd");});
    must_throw([&]{jamila::write_text(root,"../../etc/oops","BAD");});
    must_throw([&]{jamila::read_text(root,"/etc/passwd");});
    fs::create_symlink("/etc/passwd",root/"leak");
    must_throw([&]{jamila::read_text(root,"leak");});
    must_throw([&]{jamila::write_text(root,"leak","BAD");});
    must_throw([&]{jamila::write_text(root,"src/huge",std::string(1024*1024+1,'x'));});
    assert(jamila::fingerprint("a")==0xaf63dc4c8601ec8cULL);
    assert(jamila::execute_argv(root,{"/bin/true"})==0);
    assert(jamila::execute_argv(root,{"/bin/false"})!=0);
    jamila::Agent agent({root,"","",""});
    assert(agent.history().empty());
    must_throw([&]{agent.ask("");});
    agent.clear();
    // Hermetic OpenAI-compatible loopback mock. No API token/network service required.
    int server=::socket(AF_INET,SOCK_STREAM,0);
    assert(server>=0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=0;
    assert(::bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    socklen_t length=sizeof(address);
    assert(::getsockname(server,reinterpret_cast<sockaddr*>(&address),&length)==0);
    assert(::listen(server,1)==0);
    std::thread mock([server]{
        int client=::accept(server,nullptr,nullptr);
        assert(client>=0);
        char buffer[8192]{};
        ssize_t count=::recv(client,buffer,sizeof(buffer)-1,0);
        assert(count>0);
        assert(std::strstr(buffer,"POST /")!=nullptr);
        const std::string payload=R"({"choices":[{"message":{"role":"assistant","content":"mock-response"}}]})";
        const std::string response="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(payload.size())+"\r\nConnection: close\r\n\r\n"+payload;
        (void)::send(client,response.data(),response.size(),0);
        ::close(client);
        ::close(server);
    });
    jamila::Agent local({root,"http://127.0.0.1:"+std::to_string(ntohs(address.sin_port))+"/v1/chat/completions","","test-model"});
    assert(local.ask("Hello") == "mock-response");
    assert(local.history().size()==2);
    mock.join();
    fs::remove_all(root);
    std::cout << "All native core tests passed\n";
}
