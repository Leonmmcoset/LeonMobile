#include "application.hpp"
#include <cmath>
#include <leon/platform.hpp>
using namespace mobile;
namespace leon::apps {
GalleryApplication::~GalleryApplication() {
    if(!audio_token.empty()) try {leon::platform::Client().audio_control(audio_token,"close");} catch(...) {}
}
void GalleryApplication::toggle_audio() {
    leon::platform::Client platform;
    if(!audio_token.empty()) {platform.audio_control(audio_token,"close");audio_token.clear();return;}
    audio_sample=0;
    audio_token=platform.open_audio(16000,1,"exclusive",tr(en,"LeonMobile 测试音","LeonMobile test tone"),"LMUI");
}
void GalleryApplication::on_tick() {
    if(audio_token.empty()) return;
    try {
        leon::platform::Client platform;
        if(audio_sample>=16000*60) {platform.audio_control(audio_token,"close");audio_token.clear();app.redraw();return;}
        std::vector<uint8_t> pcm(6400);
        for(size_t i=0;i<pcm.size()/2;++i) {
            double amplitude=std::min(1.0,double(audio_sample+i)/400.0)*2500;
            int16_t sample=int16_t(std::sin(double(audio_sample+i)*440*2*3.141592653589793/16000)*amplitude);
            pcm[2*i]=uint8_t(sample);pcm[2*i+1]=uint8_t(uint16_t(sample)>>8);
        }
        audio_sample+=platform.write_audio(audio_token,pcm)/2;
    } catch(const std::exception &e) {audio_token.clear();error=e.what();app.redraw();}
}
} // namespace leon::apps
