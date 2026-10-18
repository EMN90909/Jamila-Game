#include "jamila/providers.hpp"
#include <json-c/json.h>
#include <cassert>
#include <string>
#include <iostream>
int main(){
    std::string open=R"({"model":"claude-test","messages":[{"role":"system","content":"Godot test"},{"role":"user","content":"Make a scene"},{"role":"assistant","content":null,"tool_calls":[{"type":"function","id":"toolu_one","function":{"name":"jamila__write_file","arguments":"{\"path\":\"demo.gd\",\"content\":\"extends Node\"}"}}]},{"role":"tool","tool_call_id":"toolu_one","content":"file saved"}],"tools":[{"type":"function","function":{"name":"jamila__write_file","description":"Write file","parameters":{"type":"object"}}}]})";
    auto out=jamila::to_anthropic_messages(open);
    auto* payload=json_tokener_parse(out.c_str());assert(payload);
    assert(json_object_object_get(payload,"max_tokens"));
    assert(json_object_object_get(payload,"system"));
    auto* messages=json_object_object_get(payload,"messages");assert(json_object_array_length(messages)==3);
    auto* turn=json_object_array_get_idx(messages,2);assert(std::string(json_object_get_string(json_object_object_get(turn,"role")))=="user");
    auto* block=json_object_array_get_idx(json_object_object_get(turn,"content"),0);
    assert(std::string(json_object_get_string(json_object_object_get(block,"type")))=="tool_result");
    json_object_put(payload);
    auto reply=jamila::from_anthropic_messages(R"({"content":[{"type":"text","text":"Working"},{"type":"tool_use","id":"toolu_two","name":"jamila__read_file","input":{"path":"demo.gd"}}]})");
    auto* parsed=json_tokener_parse(reply.c_str());assert(parsed);
    auto* message=json_object_object_get(json_object_array_get_idx(json_object_object_get(parsed,"choices"),0),"message");
    assert(std::string(json_object_get_string(json_object_object_get(message,"content")))=="Working");
    assert(json_object_array_length(json_object_object_get(message,"tool_calls"))==1);
    json_object_put(parsed);
    std::cout<<"Anthropic Messages tool_use/tool_result conversions passed\n";
}