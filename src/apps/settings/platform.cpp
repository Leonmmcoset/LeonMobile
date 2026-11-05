#include "application.hpp"
#include <leon/system.hpp>
#include <sstream>
using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::platform_refresh() {
    if (platform_job_.valid()) return;
    auto page = page_;
    if (page != Page::network && page != Page::sound && page != Page::defaults && page != Page::documents) return;
    platform_job_ = std::async(std::launch::async, [page] {
        return system_request(page == Page::network ? "network" : page == Page::sound ? "audio" :
                              page == Page::defaults ? "intents" : "documents",
                              {{"op", page == Page::defaults ? "defaults" : page == Page::documents ? "list" : "status"}});
    });
    platform_page_ = page;
}
void SettingsApplication::platform_poll() {
    if (!platform_job_.valid() || platform_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        auto result = platform_job_.get();
        if (page_ == platform_page_) {
            platform_data_ = std::move(result);
            if (page_ == Page::network && !network_loaded_) {
                auto values = platform_data_.value("configuration", Json::object());
                static_network_ = values.value("method", "dhcp") == "static";
                network_fields_[0] = values.value("address", "");
                network_fields_[1] = std::to_string(values.value("prefix", 24));
                network_fields_[2] = values.value("gateway", "");network_fields_[3].clear();
                for (const auto &server : values.value("dns", Json::array())) {
                    if (!network_fields_[3].empty()) network_fields_[3] += " ";
                    network_fields_[3] += server.get<std::string>();
                }
                network_loaded_ = true;
            }
        }
    } catch (const std::exception &e) { if (page_ == platform_page_) error = e.what(); }
    app.redraw();
}
void SettingsApplication::network_submit() {
    Json values = {{"method", static_network_ ? "static" : "dhcp"}, {"enabled", true}, {"dns", Json::array()}};
    if (static_network_) {
        size_t consumed = 0;
        int prefix = std::stoi(network_fields_[1], &consumed);
        if (consumed != network_fields_[1].size()) throw std::runtime_error("Invalid prefix");
        values["address"] = network_fields_[0]; values["prefix"] = prefix; values["gateway"] = network_fields_[2];
    }
    std::istringstream servers(network_fields_[3]);std::string server;
    while (servers >> server) values["dns"].push_back(server);
    platform_data_ = system_request("network", {{"op", "configure"}, {"values", values}});
    network_field_ = -1;keyboard = false;
}
void SettingsApplication::paint_platform(Canvas &c) {
    auto t = c.theme;double w = c.width;
    if (page_ == Page::defaults) {
        auto defaults = platform_data_.value("defaults", Json::object());double y = 164;
        c.text(tr(en, "打开文件或链接时选择“始终”保存默认应用。", "Choose Always when opening a file or link."), {28, y, w - 56, 72}, 15, t.muted);y += 84;
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            c.rounded({20, y, w - 40, 124}, t.surface, 24);
            c.text(it.key(), {36, y + 10, w - 72, 30}, 14, t.primary);
            c.text(it.value().value("id", ""), {36, y + 44, w - 72, 30}, 16, t.text);
            auto key = it.key();
            content_hit({20, y, w - 40, 124}, [this, key](double, double) {
                system_request("intents", {{"op", "clear_default"}, {"key", key}});platform_refresh();
            });
            c.text(tr(en, "点击清除", "Tap to clear"), {36, y + 82, w - 72, 24}, 13, t.muted);y += 136;
        }
        if (defaults.empty()) c.text(tr(en, "尚未选择默认应用", "No defaults selected"), {28, y, w - 56, 60}, 18, t.muted);
    } else if (page_ == Page::documents) {
        auto docs = platform_data_.value("documents", Json::array());double y = 164;
        c.text(tr(en, "撤销后，应用无法继续读取文件。", "Revoking blocks further document reads."), {28, y, w - 56, 60}, 15, t.muted);y += 72;
        for (const auto &doc : docs) {
            c.rounded({20, y, w - 40, 136}, t.surface, 24);
            c.text(doc.value("app", ""), {36, y + 8, w - 72, 30}, 15, t.primary);
            c.text(doc.value("name", ""), {36, y + 42, w - 72, 32}, 16, t.text);
            c.text(tr(en, "点击撤销 · 剩余 ", "Tap to revoke · remaining ") +
                std::to_string(std::max<int64_t>(0, doc.value("remaining_ms", int64_t{}) / 1000)) + "s",
                {36, y + 84, w - 72, 28}, 13, t.muted);
            content_hit({20, y, w - 40, 136}, [this, doc](double, double) {
                system_request("documents", {{"op", "revoke"}, {"token", doc.at("token")}});platform_refresh();
            });y += 148;
        }
        if (docs.empty()) c.text(tr(en, "没有临时文件授权", "No temporary document access"), {28, y, w - 56, 60}, 18, t.muted);
    }
}
void SettingsApplication::paint_network_controls(Canvas &c) {
    auto t=c.theme;double w=c.width;
    bool enabled=platform_data_.value("enabled",true);
    c.text(platform_data_.value("connection_state", "unavailable"),{28,548,w-56,28},14,t.muted);
    auto button_at=[&](Rect r,const std::string &label,std::function<void()> fn) {
        c.rounded(r,t.surface,18);c.text(label,{r.x+12,r.y+8,r.w-24,r.h-16},15,t.text,false,1);
        content_hit(r,[fn](double,double){fn();});
    };
    button_at({20,588,(w-48)/2,48},enabled?tr(en,"停用以太网","Disable Ethernet"):tr(en,"启用以太网","Enable Ethernet"),[this,enabled]{
        platform_data_=system_request("network",{{"op",enabled?"disable":"enable"}});
    });
    button_at({28+(w-48)/2,588,(w-48)/2,48},tr(en,"续租 DHCP","Renew DHCP"),[this]{platform_data_=system_request("network",{{"op","renew"}});});
    button_at({20,652,w-40,52},static_network_?tr(en,"地址方式：静态 IPv4","Address: static IPv4"):tr(en,"地址方式：DHCP","Address: DHCP"),[this]{static_network_=!static_network_;});
    const char *zh[]={"IPv4 地址","网络前缀长度","网关（可留空）","DNS（空格分隔，空为自动）"};
    const char *eng[]={"IPv4 address","Prefix length","Gateway (optional)","DNS (space separated, empty for automatic)"};
    double y=724;
    for(int i=0;i<4;++i) {
        if(!static_network_ && i<3) continue;
        c.text(en?eng[i]:zh[i],{28,y,w-56,28},14,t.muted);
        button_at({20,y+34,w-40,50},network_fields_[i].empty()?"—":network_fields_[i],[this,i]{network_field_=i;keyboard=true;});y+=102;
    }
    button_at({20,y+8,w-40,52},tr(en,"保存并应用","Save and apply"),[this]{network_submit();});
    auto message=platform_data_.value("error","");
    c.text(message,{28,y+72,w-56,60},14,t.muted);
}
void SettingsApplication::paint_audio_controls(Canvas &c) {
    auto t=c.theme;double w=c.width,y=488;
    c.text(platform_data_.value("backend_ready",false)?tr(en,"音频服务已连接","Audio service connected"):tr(en,"等待音频输出","Waiting for audio output"),{28,y,w-56,28},14,t.muted);y+=42;
    for(const auto &device:platform_data_.value("devices",Json::array())) {
        c.rounded({20,y,w-40,70},device.value("default",false)?t.container:t.surface,22);
        c.text(device.value("description",device.value("name","")),{36,y+10,w-72,48},16,t.text);
        content_hit({20,y,w-40,70},[this,device](double,double){system_request("audio",{{"op","route"},{"device",device.at("id")}});platform_refresh();});y+=82;
    }
    for(const auto &stream:platform_data_.value("streams",Json::array())) {
        c.rounded({20,y,w-40,148},t.surface,24);
        c.text(stream.value("title",stream.value("app","")),{36,y+8,w-72,36},17,t.text);
        c.text(stream.value("app","")+" · "+std::to_string(stream.value("volume",100))+"%",{36,y+48,w-72,28},13,t.muted);
        for(int direction:{-1,1}) {
            double x=direction<0?36:w-100;
            c.rounded({x,y+88,64,42},t.container,16);
            c.text(direction<0?"−":"+",{x,y+90,64,36},20,t.text,false,1);
            content_hit({x,y+88,64,42},[this,stream,direction](double,double){
                system_request("audio",{{"op","set_volume"},{"app",stream.at("app")},{"volume",std::clamp(stream.value("volume",100)+direction*10,0,100)}});platform_refresh();
            });
        }
        y+=160;
    }
}
} // namespace leon::apps
