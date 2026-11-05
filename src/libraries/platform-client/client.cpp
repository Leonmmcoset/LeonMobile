#include <leon/platform.hpp>
#include <leon/ipc.hpp>
#include <stdexcept>
namespace leon::platform {
Json Client::call(const std::string &service,const Json &r) const {
    auto out=Json::parse(leon::request(directory_+"/"+service+".sock",r));
    if(!out.value("ok",false)) throw std::runtime_error(out.value("error","platform request failed"));
    return out;
}
Json Client::settings() const {return call("settings",{{"op","status"}});}
Json Client::watch_settings(std::int64_t n) const {return call("settings",{{"op","watch"},{"since",n}});}
Json Client::set_preference(const std::string &key,Json value) const {return call("settings",{{"op","set"},{"key",key},{"value",std::move(value)}});}
Json Client::network(std::int64_t since) const {return since<0?call("network",{{"op","status"}}):call("network",{{"op","watch"},{"since",since}});}
std::string Client::choose_document(unsigned seconds) const {return call("documents",{{"op","choose"},{"seconds",seconds}}).at("token");}
std::string Client::offer_document(const std::string &file,unsigned seconds) const {return call("documents",{{"op","offer"},{"file",file},{"seconds",seconds}}).at("token");}
Json Client::document(const std::string &token) const {return call("documents",{{"op","selection"},{"token",token}});}
std::vector<std::uint8_t> Client::read_document(const std::string &token,std::size_t limit) const {
    auto meta=document(token);auto size=meta.at("bytes").get<std::size_t>();
    if(meta.at("state")!="ready" || size>limit || limit>16*1024*1024) throw std::runtime_error("document unavailable or exceeds caller limit");
    std::vector<std::uint8_t> out;out.reserve(size);
    while(out.size()<size) {
        auto reply=call("documents",{{"op","read"},{"token",token},{"offset",out.size()},{"length",2048}});
        auto text=reply.at("hex").get<std::string>();
        if(text.empty() || text.size()%2 || out.size()+text.size()/2>size) throw std::runtime_error("invalid document chunk");
        auto digit=[](char c)->unsigned {if(c>='0'&&c<='9') return unsigned(c-'0');if(c>='a'&&c<='f') return unsigned(c-'a'+10);throw std::runtime_error("invalid document encoding");};
        for(std::size_t i=0;i<text.size();i+=2) out.push_back(std::uint8_t(digit(text[i])*16+digit(text[i+1])));
    }
    return out;
}
void Client::revoke_document(const std::string &token) const {call("documents",{{"op","revoke"},{"token",token}});}
Json Client::resolve(Json p) const {p["op"]="resolve";return call("intents",p);}
std::string Client::interact(Json p) const {p["op"]="request";return call("intents",p).at("token");}
Json Client::interaction(const std::string &token) const {return call("intents",{{"op","result"},{"token",token}});}
Json Client::receive() const {return call("intents",{{"op","receive"}}).at("events");}
void Client::acknowledge(const std::string &token) const {call("intents",{{"op","ack"},{"token",token}});}
std::string Client::open_audio(unsigned rate,unsigned channels,const std::string &focus,const std::string &title,const std::string &artist) const {
    return call("audio",{{"op","open"},{"rate",rate},{"channels",channels},{"focus",focus},{"title",title},{"artist",artist}}).at("token");
}
std::size_t Client::write_audio(const std::string &token,std::span<const std::uint8_t> pcm) const {
    std::size_t used=0;
    while(used<pcm.size()) {
        auto count=std::min<std::size_t>(2048,pcm.size()-used);std::string hex;hex.reserve(count*2);
        for(std::size_t i=0;i<count;++i){auto byte=pcm[used+i];hex+="0123456789abcdef"[byte>>4];hex+="0123456789abcdef"[byte&15];}
        auto accepted=call("audio",{{"op","write"},{"token",token},{"hex",hex}}).at("accepted").get<std::size_t>();
        if(accepted>count) throw std::runtime_error("invalid PCM acknowledgement");
        used+=accepted;if(accepted<count) break;
    }
    return used;
}
void Client::audio_control(const std::string &token,const std::string &op) const {
    if(op!="keepalive" && op!="pause" && op!="resume" && op!="close") throw std::runtime_error("invalid audio control");
    call("audio",{{"op",op},{"token",token}});
}
Json Client::audio_status() const {return call("audio",{{"op","status"}});}
void Client::audio_volume(unsigned percent) const {call("audio",{{"op","set_volume"},{"volume",percent}});}
} // namespace leon::platform
