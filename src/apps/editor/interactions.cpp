#include "application.hpp"
#include <leon/platform.hpp>
using namespace mobile;
namespace leon::apps {
void EditorApplication::poll_interactions() {
    if (document_job.valid()) {
        if (document_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            auto result=document_job.get();
            document.text=result.at("text");
            // Work on a private draft. Never overwrite a source selected in another app.
            document.path=user_home()+"/received-note.txt";focus=0;error.clear();
        } catch(const std::exception &e) {error=e.what();}
        app.redraw();return;
    }
    if (++interaction_ticks % 5) return;
    try {
        leon::platform::Client platform;
        if (!document_token.empty()) {
            auto selected=platform.document(document_token);
            if(selected.at("state")=="canceled") {platform.revoke_document(document_token);document_token.clear();}
            else if(selected.at("state")=="ready") {
                auto token=document_token;document_token.clear();
                document_job=std::async(std::launch::async,[token] {
                    leon::platform::Client client;auto bytes=client.read_document(token,1024*1024);
                    client.revoke_document(token);
                    if(std::find(bytes.begin(),bytes.end(),0)!=bytes.end()) throw std::runtime_error("Binary content cannot be edited as text");
                    return Json{{"text",std::string(bytes.begin(),bytes.end())}};
                });
            }
            return;
        }
        auto events=platform.receive();
        if(events.empty()) return;
        auto event=events.front();auto payload=event.at("payload");
        auto token=event.at("token").get<std::string>();
        if(payload.value("mime","")!="text/plain") {platform.acknowledge(token);return;}
        if(payload.contains("text")) {
            document.text=payload.at("text");document.path=user_home()+"/received-note.txt";
            platform.acknowledge(token);app.redraw();
        } else if(payload.contains("document")) {
            auto doc=payload.at("document").get<std::string>();
            document_job=std::async(std::launch::async,[doc,token] {
                leon::platform::Client client;auto bytes=client.read_document(doc,1024*1024);
                if(std::find(bytes.begin(),bytes.end(),0)!=bytes.end()) throw std::runtime_error("Binary content cannot be edited as text");
                client.acknowledge(token);
                return Json{{"text",std::string(bytes.begin(),bytes.end())}};
            });
        }
    } catch (const std::exception &e) {
        if(!document_token.empty()) {document_token.clear();error=e.what();app.redraw();}
    }
}
} // namespace leon::apps
