#include "jamila/providers.hpp"
#include <json-c/json.h>
#include <memory>
#include <stdexcept>
#include <string>
namespace jamila {
namespace {
using Json=std::unique_ptr<json_object,decltype(&json_object_put)>;
json_object* field(json_object* object,const char* key){json_object* out=nullptr;if(object)json_object_object_get_ex(object,key,&out);return out;}
std::string str(json_object* object,const char* key){auto* o=field(object,key);return o && json_object_is_type(o,json_type_string)?json_object_get_string(o):"";}
std::string stringify(json_object* j){return json_object_to_json_string_ext(j,JSON_C_TO_STRING_PLAIN);}
Json parse(const std::string& s){
    json_tokener* t=json_tokener_new();auto* x=json_tokener_parse_ex(t,s.data(),static_cast<int>(s.size()));
    bool valid=x&&json_tokener_get_error(t)==json_tokener_success&&static_cast<size_t>(t->char_offset)==s.size();json_tokener_free(t);
    if(!valid){if(x)json_object_put(x);throw std::runtime_error("provider JSON invalid");}return Json(x,json_object_put);
}
json_object* strv(const char* value){return json_object_new_string(value);}
}
std::string to_anthropic_messages(const std::string& request){
    auto source=parse(request);
    Json out(json_object_new_object(),json_object_put);
    json_object_object_add(out.get(),"model",json_object_get(field(source.get(),"model")));
    json_object_object_add(out.get(),"max_tokens",json_object_new_int(3072));
    auto* incoming=field(source.get(),"messages");
    if(!incoming || !json_object_is_type(incoming,json_type_array))throw std::runtime_error("model messages missing");
    auto* messages=json_object_new_array();std::string instructions;
    for(size_t i=0;i<json_object_array_length(incoming);++i){
        auto* msg=json_object_array_get_idx(incoming,i);
        const auto role=str(msg,"role");
        if(role=="system") {instructions+=str(msg,"content")+"\n";continue;}
        auto* outgoing=json_object_new_object();
        if(role=="tool"){
            json_object_object_add(outgoing,"role",strv("user"));
            auto* contents=json_object_new_array();auto* result=json_object_new_object();
            json_object_object_add(result,"type",strv("tool_result"));
            json_object_object_add(result,"tool_use_id",json_object_new_string(str(msg,"tool_call_id").c_str()));
            json_object_object_add(result,"content",json_object_new_string(str(msg,"content").c_str()));
            json_object_array_add(contents,result);
            json_object_object_add(outgoing,"content",contents);
        }else if(role=="assistant" && field(msg,"tool_calls") && json_object_is_type(field(msg,"tool_calls"),json_type_array)){
            json_object_object_add(outgoing,"role",strv("assistant"));
            auto* content=json_object_new_array();
            auto t=str(msg,"content");
            if(!t.empty()){
                auto* txt=json_object_new_object();json_object_object_add(txt,"type",strv("text"));
                json_object_object_add(txt,"text",json_object_new_string(t.c_str()));json_object_array_add(content,txt);
            }
            auto* calls=field(msg,"tool_calls");
            for(size_t j=0;j<json_object_array_length(calls);++j){
                auto* call=json_object_array_get_idx(calls,j);auto* fn=field(call,"function");
                auto* use=json_object_new_object();
                json_object_object_add(use,"type",strv("tool_use"));
                json_object_object_add(use,"id",json_object_new_string(str(call,"id").c_str()));
                json_object_object_add(use,"name",json_object_new_string(str(fn,"name").c_str()));
                auto args=parse(str(fn,"arguments"));
                json_object_object_add(use,"input",json_object_get(args.get()));
                json_object_array_add(content,use);
            }
            json_object_object_add(outgoing,"content",content);
        }else if(role=="assistant" || role=="user"){
            json_object_object_add(outgoing,"role",json_object_new_string(role.c_str()));
            json_object_object_add(outgoing,"content",json_object_new_string(str(msg,"content").c_str()));
        }else{json_object_put(outgoing);throw std::runtime_error("unsupported model message role");}
        json_object_array_add(messages,outgoing);
    }
    json_object_object_add(out.get(),"system",json_object_new_string(instructions.c_str()));
    json_object_object_add(out.get(),"messages",messages);
    auto* tools=field(source.get(),"tools");
    if(tools && json_object_is_type(tools,json_type_array) && json_object_array_length(tools)>0){
        auto* target=json_object_new_array();
        for(size_t i=0;i<json_object_array_length(tools);++i){
            auto* function=field(json_object_array_get_idx(tools,i),"function");
            if(!function)continue;
            auto* t=json_object_new_object();
            json_object_object_add(t,"name",json_object_new_string(str(function,"name").c_str()));
            json_object_object_add(t,"description",json_object_new_string(str(function,"description").c_str()));
            auto* schema=field(function,"parameters");
            if(schema)json_object_object_add(t,"input_schema",json_object_get(schema));
            else json_object_object_add(t,"input_schema",json_object_new_object());
            json_object_array_add(target,t);
        }
        json_object_object_add(out.get(),"tools",target);
    }
    return stringify(out.get());
}
std::string from_anthropic_messages(const std::string& response){
    auto source=parse(response);
    auto* blocks=field(source.get(),"content");
    if(!blocks || !json_object_is_type(blocks,json_type_array))throw std::runtime_error("Claude response has no content blocks");
    Json out(json_object_new_object(),json_object_put);
    auto* choices=json_object_new_array();auto* choice=json_object_new_object();auto* message=json_object_new_object();
    json_object_object_add(message,"role",strv("assistant"));
    std::string text;auto* calls=json_object_new_array();
    for(size_t i=0;i<json_object_array_length(blocks);++i){
        auto* block=json_object_array_get_idx(blocks,i);const auto type=str(block,"type");
        if(type=="text")text+=str(block,"text");
        if(type=="tool_use"){
            auto* call=json_object_new_object();auto* fn=json_object_new_object();
            json_object_object_add(call,"id",json_object_new_string(str(block,"id").c_str()));
            json_object_object_add(call,"type",strv("function"));
            json_object_object_add(fn,"name",json_object_new_string(str(block,"name").c_str()));
            auto* input=field(block,"input");
            json_object_object_add(fn,"arguments",json_object_new_string(input?stringify(input).c_str():"{}"));
            json_object_object_add(call,"function",fn);json_object_array_add(calls,call);
        }
    }
    json_object_object_add(message,"content",json_object_new_string(text.c_str()));
    if(json_object_array_length(calls)>0)json_object_object_add(message,"tool_calls",calls);
    else json_object_put(calls);
    json_object_object_add(choice,"message",message);json_object_array_add(choices,choice);
    json_object_object_add(out.get(),"choices",choices);
    return stringify(out.get());
}
}