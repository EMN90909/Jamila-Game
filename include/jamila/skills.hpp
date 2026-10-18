#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace jamila {
struct Skill { std::string name, description, markdown; std::filesystem::path path; };
// Reads local repository Skills; symlinks or paths leaving the workspace are ignored.
std::vector<Skill> discover_skills(const std::filesystem::path& workspace);
// Selects up to three matching skills, each bounded in size, as untrusted reference.
std::string matching_skill_context(const std::vector<Skill>& skills,const std::string& prompt);
}