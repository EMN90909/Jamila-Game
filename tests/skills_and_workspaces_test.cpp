#include "jamila/core.hpp"
#include "jamila/notifications.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs=std::filesystem;
int main(){
    char dir[]="/tmp/jamila-skills-XXXXXX";auto* p=mkdtemp(dir);assert(p);
    fs::path root(p);fs::create_directories(root/".agents/skills/godot-scripting");
    // Write tool must safely create this directory when needed.
    {std::ofstream f(root/"project.godot");f<<"config_version=5\n";}
    {std::ofstream f(root/".agents/skills/godot-scripting/SKILL.md");f<<"---\nname: godot-scripting\ndescription: Handle GDScript scenes and code\n---\nUse static typing, verify in Godot.\n";}
    // Symlinked skill directories must not exfiltrate files outside workspace.
    fs::create_directory_symlink("/tmp",root/".agents/skills/foreign");
    auto skills=jamila::discover_skills(root);assert(skills.size()==1);
    assert(jamila::matching_skill_context(skills,"fix GDScript scenes").find("godot-scripting")!=std::string::npos);
    assert(jamila::matching_skill_context(skills,"totally unrelated vegetables").empty());
    jamila::Config cfg;cfg.workspace=root;
    jamila::Agent agent(cfg);
    assert(agent.skills().size()==1);
    assert(agent.server_count()==0);
    assert(agent.mcp_tools().size()==3);
    const auto patch=R"({"path":"scripts/player.gd","content":"extends CharacterBody2D\nfunc _physics_process(delta):\n    velocity.y += delta\n"})";
    assert(agent.call_mcp("jamila__write_file",patch).find("Denied")!=std::string::npos);
    assert(!fs::exists(root/"scripts/player.gd"));
    int approvals=0;
    agent.set_approval([&](const std::string& name,const std::string& args){++approvals;assert(name=="jamila__write_file");return args.find("player.gd")!=std::string::npos;});
    assert(agent.call_mcp("jamila__write_file",patch).find("successfully")!=std::string::npos);
    assert(approvals==1);
    assert(fs::is_directory(root/"scripts"));
    assert(agent.call_mcp("jamila__read_file",R"({"path":"scripts/player.gd"})").find("CharacterBody2D")!=std::string::npos);
    assert(agent.call_mcp("jamila__list_files",R"({"directory":"."})").find("player.gd")!=std::string::npos);
    auto escaped=agent.call_mcp("jamila__write_file",R"({"path":"../../escape.txt","content":"bad"})");
    assert(escaped.find("isError")!=std::string::npos && !fs::exists(root.parent_path()/"escape.txt"));
    fs::create_symlink("/etc/passwd",root/"scripts/trap.gd");
    assert(agent.call_mcp("jamila__read_file",R"({"path":"scripts/trap.gd"})").find("isError")!=std::string::npos);
    assert(jamila::notification_action_from_signal("signal interface=org.freedesktop.Notifications; member=ActionInvoked\nuint32 5\nstring \"review\"",5)=="review");
    assert(jamila::notification_action_from_signal("signal member=ActionInvoked\nuint32 6\nstring \"approve\"",5).empty());
    assert(jamila::notification_action_from_signal("signal member=ActionInvoked\nuint32 5\nstring \"denied\"",5).empty());
    fs::remove_all(root);
    std::cout<<"Skills discovery, workspace coding, approvals and notification parser passed\n";
}