#include "jamila/core.hpp"
#include <json-c/json.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
namespace jamila {
namespace {
using Json=std::unique_ptr<json_object,decltype(&json_object_put)>;
std::string response(const std::string& text,bool error=false){
    Json r(json_object_new_object(),json_object_put);
    json_object_object_add(r.get(),"isError",json_object_new_boolean(error));
    auto* content=json_object_new_array();auto* block=json_object_new_object();
    json_object_object_add(block,"type",json_object_new_string("text"));
    json_object_object_add(block,"text",json_object_new_string_len(text.c_str(),static_cast<int>(text.size())));
    json_object_array_add(content,block);
    json_object_object_add(r.get(),"content",content);
    return json_object_to_json_string_ext(r.get(),JSON_C_TO_STRING_PLAIN);
}
Json parse_args(const std::string& raw){
    if(raw.size()>1024*1024+4096)throw std::invalid_argument("oversized tool arguments");
    json_tokener* t=json_tokener_new();auto* root=json_tokener_parse_ex(t,raw.data(),static_cast<int>(raw.size()));
    bool ok=root&&json_tokener_get_error(t)==json_tokener_success&&static_cast<size_t>(t->char_offset)==raw.size();
    json_tokener_free(t);
    if(!ok || !json_object_is_type(root,json_type_object)){
        if(root)json_object_put(root);
        throw std::invalid_argument("native tool arguments must be JSON object");
    }
    return Json(root,json_object_put);
}
std::string required(json_object* root,const char* key){
    json_object* v=nullptr;
    if(!json_object_object_get_ex(root,key,&v) || !json_object_is_type(v,json_type_string))throw std::invalid_argument(std::string("missing string field: ")+key);
    return json_object_get_string(v);
}
}
const std::vector<McpTool>& native_workspace_tools(){
    static const std::vector<McpTool> tools={
        {"jamila__read_file","Read one text file under the selected Godot project (up to 1 MiB).",R"({"type":"object","properties":{"path":{"type":"string","description":"Project-relative file path"}},"required":["path"],"additionalProperties":false})"},
        {"jamila__write_file","Create or replace a text file in an existing project directory; human approval required unless Allow All is enabled.",R"({"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"],"additionalProperties":false})"},
        {"jamila__list_files","List up to 300 project files without following symlink directories.",R"({"type":"object","properties":{"directory":{"type":"string","description":"Project-relative folder, or . for root"}},"required":["directory"],"additionalProperties":false})"}
    };
    return tools;
}
std::string run_native_workspace_tool(const std::filesystem::path& root,const std::string& name,const std::string& arguments){
    auto args=parse_args(arguments);
    try{
        if(name=="jamila__read_file")return response(read_text(root,required(args.get(),"path")));
        if(name=="jamila__write_file"){
            write_text(root,required(args.get(),"path"),required(args.get(),"content"));
            return response("File written successfully. Verify with read_file and Godot parser or runtime checks.");
        }
        if(name=="jamila__list_files"){
            const auto directory=required(args.get(),"directory");
            const auto canonical_root=std::filesystem::canonical(root);
            auto path=directory=="."?canonical_root:resolve_in_workspace(canonical_root,directory);
            if(!std::filesystem::is_directory(path))return response("Not a directory",true);
            std::string output;
            size_t count=0;
            auto options=std::filesystem::directory_options::skip_permission_denied;
            for(auto it=std::filesystem::recursive_directory_iterator(path,options);it!=std::filesystem::recursive_directory_iterator();++it){
                if(count>=300)break;
                if(it.depth()>8 || it->is_symlink() || it->path().filename()==".git" || it->path().filename()==".godot"){
                    if(it->is_directory())it.disable_recursion_pending();
                    continue;
                }
                if(it->is_regular_file()){
                    auto rel=it->path().lexically_relative(canonical_root).string();
                    if(rel.size()<512){output+=rel+"\n";++count;}
                }
            }
            if(count==300)output+="[truncated at 300 files]\n";
            return response(output.empty()?"No matching files.":output);
        }
        throw std::invalid_argument("unadvertised native tool");
    }catch(const std::exception& e){return response(std::string("Workspace tool failed: ")+e.what(),true);}
}
}