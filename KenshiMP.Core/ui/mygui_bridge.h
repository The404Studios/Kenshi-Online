#pragma once
#include <string>
#include <vector>
#include <map>
#include <Windows.h>

namespace kmp {

// Thin runtime bridge to MyGUI via GetProcAddress on MyGUIEngine_x64.dll.
// No headers or import libs needed — all calls go through resolved function pointers.
class MyGuiBridge {
public:
    static MyGuiBridge& Get();

    // Resolve all function pointers. Returns false if DLL not found.
    bool Init();
    bool IsReady() const { return m_ready; }

    // Load a .layout file, returns widgets. prefix/parent can be empty/null.
    // Multiple layouts can be loaded simultaneously (tracked by name).
    bool LoadLayout(const std::string& layoutFile, const std::string& prefix = "");
    void UnloadLayout(const std::string& layoutFile);
    void UnloadAllLayouts();

    // Find a widget by name (searches entire GUI tree)
    void* FindWidget(const std::string& name);

    // Find a child widget within a parent widget
    void* FindChildWidget(void* parent, const std::string& name);

    // Set widget visibility
    void SetVisible(void* widget, bool visible);

    // Set caption text on a TextBox or EditBox
    void SetCaption(void* widget, const std::string& text);

    // Get caption text from an EditBox (reads from MyGUI's internal string)
    std::string GetCaption(void* widget);

    // Set any widget property by key/value (e.g., "TextColour" = "0.9 0.55 0.1")
    void SetProperty(void* widget, const std::string& key, const std::string& value);

    // Set widget alpha (0.0 - 1.0)
    void SetAlpha(void* widget, float alpha);

    // Create a child widget programmatically (fallback when layout loading fails)
    void* CreateChildWidget(void* parent, const std::string& type, const std::string& skin,
                            float x, float y, float w, float h, int align, const std::string& name);

    // Create a root widget on a layer
    void* CreateRootWidget(const std::string& type, const std::string& skin,
                           float x, float y, float w, float h, int align,
                           const std::string& layer, const std::string& name);

    // Get the LayoutManager singleton
    void* GetLayoutManager();

    // Get the Gui singleton
    void* GetGui();

private:
    MyGuiBridge() = default;

    // ── Function pointer types ──

    // Static singleton getters (no this pointer)
    using FnGetInstance = void* (__cdecl*)();

    // Widget::setVisible(bool)
    using FnSetVisible = void(__fastcall*)(void* widget, bool visible);

    // TextBox::setCaptionWithReplacing(const std::string&)
    using FnSetCaption = void(__fastcall*)(void* widget, const std::string& text);

    // TextBox::getCaption() — returns const UString& where UString = basic_string<unsigned short>
    // We return a raw pointer and decode the UString structure manually to avoid SSO mismatch.
    using FnGetCaption = void* (__fastcall*)(void* widget);

    // Gui::findWidgetT(const std::string& name, bool throwExc)
    using FnFindWidgetT = void* (__fastcall*)(void* gui, const std::string& name, bool throwExc);

    // Widget::findWidget(const std::string& name)
    using FnFindWidget = void* (__fastcall*)(void* widget, const std::string& name);

    // LayoutManager::loadLayout(retval, file, prefix, parent)
    // Returns std::vector<Widget*> via hidden return param (MSVC x64 convention for large return values)
    using FnLoadLayout = std::vector<void*>* (__fastcall*)(
        void* layoutMgr, std::vector<void*>* retval,
        const std::string& file, const std::string& prefix, void* parent);

    // LayoutManager::unloadLayout(std::vector<Widget*>&)
    using FnUnloadLayout = void(__fastcall*)(void* layoutMgr, std::vector<void*>& widgets);

    // Widget::setProperty(const std::string& key, const std::string& value)
    using FnSetProperty = void(__fastcall*)(void* widget, const std::string& key, const std::string& value);

    // Widget::setAlpha(float)
    using FnSetAlpha = void(__fastcall*)(void* widget, float alpha);

    // Gui::createWidgetRealT(type, skin, x, y, w, h, align, layer, name) -> Widget*
    using FnGuiCreateWidgetReal = void* (__fastcall*)(void* gui, const std::string& type,
        const std::string& skin, float x, float y, float w, float h, int align,
        const std::string& layer, const std::string& name);

    // Widget::createWidgetRealT(type, skin, x, y, w, h, align, name) -> Widget*
    using FnWidgetCreateWidgetReal = void* (__fastcall*)(void* parent, const std::string& type,
        const std::string& skin, float x, float y, float w, float h, int align,
        const std::string& name);

    // Manual MSVC basic_string<unsigned short> layout for reading UString
    // MSVC x64 std::basic_string<T> layout: 16-byte union (SSO buf or heap ptr), size_t size, size_t capacity
    struct UStringLayout {
        union {
            unsigned short buf[8];   // SSO buffer: 16 bytes / sizeof(unsigned short) = 8 elements
            unsigned short* ptr;     // heap pointer when capacity >= 8
        };
        size_t size;
        size_t capacity;
    };

    // Decode UString to std::string (ASCII extraction)
    static std::string DecodeUString(const UStringLayout* ustr);

    // ── Resolved pointers ──
    HMODULE           m_hMyGUI = nullptr;
    FnGetInstance     m_fnLayoutMgrInstance = nullptr;
    FnGetInstance     m_fnGuiInstance = nullptr;
    FnSetVisible      m_fnSetVisible = nullptr;
    FnSetCaption      m_fnSetCaption = nullptr;
    FnGetCaption      m_fnGetCaption = nullptr;
    FnFindWidgetT     m_fnFindWidgetT = nullptr;
    FnFindWidget      m_fnFindWidget = nullptr;
    FnLoadLayout      m_fnLoadLayout = nullptr;
    FnUnloadLayout    m_fnUnloadLayout = nullptr;
    FnSetProperty     m_fnSetProperty = nullptr;
    FnSetAlpha        m_fnSetAlpha = nullptr;
    FnGuiCreateWidgetReal       m_fnGuiCreateWidgetReal = nullptr;
    FnWidgetCreateWidgetReal    m_fnWidgetCreateWidgetReal = nullptr;

    std::map<std::string, std::vector<void*>> m_loadedLayouts;
    bool m_ready = false;
};

} // namespace kmp
