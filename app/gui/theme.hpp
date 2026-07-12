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

inline void applyTheme()
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
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 2.0f;
    s.SeparatorTextPadding = ImVec2(18, 4);

    const Palette& p = palette();
    ImVec4* c = s.Colors;
    auto tint = [](ImVec4 col, float a) { col.w = a; return col; };

    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.dim;
    c[ImGuiCol_WindowBg] = p.bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = p.bg2;
    c[ImGuiCol_Border] = p.line;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.bg0;
    c[ImGuiCol_FrameBgHovered] = p.bg3;
    c[ImGuiCol_FrameBgActive] = tint(p.acc, 0.22f);
    c[ImGuiCol_TitleBg] = p.bg0;
    c[ImGuiCol_TitleBgActive] = p.bg0;
    c[ImGuiCol_MenuBarBg] = p.bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = p.bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = tint(p.acc, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = p.acc;
    c[ImGuiCol_CheckMark] = p.acc;
    c[ImGuiCol_SliderGrab] = p.acc;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.82f, 1.0f, 1.0f);
    c[ImGuiCol_Button] = p.bg2;
    c[ImGuiCol_ButtonHovered] = tint(p.acc, 0.32f);
    c[ImGuiCol_ButtonActive] = tint(p.acc, 0.55f);
    // Header (le sezioni a tendina): tinta d'accento cosi' "spiccano".
    c[ImGuiCol_Header] = tint(p.acc, 0.14f);
    c[ImGuiCol_HeaderHovered] = tint(p.acc, 0.28f);
    c[ImGuiCol_HeaderActive] = tint(p.acc, 0.38f);
    c[ImGuiCol_Separator] = p.line;
    c[ImGuiCol_SeparatorHovered] = tint(p.acc, 0.5f);
    c[ImGuiCol_SeparatorActive] = p.acc;
    c[ImGuiCol_ResizeGrip] = p.line;
    c[ImGuiCol_ResizeGripHovered] = tint(p.acc, 0.5f);
    c[ImGuiCol_Tab] = p.bg1;
    c[ImGuiCol_TabHovered] = tint(p.acc, 0.35f);
    c[ImGuiCol_TabActive] = tint(p.acc, 0.22f);
    c[ImGuiCol_PlotLines] = p.acc;
    c[ImGuiCol_PlotHistogram] = p.acc;
    c[ImGuiCol_TextSelectedBg] = tint(p.acc, 0.35f);
    c[ImGuiCol_NavHighlight] = p.acc;
    c[ImGuiCol_TableHeaderBg] = p.bg0;
    c[ImGuiCol_TableBorderLight] = p.line;
    c[ImGuiCol_TableBorderStrong] = p.line;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
}

} // namespace sdrjo::gui
