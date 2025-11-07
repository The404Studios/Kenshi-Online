#define _CRT_SECURE_NO_WARNINGS
#include "imgui.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// Minimal ImGui implementation for Kenshi Online
// This is a stub implementation to satisfy linker requirements
// In production, replace with full ImGui library from https://github.com/ocornut/imgui

namespace ImGui {
    static bool g_DummyBool = false;
    static int g_DummyInt = 0;
    static ImGuiContext* g_Context = nullptr;
    static ImGuiIO g_IO = {};
    static ImDrawData g_DrawData = {};

    ImGuiContext* CreateContext() {
        if (!g_Context) {
            g_Context = (ImGuiContext*)1; // Dummy pointer
        }
        return g_Context;
    }

    void DestroyContext(ImGuiContext* ctx) {
        g_Context = nullptr;
    }

    ImGuiIO& GetIO() {
        return g_IO;
    }

    void NewFrame() {
        // Stub: Prepare for new frame
    }

    void EndFrame() {
        // Stub: End frame
    }

    void Render() {
        // Stub: Finalize draw data
    }

    ImDrawData* GetDrawData() {
        return &g_DrawData;
    }

    bool Begin(const char* name, bool* p_open, int flags) {
        // Stub: Always return true to render contents
        return true;
    }

    void End() {
        // Stub: No-op
    }

    bool BeginChild(const char* str_id) {
        // Stub: Always return true
        return true;
    }

    bool BeginChild(const char* str_id, const ImVec2& size, bool border, int flags) {
        // Stub: Always return true
        return true;
    }

    void EndChild() {
        // Stub: No-op
    }

    void SetNextWindowPos(const ImVec2& pos, int cond) {
        // Stub: No-op
    }

    void SetNextWindowSize(const ImVec2& size, int cond) {
        // Stub: No-op
    }

    void Text(const char* fmt, ...) {
        // Stub: Could output to console for debugging
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }

    void TextColored(const ImVec4& col, const char* fmt, ...) {
        // Stub: Ignore color, just print text
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }

    bool Button(const char* label) {
        // Stub: Never pressed
        return false;
    }

    bool Checkbox(const char* label, bool* v) {
        // Stub: Never changes
        return false;
    }

    bool RadioButton(const char* label, bool active) {
        // Stub: Never pressed
        return false;
    }

    bool InputText(const char* label, char* buf, size_t buf_size) {
        // Stub: Never modified
        return false;
    }

    bool InputText(const char* label, char* buf, size_t buf_size, int flags) {
        // Stub: Never modified
        return false;
    }

    bool InputInt(const char* label, int* v, int step, int step_fast, int flags) {
        // Stub: Never modified
        return false;
    }

    bool Selectable(const char* label, bool selected) {
        // Stub: Never selected
        return false;
    }

    bool Selectable(const char* label, bool selected, int flags) {
        // Stub: Never selected
        return false;
    }

    void Separator() {
        // Stub: No-op
    }

    void SameLine() {
        // Stub: No-op
    }

    void SameLine(float offset_from_start_x, float spacing) {
        // Stub: No-op
    }

    void Spacing() {
        // Stub: No-op
    }

    void Indent(float indent_w) {
        // Stub: No-op
    }

    void Unindent(float indent_w) {
        // Stub: No-op
    }

    void Columns(int count, const char* id, bool border) {
        // Stub: No-op
    }

    void NextColumn() {
        // Stub: No-op
    }

    void Image(ImTextureID user_texture_id, const ImVec2& size) {
        // Stub: No-op
    }

    bool BeginCombo(const char* label, const char* preview_value, int flags) {
        // Stub: Never opens
        return false;
    }

    void EndCombo() {
        // Stub: No-op
    }

    bool TreeNode(const char* label) {
        // Stub: Never expands
        return false;
    }

    void TreePop() {
        // Stub: No-op
    }

    void PushID(int int_id) {
        // Stub: No-op
    }

    void PopID() {
        // Stub: No-op
    }
}
