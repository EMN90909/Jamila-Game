#include "jamila/core.hpp"
#include <curl/curl.h>
#include <json-c/json.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <atomic>
#include <sys/stat.h>
#include <limits>
#include <memory>
#include <stdexcept>

#ifdef JAMILA_WITH_ZIG
extern "C" unsigned long long jamila_fingerprint(const unsigned char*, size_t);
#endif

namespace fs = std::filesystem;
namespace jamila {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
constexpr size_t MAX_FILE = 1024 * 1024;
constexpr size_t MAX_RESPONSE = 2 * 1024 * 1024;
bool starts_with_path(const fs::path& base, const fs::path& path) {
    auto a = base.begin(), b = path.begin();
    for (; a != base.end(); ++a, ++b) if (b == path.end() || *a != *b) return false;
    return true;
}
size_t append_response(char* ptr, size_t size, size_t count, void* context) {
    auto* response = static_cast<std::string*>(context);
    if (size && count > std::numeric_limits<size_t>::max() / size) return 0;
    const size_t n = size * count;
    if (n > MAX_RESPONSE || response->size() > MAX_RESPONSE - n) return 0;
    response->append(ptr, n);
    return n;
}
std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string(fallback);
}
}

fs::path resolve_in_workspace(const fs::path& root, const fs::path& relative) {
    if (relative.empty() || relative.is_absolute()) throw std::invalid_argument("path must be relative to workspace");
    const fs::path canonical_root = fs::canonical(root);
    const fs::path target = fs::weakly_canonical(canonical_root / relative);
    if (target == canonical_root || !starts_with_path(canonical_root, target))
        throw std::invalid_argument("path escapes workspace");
    return target;
}

std::string read_text(const fs::path& root, const fs::path& relative) {
    fs::path p = resolve_in_workspace(root, relative);
    if (!fs::is_regular_file(p)) throw std::runtime_error("not a regular file");
    if (fs::file_size(p) > MAX_FILE) throw std::runtime_error("file exceeds 1 MiB read limit");
    std::ifstream file(p, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open file");
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void write_text(const fs::path& root, const fs::path& relative, const std::string& text) {
    if (text.size() > MAX_FILE) throw std::runtime_error("write exceeds 1 MiB limit");
    // The canonical path guard checks the intended target. Walk from the root via
    // dirfds with O_NOFOLLOW to prevent directory symlink replacement races.
    const auto p = resolve_in_workspace(root,relative);
    const auto canonical_root=fs::canonical(root);
    const auto rel=p.lexically_relative(canonical_root);
    const int base=::open(canonical_root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(base<0)throw std::runtime_error("could not open workspace directory");
    int dir=base;
    auto it=rel.begin();
    const auto last=std::prev(rel.end());
    try {
        for(;it!=last;++it){
            const auto part=it->string();
            if(part.empty() || part=="." || part=="..")throw std::invalid_argument("invalid output directory");
            if(::mkdirat(dir,part.c_str(),0700)<0 && errno!=EEXIST)
                throw std::runtime_error("could not create output directory");
            int next=::openat(dir,part.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
            if(next<0)throw std::runtime_error("unsafe or inaccessible output directory");
            ::close(dir);dir=next;
        }
        const auto filename=last->string();
        if(filename.empty() || filename=="." || filename=="..")throw std::invalid_argument("invalid output filename");
        struct stat existing{};
        const bool exists=::fstatat(dir,filename.c_str(),&existing,AT_SYMLINK_NOFOLLOW)==0;
        if(exists && !S_ISREG(existing.st_mode))throw std::runtime_error("destination must be regular file");
        static std::atomic<unsigned> counter{0};
        const auto tmp=".jamila-tmp-"+std::to_string(::getpid())+"-"+std::to_string(counter.fetch_add(1));
        const int fd=::openat(dir,tmp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)throw std::runtime_error("could not open temporary file");
        try {
            if(exists)::fchmod(fd,existing.st_mode&0777);
            size_t offset=0;
            while(offset<text.size()){
                const auto n=::write(fd,text.data()+offset,text.size()-offset);
                if(n<0 && errno==EINTR)continue;
                if(n<=0)throw std::runtime_error("file write failed");
                offset+=static_cast<size_t>(n);
            }
            if(::fsync(fd)!=0)throw std::runtime_error("file sync failed");
            if(::close(fd)!=0)throw std::runtime_error("file close failed");
            if(::renameat(dir,tmp.c_str(),dir,filename.c_str())!=0)throw std::runtime_error("atomic file replace failed");
            ::fsync(dir);
        } catch (...) {
            ::close(fd);::unlinkat(dir,tmp.c_str(),0);throw;
        }
        ::close(dir);
    }catch(...){::close(dir);throw;}
}

int execute_argv(const fs::path& cwd, const std::vector<std::string>& args) {
    if (args.empty() || args.front().empty()) throw std::invalid_argument("missing executable");
    if (args.size() > 64) throw std::invalid_argument("too many arguments");
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = ::fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        if (::chdir(cwd.c_str()) != 0) _exit(126);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("waitpid failed");
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
}

unsigned long long fingerprint(const std::string& text) {
#ifdef JAMILA_WITH_ZIG
    return jamila_fingerprint(reinterpret_cast<const unsigned char*>(text.data()), text.size());
#else
    unsigned long long h = 14695981039346656037ULL;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    return h;
#endif
}

Agent::Agent(Config config) : config_(std::move(config)) {
    config_.workspace = fs::canonical(config_.workspace);
    skills_ = discover_skills(config_.workspace);
    if (config_.provider.empty()) config_.provider=env_or("JAMILA_PROVIDER","openai");
    if (config_.provider!="openai" && config_.provider!="anthropic")
        throw std::invalid_argument("provider must be openai or anthropic");
    if (config_.api_url.empty()) config_.api_url=env_or("JAMILA_API_URL",
        config_.provider=="anthropic"?"https://api.anthropic.com/v1/messages":"https://api.openai.com/v1/chat/completions");
    if (config_.api_key.empty()) config_.api_key = env_or("JAMILA_API_KEY", "");
    if (config_.model.empty()) config_.model = env_or("JAMILA_MODEL", "gpt-4o-mini");
    if (!config_.mcp_command.empty()) config_.mcp_servers.insert(config_.mcp_servers.begin(),
        {"godot",config_.mcp_command,{},{}});
    // Each MCP is isolated: one client, one subprocess, independent errors.
    std::map<std::string, unsigned> used_names;
    for (const auto& spec : config_.mcp_servers) {
        if (spec.name.empty() || spec.name.size()>32 || ++used_names[spec.name] > 1)
            throw std::invalid_argument("MCP server names must be unique and 1..32 characters");
        try { servers_.push_back({spec.name,std::make_unique<McpClient>(config_.workspace,spec.command,spec.env)}); }
        catch (const std::exception& e) { connection_errors_.push_back(spec.name+": "+e.what()); }
    }
    for (size_t i=0;i<servers_.size();++i) {
        for (const auto& tool : servers_[i].client->tools()) {
            const auto& selected=config_.mcp_servers; // server specs were validated before connection
            auto match=std::find_if(selected.begin(),selected.end(),[&](const McpServerConfig& c){return c.name==servers_[i].name;});
            if (match!=selected.end() && !match->include_tools.empty() &&
                std::find(match->include_tools.begin(),match->include_tools.end(),tool.name)==match->include_tools.end()) continue;
            if (exposed_tools_.size()>=128)
                throw std::runtime_error("Too many MCP tools (max 128). Configure includeTools per server.");
            // Stable, provider-compatible name; maintain a private mapping to the exact MCP name.
            std::string full = servers_.size()==1 ? tool.name : servers_[i].name+"__"+tool.name;
            std::string n=full;
            for(char& ch:n) if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||
                                   (ch>='0'&&ch<='9')||ch=='_'||ch=='-')) ch='_';
            if (n.size()>64) n=n.substr(0,44)+"_"+std::to_string(fingerprint(full));
            if (n.size()>64) n.resize(64);
            if (!routing_.emplace(n,std::make_pair(i,tool.name)).second)
                throw std::runtime_error("MCP tool name collision: "+n);
            exposed_tools_.push_back({n,"["+servers_[i].name+"] "+tool.description,tool.input_schema});
        }
    }
    for(const auto& native:native_workspace_tools()) {
        if(!routing_.count(native.name))exposed_tools_.push_back(native);
        else throw std::runtime_error("MCP name shadows reserved native tool: "+native.name);
    }
}

std::string Agent::ask(const std::string& prompt) {
    if (prompt.empty() || prompt.size() > 32768) throw std::invalid_argument("prompt must be 1..32768 bytes");
    return ask_godot(prompt);
    /* Legacy advice-only path retained for reference; native workspace tools now
       make the tool-calling pipeline available even with no MCP server. */
    if (config_.api_key.empty() && config_.api_url.rfind("http://localhost:", 0) != 0 &&
        config_.api_url.rfind("http://127.0.0.1:", 0) != 0)
        throw std::runtime_error("set JAMILA_API_KEY before calling a remote provider");
    if (config_.api_url.rfind("https://", 0) != 0 &&
        config_.api_url.rfind("http://127.0.0.1:", 0) != 0 &&
        config_.api_url.rfind("http://localhost:", 0) != 0)
        throw std::runtime_error("only HTTPS or explicit loopback HTTP endpoints allowed");
    Json root(json_object_new_object(), json_object_put);
    json_object_object_add(root.get(), "model", json_object_new_string(config_.model.c_str()));
    auto* messages = json_object_new_array();
    json_object_array_add(messages, json_object_new_object());
    json_object_object_add(json_object_array_get_idx(messages, 0), "role", json_object_new_string("system"));
    json_object_object_add(json_object_array_get_idx(messages, 0), "content", json_object_new_string("You are Jamila Game, a Godot 4 game-development assistant. Help with gameplay systems, scenes, GDScript, C++ GDExtension, debugging, performance, and test design. This chat mode has NO Godot MCP tools connected: explain how to connect with --godot-mcp. Never claim to have edited or executed anything."));
    for (const auto& msg : history_) {
        auto* item = json_object_new_object();
        json_object_object_add(item, "role", json_object_new_string(msg.role.c_str()));
        json_object_object_add(item, "content", json_object_new_string_len(msg.content.c_str(), static_cast<int>(msg.content.size())));
        json_object_array_add(messages, item);
    }
    auto* item = json_object_new_object();
    json_object_object_add(item, "role", json_object_new_string("user"));
    json_object_object_add(item, "content", json_object_new_string_len(prompt.c_str(), static_cast<int>(prompt.size())));
    json_object_array_add(messages, item);
    json_object_object_add(root.get(), "messages", messages);
    const char* body = json_object_to_json_string_ext(root.get(), JSON_C_TO_STRING_PLAIN);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl initialization failed");
    std::string response;
    curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + config_.api_key;
    if (!config_.api_key.empty()) raw_headers = curl_slist_append(raw_headers, auth.c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw_headers, curl_slist_free_all);
    curl_easy_setopt(curl.get(), CURLOPT_URL, config_.api_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_response);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    CURLcode result = curl_easy_perform(curl.get());
    if (result != CURLE_OK) throw std::runtime_error(std::string("provider request failed: ") + curl_easy_strerror(result));
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    if (code < 200 || code >= 300) throw std::runtime_error("provider returned HTTP " + std::to_string(code));
    Json parsed(json_tokener_parse(response.c_str()), json_object_put);
    if (!parsed) throw std::runtime_error("provider returned invalid JSON");
    json_object *choices=nullptr, *first=nullptr, *message=nullptr, *content=nullptr;
    if (!json_object_object_get_ex(parsed.get(), "choices", &choices) ||
        !json_object_is_type(choices, json_type_array) || json_object_array_length(choices) == 0 ||
        !(first = json_object_array_get_idx(choices, 0)) ||
        !json_object_object_get_ex(first, "message", &message) ||
        !json_object_object_get_ex(message, "content", &content) ||
        !json_object_is_type(content, json_type_string))
        throw std::runtime_error("provider JSON missing choices[0].message.content");
    std::string answer = json_object_get_string(content);
    history_.push_back({"user", prompt});
    history_.push_back({"assistant", answer});
    if (history_.size() > 20) history_.erase(history_.begin(), history_.begin() + 2);
    return answer;
}
}