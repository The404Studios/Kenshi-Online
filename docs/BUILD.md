# Build Guide

## Prerequisites

### Required Software
- **.NET 8.0 SDK** or later
- **Visual Studio 2022** or **VS Code** with C# extension
- **Git** for version control

### System Requirements
- Windows 10/11 (64-bit) - required for Kenshi integration
- 4GB RAM minimum
- 500MB disk space

## Quick Build

```bash
# Clone repository
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# Restore dependencies
dotnet restore

# Build solution
dotnet build --configuration Release

# Output location
# ./Kenshi-Online/bin/Release/net8.0/
```

## Build Configurations

### Debug Build
```bash
dotnet build --configuration Debug
```
- Includes debug symbols
- Verbose logging enabled
- No optimization

### Release Build
```bash
dotnet build --configuration Release
```
- Optimized code
- Minimal logging
- Production ready

## Project Structure

```
Kenshi-Online/
├── Kenshi-Online/
│   ├── KenshiMultiplayer.csproj   # Main project file
│   ├── Program.cs                  # Entry point
│   ├── Data/                       # Data models
│   ├── Game/                       # Game integration
│   ├── Managers/                   # State managers
│   ├── Networking/                 # Network code
│   │   └── Authority/              # Authority system
│   ├── Systems/                    # Game systems
│   └── Utility/                    # Helpers
├── docs/                           # Documentation
└── README.md
```

## Build Output

After successful build:
```
bin/Release/net8.0/
├── KenshiMultiplayer.dll          # Main assembly
├── KenshiMultiplayer.exe          # Executable
├── KenshiMultiplayer.deps.json    # Dependencies
├── KenshiMultiplayer.runtimeconfig.json
└── [dependency DLLs]
```

## Dependencies

Managed via NuGet:
- `System.Text.Json` - JSON serialization
- `Microsoft.IdentityModel.Tokens` - JWT authentication
- `System.IdentityModel.Tokens.Jwt` - Token handling

## Building Components Separately

### Server Only
```bash
# Build with server flag
dotnet build -p:DefineConstants="SERVER_BUILD"
```

### Client Only
```bash
# Build with client flag
dotnet build -p:DefineConstants="CLIENT_BUILD"
```

## Troubleshooting Build Issues

### "SDK not found"
```
The SDK 'Microsoft.NET.Sdk' specified could not be found.
```
**Solution**: Install .NET 8.0 SDK from https://dotnet.microsoft.com/download

### "Package restore failed"
```
Unable to resolve dependencies...
```
**Solution**:
```bash
dotnet nuget locals all --clear
dotnet restore
```

### "Access denied" on Windows
```
Access to the path is denied...
```
**Solution**:
- Close any running instances of the application
- Run terminal as Administrator
- Check antivirus isn't blocking

### "Missing reference"
```
The type or namespace 'X' could not be found
```
**Solution**:
```bash
dotnet restore --force
dotnet build --no-incremental
```

### "Platform not supported"
```
System.PlatformNotSupportedException
```
**Solution**: This project requires Windows for Kenshi memory integration. Build and run on Windows only.

## IDE Setup

### Visual Studio 2022
1. Open `Kenshi-Online.sln`
2. Right-click Solution → Restore NuGet Packages
3. Build → Build Solution (Ctrl+Shift+B)

### VS Code
1. Install C# extension
2. Open folder in VS Code
3. Press Ctrl+Shift+B to build
4. Select "build" task

### JetBrains Rider
1. Open `Kenshi-Online.sln`
2. Build → Build Solution

## Running Tests

```bash
# Run all tests
dotnet test

# Run with coverage
dotnet test --collect:"XPlat Code Coverage"
```

## Creating a Release

1. Update version in `.csproj`:
```xml
<Version>1.0.0</Version>
```

2. Build release:
```bash
dotnet publish -c Release -r win-x64 --self-contained true
```

3. Package output:
```
bin/Release/net8.0/win-x64/publish/
```

## Continuous Integration

The project supports GitHub Actions:
```yaml
# .github/workflows/build.yml
name: Build
on: [push, pull_request]
jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v3
      - uses: actions/setup-dotnet@v3
        with:
          dotnet-version: '8.0.x'
      - run: dotnet build --configuration Release
```

## Next Steps

After building:
- [Server Setup](SERVER_SETUP.md) - Configure and run server
- [Client Install](CLIENT_INSTALL.md) - Install on client machines
- [Troubleshooting](TROUBLESHOOTING.md) - Common issues
