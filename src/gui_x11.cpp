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
constexpr int SIDEBAR = 246;
constexpr unsigned long DARK = 0x11110F;
constexpr unsigned long CREAM = 0xF4EEDF;
constexpr unsigned long MUTED = 0xB9B09F;
constexpr unsigned long BORDER = 0xCFC4B1;
constexpr unsigned long PAPER_INK = 0x201F1B;
constexpr unsigned long PANEL = 0x292824;
constexpr unsigned long ACCENT = 0xDED0B8;
struct View {
    Display* d;
    Window w;
    GC gc;
    unsigned width = 1100, height = 740;
    std::string input;
    std::string status = "Ready  |  Ctrl+Enter to send  |  Esc to clear input";
    int scroll = 0;
    bool awaiting_approval = false;
    std::string approve_name, approve_args;
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
        rectangle(SIDEBAR,0,width-SIDEBAR,79,PAPER_INK);
        label(27,45,"JAMILA",CREAM);
        label(27,68,"GAME DEV   /   0.2",MUTED);
        rectangle(17,100,SIDEBAR-34,37,CREAM); label(33,124,"+  NEW SESSION",DARK);
        label(27,174,"GODOT PROJECT",MUTED);
        auto workspace = agent.config().workspace.filename().string();
        label(27,200,workspace.substr(0,28));
        label(27,225,agent.has_mcp()?"MCP  CONNECTED":"MCP  DISCONNECTED",agent.has_mcp()?CREAM:MUTED);
        label(27,243,std::to_string(agent.mcp_tools().size())+" tools discovered",MUTED);
        label(27,287,"QUICK START",MUTED);
        const char* actions[]={"01   Inspect project","02   Debug errors","03   Build a scene","04   Playtest game"};
        for(int i=0;i<4;++i) {
            rectangle(17,303+i*46,SIDEBAR-34,40,PANEL);
            label(28,327+i*46,actions[i],CREAM);
        }
        label(27,529,"AGENT ACTIVITY",MUTED);
        const auto& events=agent.activity();
        if (events.empty()) label(27,555,"No tool calls yet",MUTED);
        for(size_t i=events.size()>3?events.size()-3:0;i<events.size();++i) {
            auto text=events[i].substr(0,29);
            label(27,555+static_cast<int>(i-(events.size()>3?events.size()-3:0))*21,text,CREAM);
        }
        label(27,height-59,"C++ CORE / MCP TOOLS",MUTED);
        label(27,height-39,"ZIG + NOQERI  /  LINUX",MUTED);
        label(SIDEBAR+27,40,"GODOT  /  AI DEVELOPMENT WORKSPACE",CREAM);
        label(width-145,41,agent.has_mcp()?"TOOLS ONLINE":"CHAT ONLY",ACCENT);
        label(SIDEBAR+29,110,"Build, debug and playtest your game",PAPER_INK);
        label(SIDEBAR+29,133,"Godot MCP  |  Scene tools  |  Scripts  |  Human-reviewed changes",PAPER_INK);
        rectangle(SIDEBAR+20,144,width-SIDEBAR-40,1,BORDER);
        std::vector<std::pair<std::string,unsigned long>> lines;
        if(agent.history().empty()) {
            lines.push_back({"WELCOME TO YOUR GAME DEVELOPMENT WORKSPACE",PAPER_INK});
            lines.push_back({"1. Open a Godot project using --workspace /path/to/game",PAPER_INK});
            lines.push_back({"2. Connect Godot MCP using --godot-mcp",PAPER_INK});
            lines.push_back({"3. Ask to create scenes, fix scripts, or playtest.",PAPER_INK});
            lines.push_back({"Godot MCP changes are reviewed before execution.",PAPER_INK});
        }
        for(const auto& m:agent.history()) {
            lines.push_back({m.role=="user"?"YOU  /  TASK":"JAMILA  /  RESULT",PAPER_INK});
            for(const auto& line:wrap(m.content,std::max<size_t>(15,(width-SIDEBAR-88)/8)))
                lines.push_back({line,PAPER_INK});
            lines.push_back({"",PAPER_INK});
        }
        const int page=std::max(1,static_cast<int>((height-340)/21));
        const int max_scroll=std::max(0,static_cast<int>(lines.size())-page);
        scroll=std::clamp(scroll,0,max_scroll);
        for(int i=scroll;i<static_cast<int>(lines.size())&&i<scroll+page;++i)
            label(SIDEBAR+30,180+(i-scroll)*21,lines[i].first,lines[i].second);
        rectangle(SIDEBAR+20,height-153,width-SIDEBAR-40,1,BORDER);
        label(SIDEBAR+26,height-135,"TASK PROMPT",PAPER_INK);
        rectangle(SIDEBAR+20,height-126,width-SIDEBAR-40,65,DARK);
        auto visible=input.substr(input.size()>88?input.size()-88:0);
        for(char& c:visible) if(c=='\n') c=' ';
        label(SIDEBAR+34,height-86,visible.empty()?"Describe what you want to build...":visible,visible.empty()?MUTED:CREAM);
        rectangle(width-139,height-58,117,34,PAPER_INK);
        label(width-120,height-36,"RUN TASK >",CREAM);
        label(SIDEBAR+25,height-34,status.substr(0,std::max<size_t>(8,(width-SIDEBAR-180)/8)),PAPER_INK);
        if(awaiting_approval) {
            const int bx=SIDEBAR+40, by=height/2-145, bw=static_cast<int>(width)-SIDEBAR-80;
            rectangle(SIDEBAR+16,79,width-SIDEBAR-32,height-155,PANEL);
            rectangle(bx,by,bw,280,CREAM);
            rectangle(bx,by,bw,45,DARK);
            label(bx+18,by+28,"REVIEW GODOT MCP ACTION",CREAM);
            label(bx+20,by+74,("TOOL: "+approve_name).substr(0,65),PAPER_INK);
            auto lines=wrap(approve_args,static_cast<size_t>(std::max(22,(bw-45)/8)));
            for(size_t i=0;i<std::min<size_t>(4,lines.size());++i)
                label(bx+20,by+100+static_cast<int>(i)*19,lines[i],PAPER_INK);
            label(bx+20,by+204,"Approve only if you trust this operation.",PAPER_INK);
            rectangle(bx+20,by+224,145,37,DARK);
            label(bx+34,by+249,"Y   APPROVE",CREAM);
            rectangle(bx+182,by+224,142,37,ACCENT);
            label(bx+199,by+249,"N   DENY",PAPER_INK);
        }
        XFlush(d);
    }
    bool confirm(const std::string& name,const std::string& args) {
        awaiting_approval=true;
        approve_name=name;
        approve_args=args.substr(0,1500);
        draw();
        while(true) {
            XEvent e;
            XNextEvent(d,&e);
            if(e.type==KeyPress) {
                KeySym sym=NoSymbol;
                char buffer[16]={0};
                XLookupString(&e.xkey,buffer,sizeof(buffer),&sym,nullptr);
                if(sym==XK_y || sym==XK_Y || sym==XK_Return) { awaiting_approval=false; draw(); return true; }
                if(sym==XK_n || sym==XK_N || sym==XK_Escape) { awaiting_approval=false; draw(); return false; }
            } else if(e.type==ButtonPress) {
                int bx=SIDEBAR+40, by=height/2-145;
                if(e.xbutton.y>=by+224 && e.xbutton.y<=by+261) {
                    if(e.xbutton.x>=bx+20 && e.xbutton.x<=bx+165) { awaiting_approval=false; draw(); return true; }
                    if(e.xbutton.x>=bx+182 && e.xbutton.x<=bx+324) { awaiting_approval=false; draw(); return false; }
                }
            } else if(e.type==Expose || e.type==ConfigureNotify) {
                if(e.type==ConfigureNotify) { width=std::max(760,e.xconfigure.width); height=std::max(620,e.xconfigure.height); }
                draw();
            } else if(e.type==ClientMessage) { awaiting_approval=false; return false; }
        }
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
    XStoreName(d,w,"Jamila Game | Godot Development Agent");
    XSelectInput(d,w,ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask);
    Atom wm_delete = XInternAtom(d,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(d,w,&wm_delete,1);
    XMapWindow(d,w);
    GC gc = XCreateGC(d,w,0,nullptr);
    XFontStruct* font=XLoadQueryFont(d,"fixed");
    if (font) XSetFont(d,gc,font->fid);
    View v{d,w,gc,1100,740,"","Ready | Ctrl+Enter to send | Esc to clear",0,false,"","",agent};
    agent.set_approval([&v](const std::string& name,const std::string& args) { return v.confirm(name,args); });
    bool running=true;
    while (running) {
        XEvent e; XNextEvent(d,&e);
        if (e.type==Expose && e.xexpose.count==0) v.draw();
        else if (e.type==ConfigureNotify) { v.width=std::max(760,e.xconfigure.width); v.height=std::max(620,e.xconfigure.height); v.draw(); }
        else if (e.type==ClientMessage && static_cast<Atom>(e.xclient.data.l[0])==wm_delete) running=false;
        else if (e.type==ButtonPress) {
            if (e.xbutton.button==4) v.scroll-=3;
            else if (e.xbutton.button==5) v.scroll+=3;
            else if (e.xbutton.x<SIDEBAR-15 && e.xbutton.y>=100 && e.xbutton.y<=137) { agent.clear(); v.scroll=0; v.status="New session"; }
            else if (e.xbutton.x<SIDEBAR-17 && e.xbutton.y>=303 && e.xbutton.y<487) {
                int index=(e.xbutton.y-303)/46;
                const char* templates[]={
                    "Inspect my Godot project's scenes, scripts and structure, identify bugs and give a prioritized plan.",
                    "Inspect the Godot project's current errors and fix the most important script or scene bug. Ask before edits.",
                    "Help me design and implement one reusable scene for this game. Inspect existing scenes first.",
                    "Use Godot MCP to run the project, inspect debug output, and explain failures and fixes. Ask before execution."
                };
                v.input=templates[std::clamp(index,0,3)];
                v.status="Template selected. Press Ctrl+Enter to run.";
            }
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
