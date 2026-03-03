#include "mygui_bridge.h"
#include <spdlog/spdlog.h>

namespace kmp {

// ── SEH wrappers (no C++ objects with destructors allowed) ──
// MSVC forbids __try in functions that require object unwinding.

static void* SEH_Call0(void* fn) {
    __try {
        return reinterpret_cast<void* (__cdecl*)()>(fn)();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* SEH_FindWidgetT(void* fn, void* gui, const void* name, bool throwExc) {
    __try {
        using Fn = void* (__fastcall*)(void*, const std::string&, bool);
        return reinterpret_cast<Fn>(fn)(gui, *reinterpret_cast<const std::string*>(name), throwExc);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* SEH_FindWidget(void* fn, void* widget, const void* name) {
    __try {
        using Fn = void* (__fastcall*)(void*, const std::string&);
        return reinterpret_cast<Fn>(fn)(widget, *reinterpret_cast<const std::string*>(name));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SEH_SetVisible(void* fn, void* widget, bool visible) {
    __try {
        reinterpret_cast<void(__fastcall*)(void*, bool)>(fn)(widget, visible);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_SetCaption(void* fn, void* widget, const void* text) {
    __try {
        using Fn = void(__fastcall*)(void*, const std::string&);
        reinterpret_cast<Fn>(fn)(widget, *reinterpret_cast<const std::string*>(text));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void* SEH_GetCaption(void* fn, void* widget) {
    __try {
        return reinterpret_cast<void* (__fastcall*)(void*)>(fn)(widget);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SEH_UnloadLayout(void* fn, void* layoutMgr, void* widgets) {
    __try {
        using Fn = void(__fastcall*)(void*, std::vector<void*>&);
        reinterpret_cast<Fn>(fn)(layoutMgr, *reinterpret_cast<std::vector<void*>*>(widgets));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_SetProperty(void* fn, void* widget, const void* key, const void* value) {
    __try {
        using Fn = void(__fastcall*)(void*, const std::string&, const std::string&);
        reinterpret_cast<Fn>(fn)(widget,
            *reinterpret_cast<const std::string*>(key),
            *reinterpret_cast<const std::string*>(value));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SEH_SetAlpha(void* fn, void* widget, float alpha) {
    __try {
        reinterpret_cast<void(__fastcall*)(void*, float)>(fn)(widget, alpha);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void* SEH_CreateWidgetReal(void* fn, void* parent, const void* type, const void* skin,
                                   float x, float y, float w, float h, int align, const void* name) {
    __try {
        using Fn = void* (__fastcall*)(void*, const std::string&, const std::string&,
                                       float, float, float, float, int, const std::string&);
        return reinterpret_cast<Fn>(fn)(parent,
            *reinterpret_cast<const std::string*>(type),
            *reinterpret_cast<const std::string*>(skin),
            x, y, w, h, align,
            *reinterpret_cast<const std::string*>(name));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* SEH_CreateRootWidgetReal(void* fn, void* gui, const void* type, const void* skin,
                                       float x, float y, float w, float h, int align,
                                       const void* layer, const void* name) {
    __try {
        using Fn = void* (__fastcall*)(void*, const std::string&, const std::string&,
                                       float, float, float, float, int,
                                       const std::string&, const std::string&);
        return reinterpret_cast<Fn>(fn)(gui,
            *reinterpret_cast<const std::string*>(type),
            *reinterpret_cast<const std::string*>(skin),
            x, y, w, h, align,
            *reinterpret_cast<const std::string*>(layer),
            *reinterpret_cast<const std::string*>(name));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// loadLayout needs special handling — hidden return param for vector
static bool SEH_LoadLayout(void* fn, void* layoutMgr, void* retval,
                            const void* file, const void* prefix, void* parent) {
    __try {
        using Fn = std::vector<void*>* (__fastcall*)(
            void*, std::vector<void*>*, const std::string&, const std::string&, void*);
        reinterpret_cast<Fn>(fn)(
            layoutMgr,
            reinterpret_cast<std::vector<void*>*>(retval),
            *reinterpret_cast<const std::string*>(file),
            *reinterpret_cast<const std::string*>(prefix),
            parent);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ── MyGuiBridge implementation ──

MyGuiBridge& MyGuiBridge::Get() {
    static MyGuiBridge instance;
    return instance;
}

bool MyGuiBridge::Init() {
    if (m_ready) return true;

    m_hMyGUI = GetModuleHandleA("MyGUIEngine_x64.dll");
    if (!m_hMyGUI) {
        // Don't spam — this is normal during early init
        return false;
    }

    spdlog::info("MyGuiBridge: Found MyGUIEngine_x64.dll at 0x{:X}", (uintptr_t)m_hMyGUI);

    struct SymbolEntry {
        const char* mangledName;
        void** target;
        const char* friendlyName;
    };

    SymbolEntry symbols[] = {
        {
            "?getInstance@?$Singleton@VLayoutManager@MyGUI@@@MyGUI@@SAAEAVLayoutManager@2@XZ",
            reinterpret_cast<void**>(&m_fnLayoutMgrInstance),
            "LayoutManager::getInstance"
        },
        {
            "?getInstance@?$Singleton@VGui@MyGUI@@@MyGUI@@SAAEAVGui@2@XZ",
            reinterpret_cast<void**>(&m_fnGuiInstance),
            "Gui::getInstance"
        },
        {
            "?setVisible@Widget@MyGUI@@UEAAX_N@Z",
            reinterpret_cast<void**>(&m_fnSetVisible),
            "Widget::setVisible"
        },
        {
            "?setCaptionWithReplacing@TextBox@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            reinterpret_cast<void**>(&m_fnSetCaption),
            "TextBox::setCaptionWithReplacing"
        },
        {
            "?getCaption@TextBox@MyGUI@@QEBA?BV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@XZ",
            reinterpret_cast<void**>(&m_fnGetCaption),
            "TextBox::getCaption"
        },
        {
            "?findWidgetT@Gui@MyGUI@@QEAAPEAVWidget@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N@Z",
            reinterpret_cast<void**>(&m_fnFindWidgetT),
            "Gui::findWidgetT"
        },
        {
            "?findWidget@Widget@MyGUI@@QEAAPEAV12@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            reinterpret_cast<void**>(&m_fnFindWidget),
            "Widget::findWidget"
        },
        {
            "?loadLayout@LayoutManager@MyGUI@@QEAA?AV?$vector@PEAVWidget@MyGUI@@V?$allocator@PEAVWidget@MyGUI@@@std@@@std@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@4@0PEAVWidget@2@@Z",
            reinterpret_cast<void**>(&m_fnLoadLayout),
            "LayoutManager::loadLayout"
        },
        {
            "?unloadLayout@LayoutManager@MyGUI@@QEAAXAEAV?$vector@PEAVWidget@MyGUI@@V?$allocator@PEAVWidget@MyGUI@@@std@@@std@@@Z",
            reinterpret_cast<void**>(&m_fnUnloadLayout),
            "LayoutManager::unloadLayout"
        },
        {
            "?setProperty@Widget@MyGUI@@QEAAXAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z",
            reinterpret_cast<void**>(&m_fnSetProperty),
            "Widget::setProperty"
        },
        {
            "?setAlpha@Widget@MyGUI@@QEAAXM@Z",
            reinterpret_cast<void**>(&m_fnSetAlpha),
            "Widget::setAlpha"
        },
        {
            "?createWidgetRealT@Gui@MyGUI@@QEAAPEAVWidget@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0MMMMUAlign@2@00@Z",
            reinterpret_cast<void**>(&m_fnGuiCreateWidgetReal),
            "Gui::createWidgetRealT"
        },
        {
            "?createWidgetRealT@Widget@MyGUI@@QEAAPEAV12@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0MMMMUAlign@2@0@Z",
            reinterpret_cast<void**>(&m_fnWidgetCreateWidgetReal),
            "Widget::createWidgetRealT"
        },
    };

    int resolved = 0;
    int total = sizeof(symbols) / sizeof(symbols[0]);

    for (auto& sym : symbols) {
        FARPROC proc = GetProcAddress(m_hMyGUI, sym.mangledName);
        if (proc) {
            *sym.target = reinterpret_cast<void*>(proc);
            spdlog::info("MyGuiBridge: Resolved {} at 0x{:X}", sym.friendlyName, (uintptr_t)proc);
            resolved++;
        } else {
            spdlog::warn("MyGuiBridge: FAILED to resolve {}", sym.friendlyName);
        }
    }

    spdlog::info("MyGuiBridge: Resolved {}/{} symbols", resolved, total);

    // getCaption is optional, rest are required
    bool coreOk = m_fnLayoutMgrInstance && m_fnGuiInstance && m_fnSetVisible &&
                  m_fnSetCaption && m_fnFindWidgetT && m_fnFindWidget &&
                  m_fnLoadLayout && m_fnUnloadLayout;

    if (!coreOk) {
        spdlog::error("MyGuiBridge: Missing critical symbols, native UI disabled");
        return false;
    }

    // Verify singletons are alive (MyGUI may be loaded but not yet initialized)
    void* lm = SEH_Call0(reinterpret_cast<void*>(m_fnLayoutMgrInstance));
    void* gui = SEH_Call0(reinterpret_cast<void*>(m_fnGuiInstance));
    if (!lm || !gui) {
        spdlog::warn("MyGuiBridge: Singletons not ready yet (LayoutMgr={}, Gui={})",
                      lm ? "OK" : "null", gui ? "OK" : "null");
        return false;
    }

    m_ready = true;
    spdlog::info("MyGuiBridge: Ready (LayoutMgr=0x{:X}, Gui=0x{:X})", (uintptr_t)lm, (uintptr_t)gui);
    spdlog::info("MyGuiBridge: ABI check — sizeof(string)={}, sizeof(vector<void*>)={}, _ITERATOR_DEBUG_LEVEL={}",
                 sizeof(std::string), sizeof(std::vector<void*>),
#ifdef _ITERATOR_DEBUG_LEVEL
                 _ITERATOR_DEBUG_LEVEL
#else
                 -1
#endif
                 );
    return true;
}

void* MyGuiBridge::GetLayoutManager() {
    if (!m_fnLayoutMgrInstance) return nullptr;
    return SEH_Call0(reinterpret_cast<void*>(m_fnLayoutMgrInstance));
}

void* MyGuiBridge::GetGui() {
    if (!m_fnGuiInstance) return nullptr;
    return SEH_Call0(reinterpret_cast<void*>(m_fnGuiInstance));
}

bool MyGuiBridge::LoadLayout(const std::string& layoutFile, const std::string& prefix) {
    if (!m_ready) return false;

    void* layoutMgr = GetLayoutManager();
    if (!layoutMgr) {
        spdlog::error("MyGuiBridge: LayoutManager singleton is null");
        return false;
    }

    // Unload this specific layout if it was already loaded
    UnloadLayout(layoutFile);

    spdlog::info("MyGuiBridge: Loading layout '{}' (fn=0x{:X}, mgr=0x{:X})",
                 layoutFile, (uintptr_t)m_fnLoadLayout, (uintptr_t)layoutMgr);

    std::vector<void*> result;
    bool callOk = SEH_LoadLayout(reinterpret_cast<void*>(m_fnLoadLayout),
                                  layoutMgr, &result, &layoutFile, &prefix, nullptr);

    if (!callOk) {
        spdlog::error("MyGuiBridge: loadLayout CRASHED (SEH caught exception) for '{}'", layoutFile);
        return false;
    }

    spdlog::info("MyGuiBridge: loadLayout returned {} widgets for '{}'",
                 result.size(), layoutFile);

    if (result.empty()) {
        spdlog::error("MyGuiBridge: loadLayout returned empty — file not found in resource system");
        return false;
    }

    spdlog::info("MyGuiBridge: Loaded {} root widgets from '{}'", result.size(), layoutFile);
    m_loadedLayouts[layoutFile] = std::move(result);
    return true;
}

void MyGuiBridge::UnloadLayout(const std::string& layoutFile) {
    auto it = m_loadedLayouts.find(layoutFile);
    if (it == m_loadedLayouts.end() || it->second.empty()) return;
    if (!m_ready) return;

    void* layoutMgr = GetLayoutManager();
    if (layoutMgr) {
        if (!SEH_UnloadLayout(reinterpret_cast<void*>(m_fnUnloadLayout), layoutMgr, &it->second)) {
            spdlog::error("MyGuiBridge: unloadLayout crashed for '{}'", layoutFile);
        }
    }
    m_loadedLayouts.erase(it);
}

void MyGuiBridge::UnloadAllLayouts() {
    if (!m_ready) return;

    void* layoutMgr = GetLayoutManager();
    for (auto& [name, widgets] : m_loadedLayouts) {
        if (!widgets.empty() && layoutMgr) {
            if (!SEH_UnloadLayout(reinterpret_cast<void*>(m_fnUnloadLayout), layoutMgr, &widgets)) {
                spdlog::error("MyGuiBridge: unloadLayout crashed for '{}'", name);
            }
        }
    }
    m_loadedLayouts.clear();
}

void* MyGuiBridge::FindWidget(const std::string& name) {
    if (!m_ready || !m_fnFindWidgetT) return nullptr;

    void* gui = GetGui();
    if (!gui) return nullptr;

    return SEH_FindWidgetT(reinterpret_cast<void*>(m_fnFindWidgetT), gui, &name, false);
}

void* MyGuiBridge::FindChildWidget(void* parent, const std::string& name) {
    if (!m_ready || !parent || !m_fnFindWidget) return nullptr;
    return SEH_FindWidget(reinterpret_cast<void*>(m_fnFindWidget), parent, &name);
}

void MyGuiBridge::SetVisible(void* widget, bool visible) {
    if (!m_ready || !widget || !m_fnSetVisible) return;
    if (!SEH_SetVisible(reinterpret_cast<void*>(m_fnSetVisible), widget, visible)) {
        spdlog::error("MyGuiBridge: setVisible crashed");
    }
}

void MyGuiBridge::SetCaption(void* widget, const std::string& text) {
    if (!m_ready || !widget || !m_fnSetCaption) return;
    if (!SEH_SetCaption(reinterpret_cast<void*>(m_fnSetCaption), widget, &text)) {
        spdlog::error("MyGuiBridge: setCaptionWithReplacing crashed");
    }
}

std::string MyGuiBridge::DecodeUString(const UStringLayout* ustr) {
    if (!ustr) return "";

    size_t len = ustr->size;
    if (len == 0 || len > 4096) return "";

    // basic_string<unsigned short> SSO: capacity < 8 means inline buf, >= 8 means heap ptr
    const unsigned short* data;
    if (ustr->capacity >= 8) {
        data = ustr->ptr;
    } else {
        data = ustr->buf;
    }

    if (!data) return "";

    // Validate the pointer looks sane
    uintptr_t addr = reinterpret_cast<uintptr_t>(data);
    if (addr < 0x10000 || addr > 0x00007FFFFFFFFFFF) return "";

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; i++) {
        unsigned short ch = data[i];
        if (ch >= 32 && ch < 128) {
            result.push_back(static_cast<char>(ch));
        }
    }
    return result;
}

std::string MyGuiBridge::GetCaption(void* widget) {
    if (!m_ready || !widget || !m_fnGetCaption) return "";

    void* ustringPtr = SEH_GetCaption(reinterpret_cast<void*>(m_fnGetCaption), widget);
    if (!ustringPtr) return "";

    // Validate the UString pointer
    uintptr_t addr = reinterpret_cast<uintptr_t>(ustringPtr);
    if (addr < 0x10000 || addr > 0x00007FFFFFFFFFFF) return "";

    return DecodeUString(reinterpret_cast<const UStringLayout*>(ustringPtr));
}

void MyGuiBridge::SetProperty(void* widget, const std::string& key, const std::string& value) {
    if (!m_ready || !widget || !m_fnSetProperty) return;
    if (!SEH_SetProperty(reinterpret_cast<void*>(m_fnSetProperty), widget, &key, &value)) {
        spdlog::error("MyGuiBridge: setProperty('{}', '{}') crashed", key, value);
    }
}

void MyGuiBridge::SetAlpha(void* widget, float alpha) {
    if (!m_ready || !widget || !m_fnSetAlpha) return;
    if (!SEH_SetAlpha(reinterpret_cast<void*>(m_fnSetAlpha), widget, alpha)) {
        spdlog::error("MyGuiBridge: setAlpha crashed");
    }
}

void* MyGuiBridge::CreateChildWidget(void* parent, const std::string& type, const std::string& skin,
                                      float x, float y, float w, float h, int align, const std::string& name) {
    if (!m_ready || !parent || !m_fnWidgetCreateWidgetReal) return nullptr;
    void* widget = SEH_CreateWidgetReal(reinterpret_cast<void*>(m_fnWidgetCreateWidgetReal),
                                         parent, &type, &skin, x, y, w, h, align, &name);
    if (!widget) {
        spdlog::error("MyGuiBridge: createWidgetRealT failed for '{}'", name);
    }
    return widget;
}

void* MyGuiBridge::CreateRootWidget(const std::string& type, const std::string& skin,
                                     float x, float y, float w, float h, int align,
                                     const std::string& layer, const std::string& name) {
    if (!m_ready || !m_fnGuiCreateWidgetReal) return nullptr;
    void* gui = GetGui();
    if (!gui) return nullptr;
    void* widget = SEH_CreateRootWidgetReal(reinterpret_cast<void*>(m_fnGuiCreateWidgetReal),
                                             gui, &type, &skin, x, y, w, h, align, &layer, &name);
    if (!widget) {
        spdlog::error("MyGuiBridge: Gui::createWidgetRealT failed for '{}'", name);
    }
    return widget;
}

} // namespace kmp
