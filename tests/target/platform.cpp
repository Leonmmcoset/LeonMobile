#include <leon/platform.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <signal.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include "system_probe.h"
}
using J=nlohmann::json;
static std::ofstream report;
static J call(const char *service,J request,uid_t uid=0) {
    auto text=request.dump();
    if(probe_rpc(service,text.c_str(),uid)) throw std::runtime_error(std::string("transport: ")+service);
    return J::parse(probe_reply());
}
static J ok(const char *service,J request,uid_t uid=0) {
    auto result=call(service,std::move(request),uid);
    if(!result.value("ok",false)) throw std::runtime_error(result.dump());
    return result;
}
static void check(bool result,const char *message){if(!result) throw std::runtime_error(message);}
static void passed(const char *message){report<<"PASS "<<message<<std::endl;}
static void wait_audio() {
    for(int i=0;i<150;++i) {
        auto status=ok("audio",{{"op","status"}});
        if(status.value("backend_ready",false)&&!status.at("devices").empty()) return;
        usleep(100000);
    }
    throw std::runtime_error("PipeWire sink unavailable");
}
static pid_t audio_backend_pid() {
    DIR *directory=opendir("/proc");
    check(directory,"cannot inspect guest processes");
    pid_t pid=0;
    while(auto *entry=readdir(directory)) {
        if(entry->d_name[0]<'0'||entry->d_name[0]>'9') continue;
        std::ifstream comm(std::string("/proc/")+entry->d_name+"/comm");
        std::string name;std::getline(comm,name);
        if(name=="pipewire") {pid=pid_t(std::stoi(entry->d_name));break;}
    }
    closedir(directory);
    check(pid>1,"guest PipeWire process unavailable");return pid;
}
static void run() {
    auto initial=ok("settings",{{"op","status"}});
    check(!call("settings",{{"op","set"},{"key","dark"},{"value",true}},11002).at("ok"),"settings spoofing");
    auto next=ok("settings",{{"op","set"},{"key","dark"},{"value",true}},11000);
    check(next.at("settings").at("dark"),"settings update");
    check(ok("apps",{{"op","settings"}}).at("settings").at("dark"),"settings broker relay");
    ok("settings",{{"op","update"},{"values",initial.at("settings")}},11000);
    passed("settingsd real UID policy, durable generation and broker compatibility relay");
    std::ofstream("/data/leon/shared/platform-note.txt")<<"LeonMobile platform document\n";
    auto document=ok("documents",{{"op","choose"}},11004).at("token");
    check(!call("documents",{{"op","pick"},{"token",document},{"file","platform-note.txt"}},11004).at("ok"),"untrusted picker");
    check(!call("documents",{{"op","pick"},{"token",document},{"file","../settings.json"}},1000).at("ok"),"document traversal");
    ok("documents",{{"op","pick"},{"token",document},{"file","platform-note.txt"}},1000);
    auto data=ok("documents",{{"op","read"},{"token",document},{"offset",0},{"length",2048}},11004);
    check(data.at("hex")=="4c656f6e4d6f62696c6520706c6174666f726d20646f63756d656e740a","document content");
    check(!call("documents",{{"op","read"},{"token",document},{"offset",0},{"length",2048}},11002).at("ok"),"foreign read");
    ok("documents",{{"op","revoke"},{"token",document}},11000);
    check(!call("documents",{{"op","read"},{"token",document},{"offset",0},{"length",20}},11004).at("ok"),"revocation");
    passed("documentd trusted picker, exact binary transfer, traversal/peer denial and Settings revocation");
    auto interaction=ok("intents",{{"op","request"},{"action","share"},{"mime","text/plain"},{"text","Target interaction text"}},11002).at("token");
    check(!call("intents",{{"op","choose"},{"token",interaction},{"target","org.leon.editor"}},11002).at("ok"),"chooser spoofing");
    ok("intents",{{"op","choose"},{"token",interaction},{"target","org.leon.editor"},{"always",true}},1000);
    bool received=false;
    for(int i=0;i<150;++i){auto result=ok("intents",{{"op","result"},{"token",interaction}},11002);if(result.value("acknowledged",false)){received=true;break;}usleep(100000);}
    check(received,"editor did not acknowledge interaction");
    ok("apps",{{"op","save"},{"id","org.leon.editor"}});
    usleep(300000);
    std::ifstream session("/data/leon/data/org.leon.editor/.session.json");J saved;session>>saved;
    check(saved.at("state").at("text")=="Target interaction text","editor content");
    ok("intents",{{"op","cancel"},{"token",interaction}},11002);
    ok("apps",{{"op","stop"},{"id","org.leon.editor"}});
    ok("intents",{{"op","clear_default"},{"key","share:mime:text/plain"}},11000);
    passed("intentd selection/launch, real sandboxed editor delivery, acknowledgement and saved content");
    wait_audio();
    ok("audio",{{"op","set_master"},{"volume",70}},11000);
    auto first=ok("audio",{{"op","open"},{"rate",16000},{"channels",1},{"focus","mix"},{"paused",true},{"title","440 Hz target"}},11002).at("token");
    auto second=ok("audio",{{"op","open"},{"rate",16000},{"channels",1},{"focus","mix"},{"paused",true},{"title","660 Hz target"}},11004).at("token");
    check(!call("audio",{{"op","write"},{"token",first},{"hex","0000"}},11004).at("ok"),"foreign PCM");
    static const char digits[]="0123456789abcdef";
    unsigned samples[2]={0,0};
    for(int stream=0;stream<2;++stream) {
        for(int round=0;round<20;++round) {
            std::string hex;
            for(int i=0;i<1000;++i) {
                int16_t v=int16_t(3000*std::sin((samples[stream]+unsigned(i))*2*3.141592653589793*(stream?660:440)/16000));
                uint8_t bytes[2]={uint8_t(v),uint8_t(uint16_t(v)>>8)};
                for(auto byte:bytes){hex+=digits[byte>>4];hex+=digits[byte&15];}
            }
            auto count=ok("audio",{{"op","write"},{"token",stream?second:first},{"hex",hex}},stream?11004:11002).at("accepted").get<unsigned>();
            check(count==2000,"prebuffer rejected samples");samples[stream]+=count/2;
        }
    }
    ok("audio",{{"op","resume"},{"token",first}},11002);
    ok("audio",{{"op","resume"},{"token",second}},11004);
    usleep(2500000);
    for(auto token:{first,second}) {
        auto status=ok("audio",{{"op","stream"},{"token",token}}).at("stream");
        check(status.at("played_bytes").get<unsigned long long>()>20000,"PipeWire did not consume PCM");
    }
    ok("audio",{{"op","pause"},{"token",first}},1000);
    check(ok("audio",{{"op","stream"},{"token",first}},11002).at("stream").at("paused"),"media pause");
    ok("audio",{{"op","resume"},{"token",first}},1000);
    ok("audio",{{"op","close"},{"token",first}},11002);ok("audio",{{"op","close"},{"token",second}},11004);
    passed("audiod two actual PipeWire PCM streams, mixing, consumption, peer denial and trusted media controls");
    auto stale=ok("audio",{{"op","open"},{"focus","mix"}},11002).at("token");
    check(!kill(audio_backend_pid(),SIGKILL),"cannot stop guest PipeWire");
    bool invalidated=false;
    for(int i=0;i<100;++i) {
        if(!call("audio",{{"op","stream"},{"token",stale}},11002).value("ok",false)) {invalidated=true;break;}
        usleep(100000);
    }
    check(invalidated,"backend loss retained old audio capability");
    wait_audio();
    auto recovered=ok("audio",{{"op","open"},{"focus","mix"}},11002).at("token");
    ok("audio",{{"op","close"},{"token",recovered}},11002);
    passed("PipeWire crash/restart invalidates streams and audiod reconnects to the actual backend");
    auto grant=ok("documents",{{"op","choose"}},11004).at("token");
    auto pending=ok("intents",{{"op","request"},{"action","share"},{"mime","text/plain"},{"text","clear target"}},11004).at("token");
    auto playback=ok("audio",{{"op","open"},{"focus","mix"}},11004).at("token");
    ok("apps",{{"op","app_clear"},{"id","org.leon.editor"}},11000);
    check(!call("documents",{{"op","selection"},{"token",grant}},11004).value("ok",false),"clear retained document access");
    check(!call("intents",{{"op","result"},{"token",pending}},11004).value("ok",false),"clear retained interaction");
    check(!call("audio",{{"op","stream"},{"token",playback}},11004).value("ok",false),"clear retained audio stream");
    passed("application data clear synchronously revokes document, interaction and audio capabilities");
    unlink("/data/leon/shared/platform-note.txt");
}
int main(){
    report.open("/run/leon/platform-tests.txt");
    try {
        if (access("/data/leon/system/platform-validation.json", F_OK) == 0) {
            auto preferences=ok("settings",{{"op","status"}}).at("settings");
            check(preferences.at("volume")==37,"reboot preference retention");
            auto network=ok("network",{{"op","status"}}).at("configuration");
            check(network.at("dns")==J::array({"10.0.2.3"}),"reboot DNS policy retention");
            auto defaults=ok("intents",{{"op","defaults"}},11000).at("defaults");
            check(defaults.contains("share:mime:text/plain"),"reboot default app retention");
            wait_audio();
            auto stream=ok("audio",{{"op","open"},{"focus","mix"}},11002).at("token");
            check(ok("audio",{{"op","stream"},{"token",stream}},11002).at("stream").at("volume")==43,"reboot app volume retention");
            ok("audio",{{"op","close"},{"token",stream}},11002);
            ok("settings",{{"op","set"},{"key","volume"},{"value",70}},11000);
            ok("audio",{{"op","set_volume"},{"app","org.leon.gallery"},{"volume",100}},11000);
            ok("network",{{"op","configure"},{"values",{{"dns",J::array()}}}},11000);
            ok("intents",{{"op","clear_default"},{"key","share:mime:text/plain"}},11000);
            passed("actual reboot retains settings, DNS, default handlers and per-app audio policy");
            report<<"PLATFORM_REBOOT_PASSED"<<std::endl;
        } else {
            run();
            ok("settings",{{"op","set"},{"key","volume"},{"value",37}},11000);
            ok("audio",{{"op","set_volume"},{"app","org.leon.gallery"},{"volume",43}},11000);
            ok("network",{{"op","configure"},{"values",{{"dns",J::array({"10.0.2.3"})}}}},11000);
            ok("intents",{{"op","set_default"},{"action","share"},{"mime","text/plain"},{"text","reboot"},{"target","org.leon.editor"}},11000);
            std::ofstream("/data/leon/system/platform-validation.json")<<"{}";
            report<<"PLATFORM_TESTS_PASSED"<<std::endl;
        }
        return 0;
    } catch(const std::exception &e) {
        report<<"FAIL "<<e.what()<<"\nPLATFORM_TESTS_FAILED"<<std::endl;return 1;
    }
}
