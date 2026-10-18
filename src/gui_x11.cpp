#include "jamila/core.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <stdexcept>

namespace {
constexpr int SIDEBAR = 228;
constexpr unsigned long DARK = 0x11110F;
constexpr unsigned long CREAM = 0xF4EEDF;
constexpr unsigned long MUTED = 0xB9B09F;
constexpr unsigned long BORDER = 0xCFC4B1;
constexpr unsigned long PAPER_INK = 0x201F1B;
struct View {
    Display* d;
    Window w;
    GC gc;
    unsigned width = 1100, height = 740;
    std::string input;
    std::string status = "Ready  |  Ctrl+Enter to send  |  Esc to clear input";
    int scroll = 0;
    jamila::Agent& agent;
    void rectangle(int x,int y,unsigned wi,unsigned he,unsigned long color) {
        XSetForeground(d,gc,color); XFillRectangle(d,w,gc,x,y,wi,he);
    }
    void label(int x,int y,const std::string& s,unsigned long color=CREAM) {
        XSetForeground(d,gc,color);
        XDrawString(d,w,gc,x,y,s.c_str(),static_cast<int>(s.size()));
    }
    std::vector<std::string> wrap(const std::string& raw, size_t line_length) {
        std::vector<std::string> out;
        size_t start = 0;
        while (start < raw.size()) {
            size_t stop = std::min(raw.size(), start + line_length);
            auto newline = raw.find('\n', start);
            if (newline != std::string::npos && newline < stop) stop = newline;
            else if (stop < raw.size()) {
                auto space = raw.rfind(' ', stop);
                if (space != std::string::npos && space > start) stop = space;
            }
            out.emplace_back(raw.substr(start, stop-start));
            start = stop;
            if (start < raw.size() && (raw[start] == '\n' || raw[start] == ' ')) ++start;
        }
        if (out.empty()) out.push_back("");
        return out;
    }
    void draw() {
        rectangle(0,0,width,height,CREAM);
        rectangle(0,0,SIDEBAR,height,DARK);
        rectangle(SIDEBAR,0,width-SIDEBAR,76,PAPER_INK);
        label(28,52,"JAMILA",CREAM);
        label(28,75,"G A M E  /  N A T I V E",MUTED);
        rectangle(18,118,190,39,CREAM); label(34,144,"+  NEW TASK",DARK);
        label(30,198,"WORKSPACE",MUTED);
        auto workspace = agent.config().workspace.filename().string();
        label(30,225,workspace.substr(0,24));
        label(30,287,"HISTORY",MUTED);
        label(30,313,std::to_string(agent.history().size()/2) + " conversations");
        label(30,height-54,"C++  /  ZIG  /  NOQERI",MUTED);
        label(SIDEBAR+30,43,"JAMILA  /  CONVERSATION",CREAM);
        label(width-158,43,"BLACK + CREAM",MUTED);
        label(SIDEBAR+28,108,"Your coding workspace",PAPER_INK);
        label(SIDEBAR+28,127,"Local actions require approval in the CLI. Chat never executes commands.",PAPER_INK);
        std::vector<std::pair<std::string,unsigned long>> lines;
        if (agent.history().empty()) {
            lines.push_back({"Welcome to Jamila Game.",PAPER_INK});
            lines.push_back({"Connect a provider using JAMILA_API_KEY and JAMILA_MODEL.",PAPER_INK});
            lines.push_back({"Open the CLI for workspace file/command tools.",PAPER_INK});
        }
        for (auto& m : agent.history()) {
            lines.push_back({m.role == "user" ? "YOU" : "JAMILA",PAPER_INK});
            for (auto& text : wrap(m.content, std::max<size_t>(15,(width-SIDEBAR-88)/8))) lines.push_back({text,PAPER_INK});
            lines.push_back({"",PAPER_INK});
        }
        const int page = std::max(1, static_cast<int>((height-335)/21));
        const int max_scroll = std::max(0,static_cast<int>(lines.size())-page);
        scroll = std::clamp(scroll,0,max_scroll);
        for (int i=scroll; i < static_cast<int>(lines.size()) && i < scroll+page; ++i)
            label(SIDEBAR+30,167+(i-scroll)*21,lines[i].first,lines[i].second);
        rectangle(SIDEBAR+20,height-148,width-SIDEBAR-40,2,BORDER);
        rectangle(SIDEBAR+20,height-134,width-SIDEBAR-40,62,DARK);
        auto visible = input.substr(input.size() > 85 ? input.size()-85 : 0);
        label(SIDEBAR+35,height-99,visible.empty()?"Type your message...":visible,visible.empty()?MUTED:CREAM);
        rectangle(width-137,height-65,117,36,PAPER_INK);
        label(width-117,height-42,"SEND  >",CREAM);
        label(SIDEBAR+24,height-39,status.substr(0,std::max<size_t>(8,(width-SIDEBAR-175)/8)),PAPER_INK);
        XFlush(d);
    }
    void send() {
        if (input.empty()) return;
        auto prompt = input;
        input.clear();
        status = "Contacting model...";
        draw();
        try { agent.ask(prompt); status = "Response received"; scroll = 10000000; }
        catch (const std::exception& e) { status = std::string("Error: ") + e.what(); }
        draw();
    }
};
}
void launch_window(jamila::Agent& agent) {
    Display* d = XOpenDisplay(nullptr);
    if (!d) throw std::runtime_error("could not open X display");
    const int s = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d,RootWindow(d,s),80,50,1100,740,0,DARK,CREAM);
    XStoreName(d,w,"Jamila Game | Native Coding Assistant");
    XSelectInput(d,w,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);
    Atom wm_delete = XInternAtom(d,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(d,w,&wm_delete,1);
    XMapWindow(d,w);
    GC gc = XCreateGC(d,w,0,nullptr);
    XFontStruct* font=XLoadQueryFont(d,"fixed");
    if (font) XSetFont(d,gc,font->fid);
    View v{d,w,gc,1100,740,"","Ready  |  Ctrl+Enter to send  |  Esc to clear input",0,agent};
    bool running=true;
    while (running) {
        XEvent e; XNextEvent(d,&e);
        if (e.type==Expose && e.xexpose.count==0) v.draw();
        else if (e.type==ConfigureNotify) { v.width=std::max(650,e.xconfigure.width); v.height=std::max(460,e.xconfigure.height); v.draw(); }
        else if (e.type==ClientMessage && static_cast<Atom>(e.xclient.data.l[0])==wm_delete) running=false;
        else if (e.type==ButtonPress) {
            if (e.xbutton.button==4) v.scroll-=3;
            else if (e.xbutton.button==5) v.scroll+=3;
            else if (e.xbutton.x<210 && e.xbutton.y>=118 && e.xbutton.y<=157) { agent.clear(); v.scroll=0; v.status="New task"; }
            else if (e.xbutton.x>static_cast<int>(v.width)-140 && e.xbutton.y>static_cast<int>(v.height)-70) v.send();
            v.draw();
        } else if (e.type==KeyPress) {
            KeySym k=NoSymbol; char buf[16]={0}; int n=XLookupString(&e.xkey,buf,sizeof(buf),&k,nullptr);
            if (k==XK_Escape) v.input.clear();
            else if (k==XK_BackSpace && !v.input.empty()) v.input.pop_back();
            else if (k==XK_Return && (e.xkey.state & ControlMask)) v.send();
            else if (k==XK_Return && v.input.size()<32768) v.input+='\n';
            else if (n>0 && v.input.size()+static_cast<size_t>(n)<=32768) {
                for(int i=0;i<n;++i) if (std::isprint(static_cast<unsigned char>(buf[i]))) v.input+=buf[i];
            }
            v.draw();
        }
    }
    if (font) XFreeFont(d,font);
    XFreeGC(d,gc); XDestroyWindow(d,w); XCloseDisplay(d);
}
