#include "jamila/notifications.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <regex>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace jamila {
namespace {
struct CommandResult { int exit_code=-1; std::string output; };
CommandResult run(const std::vector<std::string>& args, int timeout_ms) {
    int p[2]; if(pipe(p)) return {};
    pid_t pid=fork();
    if(pid<0){close(p[0]);close(p[1]);return {};}
    if(pid==0){
        dup2(p[1],STDOUT_FILENO);close(p[0]);close(p[1]);
        int devnull=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if(devnull>=0){dup2(devnull,STDERR_FILENO);close(devnull);}
        std::vector<char*> a; for(auto& s:args)a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);execvp(a[0],a.data());_exit(127);
    }
    close(p[1]); CommandResult r;
    fcntl(p[0],F_SETFL,fcntl(p[0],F_GETFL)|O_NONBLOCK);
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    int status=0;bool exited=false;bool eof=false;
    while(std::chrono::steady_clock::now()<until){
        if(!exited){pid_t done=waitpid(pid,&status,WNOHANG);if(done==pid)exited=true;else if(done<0)break;}
        char buf[512];
        while(true){
            auto n=read(p[0],buf,sizeof(buf));
            if(n>0){if(r.output.size()<8192)r.output.append(buf,static_cast<size_t>(n));continue;}
            if(n==0)eof=true;
            break;
        }
        if(exited&&(eof||!r.output.empty()))break;
        struct pollfd fd{p[0],POLLIN,0};
        poll(&fd,1,20);
    }
    close(p[0]);
    if(!exited){kill(pid,SIGKILL);waitpid(pid,&status,0);return r;}
    if(WIFEXITED(status))r.exit_code=WEXITSTATUS(status);
    return r;
}
}
std::string notification_action_from_signal(const std::string& text,uint32_t id){
    // dbus-monitor's textual representation; only use after matching member.
    if(text.find("member=ActionInvoked")==std::string::npos) return {};
    if(!std::regex_search(text,std::regex("uint32[[:space:]]+"+std::to_string(id)+"([^0-9]|$)")))return {};
    for(const auto& action:{"approve","review","deny"})
        if(text.find(std::string("string \"")+action+"\"")!=std::string::npos)return action;
    return {};
}
DesktopApprovalNotification::DesktopApprovalNotification() = default;
void DesktopApprovalNotification::close_monitor(){
    if(monitor_fd_>=0){close(monitor_fd_);monitor_fd_=-1;}
    if(monitor_pid_>0){kill(monitor_pid_,SIGTERM);int st=0;waitpid(monitor_pid_,&st,0);monitor_pid_=-1;}
}
DesktopApprovalNotification::~DesktopApprovalNotification(){close_monitor();}
bool DesktopApprovalNotification::show(const std::string& tool){
    notification_id_=0;
    if(!getenv("DBUS_SESSION_BUS_ADDRESS"))return false;
    // Check actual daemon capabilities. If it does not advertise action buttons,
    // leave the in-app approval prompt as the only decision point.
    auto caps=run({"busctl","--user","call","org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","GetCapabilities"},1500);
    bool gdbus=false;
    if(caps.exit_code!=0){
        caps=run({"gdbus","call","--session","--dest","org.freedesktop.Notifications",
            "--object-path","/org/freedesktop/Notifications","--method","org.freedesktop.Notifications.GetCapabilities"},1500);
        gdbus=true;
    }
    if(caps.exit_code!=0 || caps.output.find("actions")==std::string::npos){close_monitor();return false;}
    // ActionInvoked signals are authority-bearing. Only listen to the current
    // unique bus owner of org.freedesktop.Notifications, not arbitrary apps.
    auto owner=run({"dbus-send","--session","--print-reply","--dest=org.freedesktop.DBus",
        "/org/freedesktop/DBus","org.freedesktop.DBus.GetNameOwner","string:org.freedesktop.Notifications"},1500);
    if(owner.exit_code!=0){
        owner=run({"gdbus","call","--session","--dest","org.freedesktop.DBus","--object-path","/org/freedesktop/DBus",
            "--method","org.freedesktop.DBus.GetNameOwner","org.freedesktop.Notifications"},1500);
    }
    std::smatch match;
    if(owner.exit_code!=0 || !std::regex_search(owner.output,match,std::regex("(:[0-9]+\\.[0-9]+)"))){close_monitor();return false;}
    owner_=match[1].str();
    // Monitor starts before Notify to avoid missing a fast click.
    int p[2]; if(pipe(p))return false;
    monitor_pid_=fork();
    if(monitor_pid_<0){close(p[0]);close(p[1]);return false;}
    if(monitor_pid_==0){
        dup2(p[1],STDOUT_FILENO);close(p[0]);close(p[1]);
        int devnull=open("/dev/null",O_WRONLY|O_CLOEXEC);
        if(devnull>=0){dup2(devnull,STDERR_FILENO);close(devnull);}
        const std::string match_rule="type='signal',sender='"+owner_+"',interface='org.freedesktop.Notifications',member='ActionInvoked'";
        execlp("dbus-monitor","dbus-monitor","--session",match_rule.c_str(),(char*)nullptr);
        _exit(127);
    }
    close(p[1]);monitor_fd_=p[0];
    fcntl(monitor_fd_,F_SETFL,fcntl(monitor_fd_,F_GETFL)|O_NONBLOCK);
    const std::string summary="Jamila Game: approval required";
    // Never send tool arguments or API credentials to the system notification daemon.
    const std::string safe_tool=tool.substr(0,120);
    CommandResult reply;
    if(gdbus){
        reply=run({"gdbus","call","--session","--dest","org.freedesktop.Notifications",
            "--object-path","/org/freedesktop/Notifications","--method","org.freedesktop.Notifications.Notify",
            "Jamila Game","0","",summary,safe_tool+" needs your approval",
            "['approve','OK','review','Review','deny','Deny']","{}","0"},1500);
    }else{
        reply=run({"busctl","--user","call","org.freedesktop.Notifications","/org/freedesktop/Notifications",
            "org.freedesktop.Notifications","Notify","susssasa{sv}i","Jamila Game","0","",
            summary,safe_tool+" needs your approval","6","approve","OK","review","Review","deny","Deny","0","0"},1500);
    }
    if(reply.exit_code!=0){close_monitor();return false;}
    unsigned long id=0;
    if(gdbus){
        std::smatch m;
        if(!std::regex_search(reply.output,m,std::regex("(?:uint32[[:space:]]+)?([0-9]+)"))){close_monitor();return false;}
        id=std::stoul(m[1].str());
    }else{
        std::istringstream stream(reply.output);std::string type;
        if(!(stream>>type>>id) || type!="u"){close_monitor();return false;}
    }
    if(id==0 || id>UINT32_MAX){close_monitor();return false;}
    notification_id_=static_cast<uint32_t>(id);
    return true;
}
std::string DesktopApprovalNotification::consume_line(const std::string& line){
    if(line.find("member=ActionInvoked")!=std::string::npos){
        const auto sender="sender="+owner_;
        state_=line.find(sender)!=std::string::npos &&
            (line.size()==line.find(sender)+sender.size() ||
             !std::isdigit(static_cast<unsigned char>(line[line.find(sender)+sender.size()])))?1:0;
        return {};
    }
    if(state_==1){
        const auto prefix="uint32 "+std::to_string(notification_id_);
        if(std::regex_search(line,std::regex(prefix+"([^0-9]|$)"))){state_=2;return {};}
        if(line.find("uint32 ")!=std::string::npos)state_=0;
    }else if(state_==2){
        for(const auto& action:{"approve","review","deny"}){
            if(line.find(std::string("string \"")+action+"\"")!=std::string::npos){state_=0;return action;}
        }
        if(line.find("string ")!=std::string::npos)state_=0;
    }
    return {};
}
std::string DesktopApprovalNotification::poll_action(){
    if(!active() || monitor_fd_<0)return {};
    char buf[2048];
    while(true){
        auto n=read(monitor_fd_,buf,sizeof(buf));
        if(n<0 && errno==EINTR)continue;
        if(n<=0)break;
        monitor_buffer_.append(buf,static_cast<size_t>(n));
        if(monitor_buffer_.size()>65536){monitor_buffer_.clear();state_=0;}
    }
    size_t nl;
    while((nl=monitor_buffer_.find('\n'))!=std::string::npos){
        auto line=monitor_buffer_.substr(0,nl);monitor_buffer_.erase(0,nl+1);
        auto action=consume_line(line);if(!action.empty())return action;
    }
    return {};
}
}