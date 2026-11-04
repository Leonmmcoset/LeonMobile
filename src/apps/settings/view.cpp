#include "application.hpp"
#include "widgets.hpp"

using namespace mobile;
using leon::apps::settings::Page;
namespace leon::apps {
void SettingsApplication::paint_header(Canvas &c) {
    double w = c.width;
    auto t = c.theme;
    if (page_ != Page::home) {
        c.line(37, 23, 27, 33, t.text);
        c.line(27, 33, 37, 43, t.text);
        c.line(28, 33, 48, 33, t.text);
        click(hits, {8, 8, 56, 48}, [this] { back(); });
        c.text(tr(en, "设置", "Settings"), {72, 8, w - 96, 48}, 18, t.text);
        c.text(page_ == Page::applications && !application_id_.empty()
                   ? tr(en, "应用信息", "App info") : title(page_),
               {24, 76, w - 48, 64}, 32, t.text);
        return;
    }
    c.text(tr(en, "设置", "Settings"), {24, 24, w - 48, 72}, 42, t.text);
    c.text("LeonMobile", {28, 96, w - 56, 24}, 14, t.muted);
    Rect search{20, 136, w - 40, 56};
    c.rounded(search, t.container, 28);
    c.circle(46, 161, 7, t.muted);
    c.circle(46, 161, 5.2, t.container);
    c.line(51, 166, 57, 172, t.muted, 2);
    c.text(query_.empty() ? tr(en, "搜索设置", "Search settings") : query_, {76, 144, w - 144, 40},
           16, query_.empty() ? t.muted : t.text);
    click(hits, {20, 136, w - 96, 56}, [this] { keyboard = true; });
    if (!query_.empty()) {
        c.line(w - 49, 159, w - 39, 169, t.muted);
        c.line(w - 39, 159, w - 49, 169, t.muted);
        click(hits, {w - 76, 136, 56, 56}, [this] {
            query_.clear();
            scroll = 0;
        });
    }
}
void SettingsApplication::paint_content(Canvas &c, double bottom) {
    double top = page_ == Page::home ? 216 : 164;
    double end = 700;
    if (page_ == Page::home)
        end = top + visible_entries().size() * 84;
    else if (page_ == Page::applications)
        end = applications_bottom();
    else if (page_ == Page::sound || page_ == Page::navigation)
        end = 500;
    else if (page_ == Page::system)
        end = 508;
    else if (page_ == Page::security)
        end = security_dialog_ ? 680 : 640;
    else if (page_ == Page::language)
        end = 340;
    else if (page_ == Page::developer) {
        if (root_dialog_)
            end = 408 + (root_current_required_ ? 94 : 0) + (root_new_required_ ? 188 : 0);
        else if (developer_view_ == DeveloperView::home)
            end = 812;
        else if (developer_view_ == DeveloperView::services && !developer_selection_.empty())
            end = 804;
        else if (developer_view_ == DeveloperView::kernel && developer_data_.contains("value"))
            end = 586;
        else
            end = 428 + (developer_view_ == DeveloperView::modules ? 60 : 0) +
                  developer_data_.value("items", Json::array()).size() * 64;
    } else if (page_ == Page::display)
        end = 744;
    scroll = std::clamp(scroll, 0.0, std::max(0.0, end - bottom + 16));
    viewport_ = {0, top, double(c.width), std::max(0.0, bottom - top)};
    c.save();
    c.clip(viewport_);
    cairo_translate(c.cr, 0, -scroll);
    if (page_ == Page::applications)
        paint_applications(c);
    else if (page_ == Page::security)
        paint_security(c);
    else if (page_ == Page::developer)
        paint_developer(c);
    else if (page_ == Page::home)
        paint_home(c);
    else if (page_ == Page::network || page_ == Page::storage || page_ == Page::about)
        paint_device(c);
    else
        paint_preferences(c);
    c.restore();
}
void SettingsApplication::link(Canvas &c, Rect bounds, Page page) {
    c.rounded(bounds, c.theme.surface, 22);
    c.circle(bounds.x + 32, bounds.y + 38, 21, c.theme.container);
    settings::icon(c, settings::entry(page).icon, bounds.x + 32, bounds.y + 38, c.theme.primary);
    c.text(title(page), {bounds.x + 68, bounds.y + 10, bounds.w - 112, 29}, 19, c.theme.text);
    c.text(summary(page), {bounds.x + 68, bounds.y + 41, bounds.w - 112, 24}, 13, c.theme.muted);
    settings::chevron(c, bounds.x + bounds.w - 24, bounds.y + 38);
    content_hit(bounds, [this, page](double, double) { open(page); });
}
void SettingsApplication::paint_home(Canvas &c) {
    double y = 216;
    auto pages = visible_entries();
    for (auto page : pages) {
        link(c, {16, y, double(c.width - 32), 76}, page);
        y += 84;
    }
    if (pages.empty())
        c.text(tr(en, "没有找到相关设置", "No matching settings"),
               {24, 248, double(c.width - 48), 48}, 16, c.theme.muted, false, 1);
}
} // namespace leon::apps
