#include "jamila/notifications.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
int main(){
    char buf[]="/tmp/jamila-notify-XXXXXX";auto* tmp=mkdtemp(buf);assert(tmp);
    const fs::path root(tmp);
    {
        std::ofstream f(root/"busctl");
        f<<"#!/bin/sh\ncase \"$*\" in *GetCapabilities*) echo 'as 2 \"actions\" \"body\"' ;; *Notify*) echo 'u 17' ;; *) exit 1 ;; esac\n";
    }
    {
        std::ofstream f(root/"dbus-monitor");
        f<<"#!/bin/sh\nsleep 0.2\nprintf 'signal sender=:1.4 interface=org.freedesktop.Notifications member=ActionInvoked\\n   uint32 17\\n   string \"review\"\\n'\nsleep 2\n";
    }
    chmod((root/"busctl").c_str(),0700);chmod((root/"dbus-monitor").c_str(),0700);
    const char* oldpath=getenv("PATH");std::string path=root.string()+":"+(oldpath?oldpath:"");
    setenv("PATH",path.c_str(),1);setenv("DBUS_SESSION_BUS_ADDRESS","mock-local-bus",1);
    {
        jamila::DesktopApprovalNotification n;
        assert(n.show("godot__create_scene"));
        std::string action;
        for(int i=0;i<40 && action.empty();++i){
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            action=n.poll_action();
        }
        assert(action=="review");
    }
    unsetenv("DBUS_SESSION_BUS_ADDRESS");
    {jamila::DesktopApprovalNotification n;assert(!n.show("tool"));}
    fs::remove_all(root);
    std::cout<<"System notification command, action signal and no-bus fallback passed (mocked bus commands)\n";
}