// Menu look: RecompFrontend's default theme with cool, icy accents.
#include "theme.h"

#include "elements/ui_frontend_theme.h"
#include "elements/ui_theme.h"
#include "recompui/recompui.h"

namespace ui_theme = recompui::theme;

void game::theme::apply() {
    using recompui::Color;
    using ui_theme::color;

    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    recompui::register_extra_font("LatoLatin-Italic.ttf");
    recompui::register_extra_font("LatoLatin-Bold.ttf");
    recompui::register_extra_font("LatoLatin-BoldItalic.ttf");
    recompui::register_extra_font("NotoEmoji-Regular.ttf");

    ui_theme::FrontendTheme theme = ui_theme::make_default_frontend_theme();
    auto set = [&theme](color id, Color value) {
        theme.colors[static_cast<std::size_t>(id)] = value;
    };

    const Color ice{ 120, 200, 255, 255 };
    set(color::Primary, ice);
    set(color::PrimaryL, Color{ 170, 222, 255, 255 });
    set(color::PrimaryD, Color{ 70, 160, 230, 255 });
    set(color::PrimaryA5, Color{ 120, 200, 255, 13 });
    set(color::PrimaryA20, Color{ 120, 200, 255, 51 });
    set(color::PrimaryA30, Color{ 120, 200, 255, 77 });
    set(color::PrimaryA50, Color{ 120, 200, 255, 128 });
    set(color::PrimaryA80, Color{ 120, 200, 255, 204 });
    set(color::TextActive, Color{ 210, 238, 255, 255 });

    ui_theme::set_frontend_theme(theme);
}
