# Third-Party Dependencies

This directory contains third-party header-only libraries required to build the C++ plugin.

## Required Libraries

### 1. nlohmann/json (REQUIRED)

**Current Status:** ⚠️ Stub only - Download required

**What it does:** JSON parsing and serialization for network protocol

**Download:**
```batch
# Windows (PowerShell)
Invoke-WebRequest -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" -OutFile "nlohmann/json.hpp"

# Or manually download from:
https://github.com/nlohmann/json/releases/latest/download/json.hpp
```

**Where to place:**
```
Re_Kenshi_Plugin/vendor/nlohmann/json.hpp
```

**License:** MIT License

**Repository:** https://github.com/nlohmann/json

---

### 2. RapidJSON (Referenced but not used)

**Current Status:** ❌ Not needed (CMakeLists.txt references it but code uses nlohmann/json)

If you want to use RapidJSON instead:
- Download: https://github.com/Tencent/rapidjson
- Extract to: `Re_Kenshi_Plugin/vendor/rapidjson/`

---

## Quick Setup (Automated)

Run the download script from the root directory:

```batch
cd Re_Kenshi_Plugin
Download_Dependencies.bat
```

This will automatically download all required libraries.

---

## Manual Setup

If you prefer to download manually:

1. **nlohmann/json:**
   - Go to: https://github.com/nlohmann/json/releases/latest
   - Download: `json.hpp`
   - Place at: `Re_Kenshi_Plugin/vendor/nlohmann/json.hpp`
   - Replace the stub file

---

## Verification

After downloading, verify the files exist:

```
Re_Kenshi_Plugin/vendor/
├── nlohmann/
│   └── json.hpp          (25,000+ lines)
└── README.md             (this file)
```

The stub json.hpp is only ~60 lines - the real one should be 25,000+ lines.

---

## Build Without Dependencies

The project will compile with the stub files, but won't work correctly at runtime. For testing the build system, the stubs are sufficient. For actual use, download the real libraries.

---

## License Information

- **nlohmann/json:** MIT License
  - Copyright (c) 2013-2023 Niels Lohmann
  - https://github.com/nlohmann/json/blob/develop/LICENSE.MIT

Make sure to comply with the licenses of all third-party libraries.
