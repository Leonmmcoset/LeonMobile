#include "app_common.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <vterm.h>
#include <jpeglib.h>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pty.h>
#include <setjmp.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
using namespace mobile;
namespace fs=std::filesystem;
struct Calculator {
    double accumulator{};char op{};bool replace{true};std::string display="0";
    void press(const std::string &s){
        if(s=="C"){*this=Calculator{};return;}
        if(s=="⌫"){if(display.size()>1)display.pop_back();else display="0";return;}
        if(s.size()==1&&(isdigit(s[0])||s==".")){if(replace){display=s=="."?"0.":s;replace=false;}else if(display.size()<16&&(s!="."||display.find('.')==std::string::npos))display+=s;return;}
        double n=0;try{n=std::stod(display);}catch(...){display="Error";replace=true;return;}
        if(s=="±"){display=std::to_string(-n);return;}
        if(s=="%")n/=100;
        else if(op){if(op=='+')n=accumulator+n;if(op=='-')n=accumulator-n;if(op=='*')n=accumulator*n;if(op=='/')n=n==0?NAN:accumulator/n;}
        accumulator=n;op=s=="+"?'+':s=="−"?'-':s=="×"?'*':s=="÷"?'/':0;
        std::ostringstream out;out<<std::setprecision(12)<<n;display=std::isfinite(n)?out.str():"Error";replace=true;
    }
};
struct Terminal {
    int fd{-1};pid_t child{};VTerm *vt{};VTermScreen *screen{};int cols{48},rows{24};
    Terminal(){vt=vterm_new(rows,cols);vterm_set_utf8(vt,1);screen=vterm_obtain_screen(vt);vterm_screen_reset(screen,1);winsize ws{static_cast<unsigned short>(rows),static_cast<unsigned short>(cols),0,0};
        child=forkpty(&fd,nullptr,nullptr,&ws);if(!child){setenv("TERM","xterm-256color",1);chdir(user_home().c_str());execl("/bin/sh","sh",(char*)nullptr);_exit(127);}if(fd>=0)fcntl(fd,F_SETFL,O_NONBLOCK);
    }
    ~Terminal(){if(child>0){kill(child,SIGHUP);kill(child,SIGKILL);waitpid(child,nullptr,0);}if(fd>=0)close(fd);if(vt)vterm_free(vt);}
    void poll(){if(fd<0)return;char b[4096];ssize_t n;while((n=read(fd,b,sizeof(b)))>0)vterm_input_write(vt,b,size_t(n));vterm_screen_flush_damage(screen);}
    void input(const std::string &s){if(fd>=0)write(fd,s.data(),s.size());}
    void draw(Canvas &c,Rect r){
        poll();c.rounded(r,Color::hex(0x19151f),16);double cw=8.5,ch=19;int nc=std::max(8,int((r.w-20)/cw)),nr=std::max(4,int((r.h-20)/ch));
        if(nc!=cols||nr!=rows){cols=nc;rows=nr;vterm_set_size(vt,rows,cols);winsize ws{static_cast<unsigned short>(rows),static_cast<unsigned short>(cols),0,0};ioctl(fd,TIOCSWINSZ,&ws);}
        for(int y=0;y<rows;++y)for(int x=0;x<cols;++x){VTermScreenCell cell{};vterm_screen_get_cell(screen,{y,x},&cell);if(!cell.chars[0])continue;char out[8]{};uint32_t u=cell.chars[0];int len=0;
            if(u<128)out[len++]=char(u);else if(u<2048){out[len++]=char(0xc0|(u>>6));out[len++]=char(0x80|(u&63));}else if(u<65536){out[len++]=char(0xe0|(u>>12));out[len++]=char(0x80|((u>>6)&63));out[len++]=char(0x80|(u&63));}else{out[len++]=char(0xf0|(u>>18));out[len++]=char(0x80|((u>>12)&63));out[len++]=char(0x80|((u>>6)&63));out[len++]=char(0x80|(u&63));}
            c.text(std::string(out,len),{r.x+10+x*cw,r.y+10+y*ch,cw*2,ch},14,Color::hex(0xeee6f4));
        }
    }
};
struct JpegError { jpeg_error_mgr base; jmp_buf jump; };
static cairo_surface_t *load_image(const std::string &path){
    if(fs::path(path).extension()==".png")return cairo_image_surface_create_from_png(path.c_str());
    FILE *f=fopen(path.c_str(),"rb");if(!f)return nullptr;jpeg_decompress_struct d{};JpegError err{};d.err=jpeg_std_error(&err.base);err.base.error_exit=[](j_common_ptr ptr){longjmp(reinterpret_cast<JpegError *>(ptr->err)->jump,1);};
    if(setjmp(err.jump)){jpeg_destroy_decompress(&d);fclose(f);return nullptr;}
    jpeg_create_decompress(&d);jpeg_stdio_src(&d,f);jpeg_read_header(&d,TRUE);d.out_color_space=JCS_RGB;
    if(d.image_width>8192||d.image_height>8192){jpeg_destroy_decompress(&d);fclose(f);return nullptr;}
    jpeg_start_decompress(&d);auto *s=cairo_image_surface_create(CAIRO_FORMAT_RGB24,int(d.output_width),int(d.output_height));
    if(cairo_surface_status(s)){cairo_surface_destroy(s);jpeg_destroy_decompress(&d);fclose(f);return nullptr;}
    std::vector<unsigned char> row(d.output_width*3);auto *data=cairo_image_surface_get_data(s);int stride=cairo_image_surface_get_stride(s);
    while(d.output_scanline<d.output_height){auto y=d.output_scanline;JSAMPROW p=row.data();jpeg_read_scanlines(&d,&p,1);auto *dst=reinterpret_cast<uint32_t *>(data+y*stride);for(unsigned x=0;x<d.output_width;++x)dst[x]=(unsigned(row[x*3])<<16)|(unsigned(row[x*3+1])<<8)|row[x*3+2];}
    jpeg_finish_decompress(&d);jpeg_destroy_decompress(&d);fclose(f);cairo_surface_mark_dirty(s);return s;
}
int main(int argc,char **argv){
    std::string mode=argc>1?argv[1]:"gallery",id="org.leon."+mode;Application app(id,"LeonMobile");std::vector<Hit> hits;std::string error;
    bool en=false,keyboard=false,input_focus=false;Json prefs={{"dark",false},{"language","zh-CN"},{"navigation","gestures"},{"volume",70}};
    Calculator calculator;std::string editor,path=user_home()+"/note.txt",image_path=user_home()+"/image.png",file_input="new.txt";double scroll=0;int focus=0;
    fs::path cwd=user_home();fs::create_directories(cwd);
    std::unique_ptr<Terminal> terminal;if(mode=="terminal")terminal=std::make_unique<Terminal>();
    cairo_surface_t *image=nullptr;bool demo_switch=false;double demo_slider=.4;std::string demo_input;
    auto start=std::chrono::steady_clock::now();bool stopwatch=false;double elapsed=0;
    auto run=[&](std::function<void()> fn){try{fn();error.clear();}catch(const std::exception &e){error=e.what();}app.redraw();};
    auto poll_prefs=[&]{try{prefs=rpc({{"op","settings"}}).at("settings");en=prefs["language"]=="en";app.dark=prefs.value("dark",false);}catch(...){};};poll_prefs();
    auto set=[&](std::string k,Json v){run([&]{prefs=rpc({{"op","set"},{"key",k},{"value",v}}).at("settings");poll_prefs();});};
    app.tick=[&]{poll_prefs();if(mode=="clock"||terminal)app.redraw();};
    auto text_input=[&](std::string s){
        if(terminal){terminal->input(s);return;}
        std::string *target=mode=="editor"?(focus==1?&path:&editor):mode=="images"?&image_path:mode=="files"?&file_input:&demo_input;
        if(s=="\b")backspace(*target);else if(target->size()<1024*1024)*target+=s;
    };
    app.key=[&](const KeyEvent &e){if(!e.pressed)return;if(e.symbol==XKB_KEY_Escape){keyboard=false;return;}if(e.symbol==XKB_KEY_BackSpace)text_input("\b");else if(e.symbol==XKB_KEY_Return)text_input(terminal?"\r":"\n");else if(!e.text.empty())text_input(e.text);};
    app.scroll=[&](double d){scroll=std::max(0.0,scroll+d);app.redraw();};
    app.pointer=[&](double x,double y,bool down){if(down)run([&]{activate(hits,x,y);});};
    app.paint=[&](Canvas &c){
        hits.clear();auto t=c.theme;double w=c.width,h=c.height;std::string title=mode;
        if(mode=="settings")title=tr(en,"设置","Settings");if(mode=="calculator")title=tr(en,"计算器","Calculator");if(mode=="gallery")title="LMUI";if(mode=="files")title=tr(en,"文件","Files");if(mode=="editor")title=tr(en,"文本编辑器","Text editor");if(mode=="terminal")title=tr(en,"终端","Terminal");if(mode=="images")title=tr(en,"图片","Images");if(mode=="clock")title=tr(en,"时钟","Clock");
        c.text(title,{24,12,w-100,58},30,t.text,true);button(c,hits,{w-64,22,40,40},"×",[&]{app.quit();});
        double bottom=keyboard?h-250:h-16;
        if(mode=="settings"){
            c.text("LeonMobile 1.0.0",{24,82,w-48,34},20,t.primary,true);
            c.text("Linux 7.3.1 · ARM64 · musl",{24,118,w-48,30},14,t.muted);
            auto tree=Column({Toggle(tr(en,"深色模式","Dark theme"),app.dark,[&]{set("dark",!app.dark);}),Button(tr(en,"语言：简体中文 → English","Language: English → 简体中文"),[&]{set("language",en?"zh-CN":"en");},false),Button(tr(en,"切换手势 / 三键导航","Switch gestures / buttons"),[&]{set("navigation",prefs["navigation"]=="gestures"?"buttons":"gestures");},false),Label(tr(en,"播放音量","Playback volume")),Slider(prefs.value("volume",70)/100.0,[&](double v){set("volume",int(v*100));})});
            tree.layout({24,170,w-48,310});tree.draw(c);for(auto &child:tree.children){auto a=child.activate;auto change=child.change;auto r=child.bounds;click(hits,r,[&,a,change,r]{if(a)a();if(change)change(.7);});}
            c.text(tr(en,"设备：QEMU ARM64 开发原型","Device: QEMU ARM64 prototype"),{24,510,w-48,40},14,t.muted);
        }else if(mode=="calculator"){
            c.rounded({20,88,w-40,112},t.container,28);c.text(calculator.display,{36,102,w-72,84},42,t.text,false,2);
            std::vector<std::string> keys={"C","±","%","÷","7","8","9","×","4","5","6","−","1","2","3","+","⌫","0",".","="};double bh=std::min(78.0,(bottom-226)/5),bw=(w-64)/4;
            for(size_t i=0;i<keys.size();++i){auto k=keys[i];button(c,hits,{20+(i%4)*(bw+8),218+(i/4)*(bh+8),bw,bh},k,[&,k]{calculator.press(k);},k=="=");}
        }else if(mode=="gallery"){
            c.text(tr(en,"为移动端设计的原生组件","Native components for mobile"),{24,80,w-48,40},18,t.muted);
            auto tree=Column({Button(tr(en,"显示通知","Show notification"),[&]{run([&]{rpc({{"op","notify"},{"title",tr(en,"你好，LeonMobile！","Hello, LeonMobile!")}});});}),Toggle(tr(en,"开关组件","Switch component"),demo_switch,[&]{demo_switch=!demo_switch;}),Label(tr(en,"滑块组件","Slider component")),Slider(demo_slider,[&](double v){demo_slider=v;}),TextField(demo_input.empty()?tr(en,"点此输入文字","Tap to type"):demo_input,[&]{keyboard=true;input_focus=true;}),Button(tr(en,"切换深浅主题","Toggle theme"),[&]{app.dark=!app.dark;},false)});
            tree.layout({24,142,w-48,340});tree.draw(c);for(const auto &child:tree.children){auto a=child.activate;auto change=child.change;auto r=child.bounds;click(hits,r,[a,change]{if(a)a();if(change)change(.75);});}
            c.rounded({24,520,w-48,90},t.container,28);c.text("Material You · LMUI 1",{40,536,w-80,56},22,t.primary,true,1);
        }else if(mode=="editor"){
            button(c,hits,{20,84,w-132,44},path,[&]{focus=1;keyboard=true;});button(c,hits,{w-104,84,84,44},tr(en,"打开","Open"),[&]{run([&]{if(fs::file_size(path)>1024*1024)throw std::runtime_error("File exceeds 1 MiB");std::ifstream f(path);editor.assign(std::istreambuf_iterator<char>(f),{});focus=0;});});
            c.rounded({20,144,w-40,bottom-222},t.surface,16);c.text(editor.empty()?tr(en,"点击开始编辑…","Tap to edit…"):editor,{32,154,w-64,bottom-242},16,t.text);click(hits,{20,144,w-40,bottom-222},[&]{focus=0;keyboard=true;});
            button(c,hits,{20,bottom-60,w-40,48},tr(en,"保存到应用目录","Save to application directory"),[&]{run([&]{std::ofstream f(path);if(!f)throw std::runtime_error("Cannot open file");f<<editor;if(!f)throw std::runtime_error("Write failed");});},true);
        }else if(mode=="files"){
            c.text(cwd.string(),{24,78,w-48,40},13,t.muted);button(c,hits,{20,126,72,44},"↑",[&]{cwd=cwd.parent_path();scroll=0;});button(c,hits,{100,126,w-120,44},file_input,[&]{keyboard=true;});
            button(c,hits,{20,180,(w-48)/2,44},tr(en,"新建文件","New file"),[&]{run([&]{if(file_input.empty()||file_input.find('/')!=std::string::npos||file_input=="..")throw std::runtime_error("Invalid filename");std::ofstream f(cwd/file_input);if(!f)throw std::runtime_error("Cannot create file");});});
            button(c,hits,{28+(w-48)/2,180,(w-48)/2,44},tr(en,"新建目录","New folder"),[&]{run([&]{if(file_input.empty()||file_input.find('/')!=std::string::npos||file_input=="..")throw std::runtime_error("Invalid filename");fs::create_directory(cwd/file_input);});});
            std::vector<fs::directory_entry> entries;std::error_code ec;for(auto &e:fs::directory_iterator(cwd,fs::directory_options::skip_permission_denied,ec))entries.push_back(e);std::sort(entries.begin(),entries.end(),[](auto &a,auto &b){return a.path()<b.path();});
            double y=242-scroll;for(auto &entry:entries){if(y>=230&&y+54<bottom){auto p=entry.path();bool dir=entry.is_directory(ec);button(c,hits,{20,y,w-94,48},(dir?"▣  ":"·  ")+p.filename().string(),[&,p,dir]{if(dir){cwd=p;scroll=0;}else{std::ifstream f(p);std::string data(512,0);f.read(data.data(),data.size());data.resize(size_t(f.gcount()));error=data;}});button(c,hits,{w-66,y,46,48},"×",[&,p]{run([&]{fs::remove(p);});});}y+=60;}
        }else if(mode=="terminal"){
            terminal->draw(c,{16,84,w-32,bottom-148});button(c,hits,{20,bottom-52,w-40,44},tr(en,"键盘","Keyboard"),[&]{keyboard=!keyboard;});
        }else if(mode=="images"){
            button(c,hits,{20,84,w-132,48},image_path,[&]{keyboard=true;});button(c,hits,{w-104,84,84,48},tr(en,"打开","Open"),[&]{if(image)cairo_surface_destroy(image);image=load_image(image_path);if(!image||cairo_surface_status(image))error="Cannot decode PNG/JPEG";keyboard=false;});
            if(image)c.image(image,{20,154,w-40,bottom-180});else c.text(tr(en,"在应用目录放入 PNG 或 JPEG 图片","Place a PNG or JPEG in the application directory"),{32,220,w-64,120},18,t.muted,false,1);
        }else if(mode=="clock"){
            std::time_t now=std::time(nullptr);std::tm tm{};localtime_r(&now,&tm);char time[32],date[64];std::strftime(time,sizeof(time),"%H:%M:%S",&tm);std::strftime(date,sizeof(date),"%Y-%m-%d",&tm);
            c.text(time,{20,160,w-40,110},64,t.primary,false,1);c.text(date,{20,276,w-40,44},20,t.muted,false,1);
            double seconds=elapsed+(stopwatch?std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count():0);std::ostringstream s;s<<std::fixed<<std::setprecision(1)<<seconds<<" s";c.text(s.str(),{20,380,w-40,70},36,t.text,false,1);
            button(c,hits,{24,474,(w-64)/2,52},stopwatch?tr(en,"暂停","Pause"):tr(en,"秒表","Stopwatch"),[&]{if(stopwatch)elapsed+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();else start=std::chrono::steady_clock::now();stopwatch=!stopwatch;},true);button(c,hits,{40+(w-64)/2,474,(w-64)/2,52},tr(en,"复位","Reset"),[&]{elapsed=0;start=std::chrono::steady_clock::now();});
        }
        if(!error.empty()){c.rounded({16,bottom-108,w-32,48},t.container,16);c.text(error,{28,bottom-108,w-56,48},12,t.text);}
        if(keyboard){
            c.rounded({0,h-246,w,246},t.container,20);std::vector<std::string> rows={"qwertyuiop","asdfghjkl","zxcvbnm"};
            for(size_t r=0;r<rows.size();++r){double kw=(w-20)/rows[r].size();for(size_t i=0;i<rows[r].size();++i){std::string s(1,rows[r][i]);button(c,hits,{10+i*kw,h-236+r*52,kw-4,46},s,[&,s]{text_input(s);});}}
            button(c,hits,{10,h-78,56,48},"⌄",[&]{keyboard=false;});button(c,hits,{72,h-78,w-224,48},tr(en,"空格","Space"),[&]{text_input(" ");});button(c,hits,{w-146,h-78,62,48},"⌫",[&]{text_input("\b");});button(c,hits,{w-78,h-78,68,48},"↵",[&]{text_input(terminal?"\r":"\n");},true);
        }
    };
    int result=app.run();if(image)cairo_surface_destroy(image);return result;
}
