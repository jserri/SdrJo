#pragma once
//
// Tema "SdrJo dark" per Dear ImGui: coerente con il Cockpit web
// (sfondi blu-notte, accento azzurro, angoli arrotondati).
//
#include <imgui.h>

namespace sdrjo::gui {

// Palette condivisa (accessibile anche al resto della GUI per disegni
// custom: spettro, S-meter, ecc.), cosi' tutto usa gli stessi colori.
struct Palette {
    ImVec4 bg0{0.035f, 0.048f, 0.070f, 1.00f}; // sfondo piu' scuro (frame)
    ImVec4 bg1{0.060f, 0.082f, 0.118f, 1.00f}; // finestre
    ImVec4 bg2{0.090f, 0.122f, 0.172f, 1.00f}; // pannelli/rilievi
    ImVec4 bg3{0.120f, 0.160f, 0.220f, 1.00f}; // hover/attivo tenue
    ImVec4 line{0.320f, 0.430f, 0.540f, 0.24f};
    ImVec4 text{0.878f, 0.910f, 0.945f, 1.00f};
    ImVec4 dim{0.470f, 0.535f, 0.615f, 1.00f};
    ImVec4 acc{0.235f, 0.720f, 1.000f, 1.00f};  // azzurro (primario)
    ImVec4 acc2{0.560f, 0.470f, 1.000f, 1.00f}; // viola (secondario)
    ImVec4 ok{0.239f, 0.860f, 0.590f, 1.00f};
    ImVec4 warn{1.000f, 0.706f, 0.329f, 1.00f};
    ImVec4 bad{0.960f, 0.440f, 0.400f, 1.00f};
};
inline const Palette& palette()
{
    static Palette p;
    return p;
}

// Tema chiaro (chrome chiaro; lo spettro/waterfall restano scuri, come su
// tutti gli SDR: sono display dati). Accenti identici allo scuro.
struct LightBg {
    ImVec4 bg0{0.92f, 0.94f, 0.97f, 1.00f};
    ImVec4 bg1{0.97f, 0.98f, 0.99f, 1.00f};
    ImVec4 bg2{0.88f, 0.91f, 0.95f, 1.00f};
    ImVec4 bg3{0.80f, 0.86f, 0.94f, 1.00f};
    ImVec4 line{0.40f, 0.50f, 0.62f, 0.35f};
    ImVec4 text{0.10f, 0.14f, 0.20f, 1.00f};
    ImVec4 dim{0.38f, 0.45f, 0.54f, 1.00f};
};

// density: 0 = comoda, 1 = compatta. light: tema chiaro/scuro.
inline void applyTheme(bool light = false, int density = 0)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 7.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 7.0f;
    s.TabRounding = 7.0f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    // Densita': la compatta stringe padding/spaziature per far stare piu'
    // controlli a schermo (utile su laptop); la comoda e' piu' arieggiata.
    if (density == 1) {
        s.WindowPadding = ImVec2(9, 7);
        s.FramePadding = ImVec2(7, 3);
        s.ItemSpacing = ImVec2(7, 4);
        s.ItemInnerSpacing = ImVec2(6, 4);
    } else {
        s.WindowPadding = ImVec2(14, 12);
        s.FramePadding = ImVec2(10, 6);
        s.ItemSpacing = ImVec2(10, 8);
        s.ItemInnerSpacing = ImVec2(8, 6);
    }
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 2.0f;
    s.SeparatorTextPadding = ImVec2(18, 4);

    const Palette& p = palette();
    // Sfondi/testo dipendono dal tema; gli accenti no.
    ImVec4 bg0 = p.bg0, bg1 = p.bg1, bg2 = p.bg2, bg3 = p.bg3;
    ImVec4 line = p.line, text = p.text, dim = p.dim;
    if (light) {
        LightBg L;
        bg0 = L.bg0; bg1 = L.bg1; bg2 = L.bg2; bg3 = L.bg3;
        line = L.line; text = L.text; dim = L.dim;
    }

    ImVec4* c = s.Colors;
    auto tint = [](ImVec4 col, float a) { col.w = a; return col; };

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = bg2;
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = bg0;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = tint(p.acc, 0.22f);
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = tint(p.acc, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = p.acc;
    c[ImGuiCol_CheckMark] = p.acc;
    c[ImGuiCol_SliderGrab] = p.acc;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.82f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = tint(p.acc, 0.32f);
    c[ImGuiCol_ButtonActive] = tint(p.acc, 0.55f);
    // Header (le sezioni a tendina): tinta d'accento cosi' "spiccano".
    c[ImGuiCol_Header] = tint(p.acc, light ? 0.20f : 0.14f);
    c[ImGuiCol_HeaderHovered] = tint(p.acc, 0.30f);
    c[ImGuiCol_HeaderActive] = tint(p.acc, 0.40f);
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = tint(p.acc, 0.5f);
    c[ImGuiCol_SeparatorActive] = p.acc;
    c[ImGuiCol_ResizeGrip] = line;
    c[ImGuiCol_ResizeGripHovered] = tint(p.acc, 0.5f);
    c[ImGuiCol_Tab] = bg1;
    c[ImGuiCol_TabHovered] = tint(p.acc, 0.35f);
    c[ImGuiCol_TabActive] = tint(p.acc, 0.22f);
    c[ImGuiCol_PlotLines] = p.acc;
    c[ImGuiCol_PlotHistogram] = p.acc;
    c[ImGuiCol_TextSelectedBg] = tint(p.acc, 0.35f);
    c[ImGuiCol_NavHighlight] = p.acc;
    c[ImGuiCol_TableHeaderBg] = bg0;
    c[ImGuiCol_TableBorderLight] = line;
    c[ImGuiCol_TableBorderStrong] = line;
    c[ImGuiCol_TableRowBgAlt] = light ? ImVec4(0, 0, 0, 0.03f)
                                      : ImVec4(1, 1, 1, 0.02f);
}

} // namespace sdrjo::gui
