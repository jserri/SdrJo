#pragma once
//
// Tema "SdrJo dark" per Dear ImGui: coerente con il Cockpit web
// (sfondi blu-notte, accento azzurro, angoli arrotondati).
//
#include <imgui.h>

namespace sdrjo::gui {

inline void applyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);

    ImVec4* c = s.Colors;
    const ImVec4 bg0(0.040f, 0.055f, 0.078f, 1.00f);
    const ImVec4 bg1(0.063f, 0.086f, 0.122f, 1.00f);
    const ImVec4 bg2(0.086f, 0.118f, 0.165f, 1.00f);
    const ImVec4 line(0.290f, 0.400f, 0.500f, 0.20f);
    const ImVec4 text(0.858f, 0.894f, 0.933f, 1.00f);
    const ImVec4 dim(0.458f, 0.522f, 0.603f, 1.00f);
    const ImVec4 acc(0.220f, 0.714f, 1.000f, 1.00f);
    const ImVec4 accDark(0.130f, 0.420f, 0.600f, 1.00f);

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = dim;
    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = bg2;
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_FrameBg] = bg0;
    c[ImGuiCol_FrameBgHovered] = bg2;
    c[ImGuiCol_FrameBgActive] = accDark;
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg0;
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_ScrollbarBg] = bg0;
    c[ImGuiCol_ScrollbarGrab] = bg2;
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = acc;
    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = accDark;
    c[ImGuiCol_ButtonActive] = acc;
    c[ImGuiCol_Header] = bg2;
    c[ImGuiCol_HeaderHovered] = accDark;
    c[ImGuiCol_HeaderActive] = accDark;
    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_ResizeGrip] = line;
    c[ImGuiCol_Tab] = bg1;
    c[ImGuiCol_TabHovered] = accDark;
    c[ImGuiCol_PlotLines] = acc;
    c[ImGuiCol_PlotHistogram] = acc;
    c[ImGuiCol_TableHeaderBg] = bg0;
    c[ImGuiCol_TableBorderLight] = line;
    c[ImGuiCol_TableBorderStrong] = line;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.02f);
}

} // namespace sdrjo::gui
