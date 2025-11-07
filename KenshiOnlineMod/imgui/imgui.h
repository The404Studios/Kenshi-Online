#pragma once

// Minimal ImGui header for Kenshi Online
struct ImVec2 {
    float x, y;
    ImVec2() : x(0), y(0) {}
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

struct ImVec4 {
    float x, y, z, w;
    ImVec4() : x(0), y(0), z(0), w(0) {}
    ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

enum ImGuiCond_ {
    ImGuiCond_None = 0,
    ImGuiCond_Always = 1 << 0,
    ImGuiCond_Once = 1 << 1,
    ImGuiCond_FirstUseEver = 1 << 2,
    ImGuiCond_Appearing = 1 << 3
};

enum ImGuiWindowFlags_ {
    ImGuiWindowFlags_None = 0,
    ImGuiWindowFlags_NoTitleBar = 1 << 0,
    ImGuiWindowFlags_NoResize = 1 << 1,
    ImGuiWindowFlags_NoMove = 1 << 2,
    ImGuiWindowFlags_NoScrollbar = 1 << 3,
    ImGuiWindowFlags_NoScrollWithMouse = 1 << 4,
    ImGuiWindowFlags_NoCollapse = 1 << 5,
    ImGuiWindowFlags_AlwaysAutoResize = 1 << 6,
    ImGuiWindowFlags_NoBackground = 1 << 7,
    ImGuiWindowFlags_NoSavedSettings = 1 << 8,
    ImGuiWindowFlags_NoMouseInputs = 1 << 9,
    ImGuiWindowFlags_MenuBar = 1 << 10,
    ImGuiWindowFlags_HorizontalScrollbar = 1 << 11,
    ImGuiWindowFlags_NoFocusOnAppearing = 1 << 12,
    ImGuiWindowFlags_NoBringToFrontOnFocus = 1 << 13,
    ImGuiWindowFlags_AlwaysVerticalScrollbar = 1 << 14,
    ImGuiWindowFlags_AlwaysHorizontalScrollbar = 1 << 15,
    ImGuiWindowFlags_AlwaysUseWindowPadding = 1 << 16,
};

enum ImGuiInputTextFlags_ {
    ImGuiInputTextFlags_None = 0,
    ImGuiInputTextFlags_CharsDecimal = 1 << 0,
    ImGuiInputTextFlags_CharsHexadecimal = 1 << 1,
    ImGuiInputTextFlags_CharsUppercase = 1 << 2,
    ImGuiInputTextFlags_CharsNoBlank = 1 << 3,
    ImGuiInputTextFlags_AutoSelectAll = 1 << 4,
    ImGuiInputTextFlags_EnterReturnsTrue = 1 << 5,
    ImGuiInputTextFlags_ReadOnly = 1 << 10,
    ImGuiInputTextFlags_Password = 1 << 11,
};

typedef void* ImTextureID;

namespace ImGui {
    // Window functions
    bool Begin(const char* name, bool* p_open = nullptr, int flags = 0);
    void End();

    // Child windows
    bool BeginChild(const char* str_id);
    bool BeginChild(const char* str_id, const ImVec2& size, bool border = false, int flags = 0);
    void EndChild();

    // Window manipulation
    void SetNextWindowPos(const ImVec2& pos, int cond = 0);
    void SetNextWindowSize(const ImVec2& size, int cond = 0);

    // Widgets: Text
    void Text(const char* fmt, ...);
    void TextColored(const ImVec4& col, const char* fmt, ...);

    // Widgets: Main
    bool Button(const char* label);
    bool Checkbox(const char* label, bool* v);
    bool RadioButton(const char* label, bool active);

    // Widgets: Input
    bool InputText(const char* label, char* buf, size_t buf_size, int flags = 0);
    bool InputInt(const char* label, int* v, int step = 1, int step_fast = 100, int flags = 0);

    // Widgets: Selectables
    bool Selectable(const char* label, bool selected = false, int flags = 0);

    // Layout
    void Separator();
    void SameLine();
    void SameLine(float offset_from_start_x, float spacing = -1.0f);
    void Spacing();
    void Indent(float indent_w = 0.0f);
    void Unindent(float indent_w = 0.0f);

    // Columns
    void Columns(int count = 1, const char* id = nullptr, bool border = true);
    void NextColumn();

    // Images
    void Image(ImTextureID user_texture_id, const ImVec2& size);

    // Combo Box
    bool BeginCombo(const char* label, const char* preview_value, int flags = 0);
    void EndCombo();

    // Tree
    bool TreeNode(const char* label);
    void TreePop();

    // ID stack
    void PushID(int int_id);
    void PopID();
}
