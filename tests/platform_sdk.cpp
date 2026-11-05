#include <leon/platform.hpp>
#include <iostream>
int main(int argc,char **argv) {
    if(argc<3) return 2;
    try {
        leon::platform::Client c(argv[1]);std::string op=argv[2];leon::platform::Json result;
        if(op=="settings") result=c.settings();
        else if(op=="choose") result={{"token",c.choose_document()}};
        else if(op=="offer" && argc==4) result={{"token",c.offer_document(argv[3])}};
        else if(op=="read" && argc==4) {auto data=c.read_document(argv[3],1024*1024);result={{"text",std::string(data.begin(),data.end())}};}
        else if(op=="share" && argc==4) result={{"token",c.interact({{"action","share"},{"mime","text/plain"},{"text",argv[3]}})}};
        else if(op=="audio") {auto token=c.open_audio(16000,1,"mix","SDK test");std::vector<std::uint8_t> data(2048,42);auto n=c.write_audio(token,data);c.audio_control(token,"close");result={{"accepted",n}};}
        else return 2;
        std::cout<<result.dump()<<'\n';return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
