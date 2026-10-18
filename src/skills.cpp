#include "jamila/skills.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
namespace jamila {
namespace {
std::string lower(std::string s){for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
bool child_of(const std::filesystem::path& root,const std::filesystem::path& path){
    auto a=root.begin(),b=path.begin();
    while(a!=root.end())if(b==path.end() || *a++!=*b++)return false;
    return true;
}
std::string trim(std::string s){
    auto first=s.find_first_not_of(" \t\r\n\"'");if(first==std::string::npos)return {};
    auto last=s.find_last_not_of(" \t\r\n\"'");return s.substr(first,last-first+1);
}
}
std::vector<Skill> discover_skills(const std::filesystem::path& workspace){
    std::vector<Skill> results;
    if(!std::filesystem::is_directory(workspace))return results;
    const auto root=std::filesystem::canonical(workspace);
    const std::vector<std::filesystem::path> locations={".agents/skills",".jamila/skills",".claude/skills","skills"};
    std::set<std::string> names;
    for(const auto& location:locations){
        const auto folder=root/location;
        if(!std::filesystem::is_directory(folder) || std::filesystem::is_symlink(folder))continue;
        for(const auto& entry:std::filesystem::directory_iterator(folder,std::filesystem::directory_options::skip_permission_denied)){
            if(results.size()>=32)break;
            if(!entry.is_directory() || entry.is_symlink())continue;
            auto name=entry.path().filename().string();
            if(name.empty() || name.size()>64 || !std::all_of(name.begin(),name.end(),[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='-'||c=='_';}))continue;
            auto file=entry.path()/"SKILL.md";
            if(!std::filesystem::is_regular_file(file) || std::filesystem::is_symlink(file) || std::filesystem::file_size(file)>16384)continue;
            auto canonical=std::filesystem::canonical(file);
            if(!child_of(root,canonical) || !names.insert(name).second)continue;
            std::ifstream in(file);if(!in)continue;
            std::string markdown((std::istreambuf_iterator<char>(in)),{});
            std::string description;
            std::istringstream lines(markdown);std::string line;
            if(std::getline(lines,line)&&trim(line)=="---"){
                for(int n=0;n<40 && std::getline(lines,line);++n){
                    if(trim(line)=="---")break;
                    if(line.rfind("description:",0)==0)description=trim(line.substr(12));
                }
            }
            if(description.empty())description=name;
            if(description.size()>300)description.resize(300);
            results.push_back({name,description,markdown,canonical});
        }
    }
    return results;
}
std::string matching_skill_context(const std::vector<Skill>& skills,const std::string& prompt){
    const std::string q=lower(prompt);
    std::vector<std::pair<int,const Skill*>> ranked;
    for(const auto& skill:skills){
        int score=0;
        if(q.find(lower(skill.name))!=std::string::npos)score+=100;
        std::istringstream words(lower(skill.description));std::string word;
        while(words>>word){
            word.erase(std::remove_if(word.begin(),word.end(),[](char c){return !std::isalnum(static_cast<unsigned char>(c));}),word.end());
            if(word.size()>=5 && q.find(word)!=std::string::npos)++score;
        }
        if(score>0)ranked.push_back({score,&skill});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::string result;
    for(size_t i=0;i<ranked.size()&&i<3;++i){
        const auto& s=*ranked[i].second;
        result+="\n<skill_reference name=\""+s.name+"\">\n"+s.markdown.substr(0,12000)+"\n</skill_reference>\n";
    }
    return result;
}
}