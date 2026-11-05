#include "application.hpp"
#include <fstream>
#include <leon/platform.hpp>
using namespace mobile;
namespace leon::apps {
void ImagesApplication::on_tick() {
    if(document_job.valid()) {
        if(document_job.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return;
        try {
            image_path=document_job.get();image.reset(decode_image(image_path));
            if(!image || cairo_surface_status(image.get())) throw std::runtime_error("Cannot decode selected PNG/JPEG");
            error.clear();
        } catch(const std::exception &e) {error=e.what();}
        app.redraw();return;
    }
    if(++interaction_ticks%5) return;
    try {
        leon::platform::Client client;std::string doc,event_token;
        if(!document_token.empty()) {
            auto selected=client.document(document_token);
            if(selected.at("state")=="canceled") {client.revoke_document(document_token);document_token.clear();return;}
            if(selected.at("state")!="ready") return;
            doc=document_token;document_token.clear();
        } else {
            auto events=client.receive();if(events.empty()) return;
            auto event=events.front();event_token=event.at("token");auto payload=event.at("payload");
            auto mime=payload.value("mime","");
            if((mime!="image/png" && mime!="image/jpeg") || !payload.contains("document")) {client.acknowledge(event_token);return;}
            doc=payload.at("document");
        }
        document_job=std::async(std::launch::async,[doc,event_token] {
            leon::platform::Client platform;auto bytes=platform.read_document(doc);
            auto path=user_home()+"/selected-image";
            std::ofstream file(path,std::ios::binary|std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.close();
            if(!file) throw std::runtime_error("Cannot save selected image");
            if(!event_token.empty()) platform.acknowledge(event_token);
            else platform.revoke_document(doc);
            return path;
        });
    } catch(const std::exception &e) {if(!document_token.empty()){document_token.clear();error=e.what();app.redraw();}}
}
} // namespace leon::apps
