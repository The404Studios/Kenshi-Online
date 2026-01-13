#!/bin/bash
#
# Kenshi Online - Build Script (Linux/Mac)
#
# This script builds the entire project from scratch.
# Run from repository root: ./build/build.sh
#
# Requirements:
#   - .NET 8.0 SDK
#   - CMake 3.15+
#   - Visual Studio Build Tools (for Windows DLL, via cross-compile or skip)
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$ROOT_DIR/dist"
CSHARP_PROJECT="$ROOT_DIR/Kenshi-Online/KenshiMultiplayer.csproj"
CPP_PROJECT="$ROOT_DIR/KenshiOnlineMod"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Kenshi Online Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Function to check prerequisites
check_prerequisites() {
    echo -e "${YELLOW}Checking prerequisites...${NC}"

    # Check .NET SDK
    if ! command -v dotnet &> /dev/null; then
        echo -e "${RED}ERROR: .NET SDK not found. Please install .NET 8.0 SDK.${NC}"
        exit 1
    fi

    DOTNET_VERSION=$(dotnet --version)
    echo -e "  .NET SDK: ${GREEN}$DOTNET_VERSION${NC}"

    # Check if version is 8.x
    if [[ ! "$DOTNET_VERSION" =~ ^8\. ]]; then
        echo -e "${YELLOW}  WARNING: .NET 8.0 recommended, found $DOTNET_VERSION${NC}"
    fi

    # Check CMake (optional for DLL)
    if command -v cmake &> /dev/null; then
        CMAKE_VERSION=$(cmake --version | head -n1)
        echo -e "  CMake: ${GREEN}$CMAKE_VERSION${NC}"
    else
        echo -e "  CMake: ${YELLOW}Not found (C++ DLL will be skipped)${NC}"
    fi

    echo ""
}

# Function to clean previous build
clean_build() {
    echo -e "${YELLOW}Cleaning previous build...${NC}"

    # Clean .NET build
    if [ -d "$ROOT_DIR/Kenshi-Online/bin" ]; then
        rm -rf "$ROOT_DIR/Kenshi-Online/bin"
    fi
    if [ -d "$ROOT_DIR/Kenshi-Online/obj" ]; then
        rm -rf "$ROOT_DIR/Kenshi-Online/obj"
    fi

    # Clean output directory
    if [ -d "$OUTPUT_DIR" ]; then
        rm -rf "$OUTPUT_DIR"
    fi

    mkdir -p "$OUTPUT_DIR"
    echo -e "  ${GREEN}Clean complete${NC}"
    echo ""
}

# Function to build C# project
build_csharp() {
    echo -e "${YELLOW}Building C# project...${NC}"

    cd "$ROOT_DIR/Kenshi-Online"

    # Restore dependencies
    echo "  Restoring dependencies..."
    dotnet restore --verbosity quiet

    # Build release
    echo "  Building Release configuration..."
    BUILD_OUTPUT=$(dotnet build -c Release --no-restore 2>&1)
    BUILD_EXIT=$?

    # Check for errors
    if [ $BUILD_EXIT -ne 0 ]; then
        echo -e "${RED}  BUILD FAILED${NC}"
        echo "$BUILD_OUTPUT"
        exit 1
    fi

    # Check for warnings
    WARNING_COUNT=$(echo "$BUILD_OUTPUT" | grep -c "warning " || true)
    ERROR_COUNT=$(echo "$BUILD_OUTPUT" | grep -c "error " || true)

    if [ "$ERROR_COUNT" -gt 0 ]; then
        echo -e "${RED}  Build errors: $ERROR_COUNT${NC}"
        echo "$BUILD_OUTPUT" | grep "error "
        exit 1
    fi

    if [ "$WARNING_COUNT" -gt 0 ]; then
        echo -e "${YELLOW}  Build warnings: $WARNING_COUNT${NC}"
    fi

    echo -e "  ${GREEN}C# build complete${NC}"
    echo ""
}

# Function to build C++ DLL (Windows only, skip on Linux)
build_cpp() {
    echo -e "${YELLOW}Building C++ DLL...${NC}"

    if [[ "$OSTYPE" != "msys" && "$OSTYPE" != "cygwin" && "$OSTYPE" != "win32" ]]; then
        echo -e "  ${YELLOW}Skipping C++ build (Windows-only DLL)${NC}"
        echo -e "  ${YELLOW}The DLL must be built on Windows for Kenshi compatibility${NC}"
        echo ""
        return 0
    fi

    if ! command -v cmake &> /dev/null; then
        echo -e "  ${YELLOW}Skipping C++ build (CMake not found)${NC}"
        echo ""
        return 0
    fi

    cd "$CPP_PROJECT"

    # Create build directory
    mkdir -p build
    cd build

    # Configure CMake
    echo "  Configuring CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

    # Build
    echo "  Building..."
    cmake --build . --config Release > /dev/null 2>&1

    if [ -f "bin/Release/KenshiOnlineMod.dll" ]; then
        echo -e "  ${GREEN}C++ DLL build complete${NC}"
    else
        echo -e "  ${YELLOW}DLL not found (may need Windows build)${NC}"
    fi

    echo ""
}

# Function to package distribution
package_dist() {
    echo -e "${YELLOW}Packaging distribution...${NC}"

    mkdir -p "$OUTPUT_DIR"

    # Copy C# executable
    if [ -f "$ROOT_DIR/Kenshi-Online/bin/Release/net8.0/KenshiOnline.exe" ]; then
        cp "$ROOT_DIR/Kenshi-Online/bin/Release/net8.0/KenshiOnline.exe" "$OUTPUT_DIR/"
        echo "  Copied KenshiOnline.exe"
    elif [ -f "$ROOT_DIR/Kenshi-Online/bin/Release/net8.0/KenshiOnline.dll" ]; then
        cp "$ROOT_DIR/Kenshi-Online/bin/Release/net8.0/KenshiOnline.dll" "$OUTPUT_DIR/"
        echo "  Copied KenshiOnline.dll"
    fi

    # Copy dependencies
    cp "$ROOT_DIR/Kenshi-Online/bin/Release/net8.0/"*.dll "$OUTPUT_DIR/" 2>/dev/null || true

    # Copy C++ DLL if exists
    if [ -f "$CPP_PROJECT/build/bin/Release/KenshiOnlineMod.dll" ]; then
        cp "$CPP_PROJECT/build/bin/Release/KenshiOnlineMod.dll" "$OUTPUT_DIR/"
        echo "  Copied KenshiOnlineMod.dll"
    fi

    # Copy offset table
    if [ -f "$ROOT_DIR/offsets/offset-table.json" ]; then
        cp "$ROOT_DIR/offsets/offset-table.json" "$OUTPUT_DIR/"
        echo "  Copied offset-table.json"
    fi

    # Create README
    cat > "$OUTPUT_DIR/README.txt" << 'EOF'
Kenshi Online - Quick Start
===========================

1. Launch Kenshi (version 1.0.64)
2. Run KenshiOnline.exe
3. Select "Host" to create a session, or "Join" to connect

Requirements:
- Kenshi version 1.0.64 (64-bit)
- Windows 10 or later
- .NET 8.0 Runtime

Troubleshooting:
- If injection fails, try running as administrator
- Ensure your antivirus is not blocking the mod
- All players must have the same Kenshi version

For more help, visit: https://github.com/The404Studios/Kenshi-Online
EOF
    echo "  Created README.txt"

    echo -e "  ${GREEN}Packaging complete${NC}"
    echo ""
}

# Function to verify build
verify_build() {
    echo -e "${YELLOW}Verifying build...${NC}"

    VERIFY_PASS=true

    # Check executable exists
    if [ -f "$OUTPUT_DIR/KenshiOnline.exe" ] || [ -f "$OUTPUT_DIR/KenshiOnline.dll" ]; then
        echo -e "  Executable: ${GREEN}OK${NC}"
    else
        echo -e "  Executable: ${RED}MISSING${NC}"
        VERIFY_PASS=false
    fi

    # Check for common dependencies
    for dep in "Newtonsoft.Json.dll" "MemorySharp.dll"; do
        if [ -f "$OUTPUT_DIR/$dep" ]; then
            echo -e "  $dep: ${GREEN}OK${NC}"
        else
            echo -e "  $dep: ${YELLOW}Missing (may be framework-included)${NC}"
        fi
    done

    # Summary
    echo ""
    if [ "$VERIFY_PASS" = true ]; then
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}  BUILD SUCCESSFUL${NC}"
        echo -e "${GREEN}========================================${NC}"
        echo ""
        echo "Output directory: $OUTPUT_DIR"
        echo ""
        ls -la "$OUTPUT_DIR"
    else
        echo -e "${RED}========================================${NC}"
        echo -e "${RED}  BUILD VERIFICATION FAILED${NC}"
        echo -e "${RED}========================================${NC}"
        exit 1
    fi
}

# Main execution
main() {
    check_prerequisites
    clean_build
    build_csharp
    build_cpp
    package_dist
    verify_build
}

main "$@"
