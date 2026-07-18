#include "ui/Theme.h"

#include "imgui.h"

namespace trm::ui {

namespace {

ImVec4 ToImVec4(unsigned int packedRgba)
{
    return ImGui::ColorConvertU32ToFloat4(packedRgba);
}

} // namespace

void ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = kUiWindowRounding;
    style.ChildRounding = kUiCardRounding;
    style.FrameRounding = kUiFrameRounding;
    style.PopupRounding = kUiPopupRounding;
    style.ScrollbarRounding = kUiScrollbarRounding;
    style.GrabRounding = kUiGrabRounding;
    style.TabRounding = kUiTabRounding;

    style.WindowPadding = ImVec2(kUiWindowPaddingX, kUiWindowPaddingY);
    style.FramePadding = ImVec2(kUiFramePaddingX, kUiFramePaddingY);
    style.ItemSpacing = ImVec2(kUiItemSpacingX, kUiItemSpacingY);
    style.ItemInnerSpacing = ImVec2(kUiItemInnerSpacingX, kUiItemInnerSpacingY);

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ToImVec4(kColorBgWindow);
    colors[ImGuiCol_ChildBg] = ToImVec4(kColorBgPanel);
    colors[ImGuiCol_PopupBg] = ToImVec4(kColorBgPanel);
    colors[ImGuiCol_Border] = ToImVec4(kColorBorder);

    colors[ImGuiCol_Text] = ToImVec4(kColorTextPrimary);
    colors[ImGuiCol_TextDisabled] = ToImVec4(kColorWaiting);

    colors[ImGuiCol_FrameBg] = ToImVec4(kColorBgFrame);
    colors[ImGuiCol_FrameBgHovered] = ToImVec4(kColorBgFrameHovered);
    colors[ImGuiCol_FrameBgActive] = ToImVec4(kColorBgFrameActive);

    colors[ImGuiCol_TitleBg] = ToImVec4(kColorBgWindow);
    colors[ImGuiCol_TitleBgActive] = ToImVec4(kColorBgWindow);
    colors[ImGuiCol_TitleBgCollapsed] = ToImVec4(kColorBgWindow);

    colors[ImGuiCol_ScrollbarBg] = ToImVec4(kColorBgWindow);
    colors[ImGuiCol_ScrollbarGrab] = ToImVec4(kColorBgFrameHovered);
    colors[ImGuiCol_ScrollbarGrabHovered] = ToImVec4(kColorBgFrameActive);
    colors[ImGuiCol_ScrollbarGrabActive] = ToImVec4(kColorAccent);

    colors[ImGuiCol_CheckMark] = ToImVec4(kColorAccent);
    colors[ImGuiCol_SliderGrab] = ToImVec4(kColorAccent);
    colors[ImGuiCol_SliderGrabActive] = ToImVec4(kColorAccent);

    colors[ImGuiCol_Button] = ToImVec4(kColorBgFrame);
    colors[ImGuiCol_ButtonHovered] = ToImVec4(kColorBgFrameHovered);
    colors[ImGuiCol_ButtonActive] = ToImVec4(kColorBgFrameActive);

    colors[ImGuiCol_Header] = ToImVec4(kColorBgFrameHovered);
    colors[ImGuiCol_HeaderHovered] = ToImVec4(kColorBgFrameActive);
    colors[ImGuiCol_HeaderActive] = ToImVec4(kColorAccent);

    colors[ImGuiCol_Separator] = ToImVec4(kColorBorder);
    colors[ImGuiCol_SeparatorHovered] = ToImVec4(kColorAccent);
    colors[ImGuiCol_SeparatorActive] = ToImVec4(kColorAccent);

    colors[ImGuiCol_Tab] = ToImVec4(kColorBgPanel);
    colors[ImGuiCol_TabHovered] = ToImVec4(kColorBgFrameHovered);
    colors[ImGuiCol_TabActive] = ToImVec4(kColorBgFrameActive);
    colors[ImGuiCol_TabUnfocused] = ToImVec4(kColorBgPanel);
    colors[ImGuiCol_TabUnfocusedActive] = ToImVec4(kColorBgFrame);

    colors[ImGuiCol_TableHeaderBg] = ToImVec4(kColorBgFrame);
    colors[ImGuiCol_TableBorderStrong] = ToImVec4(kColorBorder);
    colors[ImGuiCol_TableBorderLight] = ToImVec4(kColorBorder);
    colors[ImGuiCol_TableRowBg] = ToImVec4(kColorBgWindow);
    colors[ImGuiCol_TableRowBgAlt] = ToImVec4(kColorBgPanel);

    ImVec4 accent = ToImVec4(kColorAccent);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
}

} // namespace trm::ui
