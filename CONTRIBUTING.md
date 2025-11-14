# Contributing to Kenshi Online

Thank you for your interest in contributing to Kenshi Online! This document provides guidelines and information for contributors.

## 📋 Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [How to Contribute](#how-to-contribute)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Suggesting Features](#suggesting-features)

---

## 🤝 Code of Conduct

Be respectful and inclusive. We welcome contributors of all skill levels!

**Expected Behavior:**
- Use welcoming and inclusive language
- Be respectful of differing viewpoints
- Accept constructive criticism gracefully
- Focus on what is best for the community

**Unacceptable Behavior:**
- Harassment, trolling, or derogatory comments
- Publishing others' private information
- Other conduct inappropriate in a professional setting

---

## 🚀 Getting Started

### Prerequisites

- **Windows 10/11** (64-bit)
- **Visual Studio 2022** with C++ support
- **.NET 8.0 SDK**
- **CMake** 3.20+
- **Git**
- **Kenshi** (for testing)

### Fork and Clone

```bash
# Fork the repository on GitHub, then:
git clone https://github.com/YOUR_USERNAME/Kenshi-Online.git
cd Kenshi-Online
```

---

## 💻 Development Setup

### 1. Build C++ Plugin

```batch
Build_Plugin.bat
```

This will:
- Download nlohmann/json dependency
- Configure CMake
- Build Re_Kenshi_Plugin.dll

### 2. Build C# Projects

```batch
# Option 1: Use PLAY.bat (auto-builds)
PLAY.bat

# Option 2: Manual build
dotnet build KenshiOnline.sln -c Debug
```

### 3. Test Your Changes

```batch
# Start in Solo mode for local testing
PLAY.bat
> [1] Solo Mode

# Then inject plugin and test in Kenshi
```

---

## 🎯 How to Contribute

### Types of Contributions

1. **Bug Fixes** - Fix crashes, errors, or unexpected behavior
2. **New Features** - Add new multiplayer features
3. **Documentation** - Improve guides, comments, or README
4. **Testing** - Test on different systems, report issues
5. **Translations** - Add support for more languages

### Contribution Workflow

1. **Find an Issue**
   - Check [Issues](https://github.com/The404Studios/Kenshi-Online/issues)
   - Look for `good first issue` or `help wanted` labels
   - Or create a new issue to discuss your idea

2. **Create a Branch**
   ```bash
   git checkout -b feature/your-feature-name
   # or
   git checkout -b fix/bug-description
   ```

3. **Make Your Changes**
   - Write clean, documented code
   - Follow coding standards (see below)
   - Test thoroughly

4. **Commit Your Changes**
   - Follow commit guidelines (see below)
   - Reference issue numbers

5. **Push and Create PR**
   ```bash
   git push origin feature/your-feature-name
   ```
   - Create Pull Request on GitHub
   - Fill out PR template
   - Link related issues

---

## 📝 Coding Standards

### C# Code (.NET 8.0)

```csharp
// Use PascalCase for public members
public class EntityManager
{
    // Use camelCase for private fields with underscore prefix
    private readonly ConcurrentDictionary<Guid, Entity> _entities;

    // Use PascalCase for properties
    public int EntityCount => _entities.Count;

    // Use PascalCase for methods
    public void AddEntity(Entity entity)
    {
        // Use var for obvious types
        var id = entity.Id;

        // Use explicit types for non-obvious
        ConcurrentDictionary<string, int> stats = new();
    }
}

// Always use braces for control flow
if (condition)
{
    DoSomething();
}

// Add XML documentation for public APIs
/// <summary>
/// Adds an entity to the manager.
/// </summary>
/// <param name="entity">The entity to add</param>
public void AddEntity(Entity entity)
```

### C++ Code (C++17)

```cpp
// Use PascalCase for classes
class NetworkClient
{
public:
    // Use PascalCase for public methods
    bool Connect(const std::string& serverAddress);

private:
    // Use m_ prefix for member variables
    std::string m_serverAddress;
    HANDLE m_pipe;

    // Use PascalCase for private methods
    void HandleMessage(const NetworkMessage& msg);
};

// Use descriptive variable names
const float maxSyncRadius = 100.0f;  // Not: float r = 100;

// Always use braces
if (isConnected)
{
    SendHeartbeat();
}

// Add comments for complex logic
// Calculate spatial grid cell for position
auto cellKey = GetGridCell(position);
```

### General Guidelines

- **Naming**: Use descriptive names, avoid abbreviations
- **Comments**: Explain WHY, not WHAT (code shows what)
- **Formatting**: Use 4 spaces for indentation (no tabs)
- **Line Length**: Aim for 120 characters max
- **File Headers**: Include brief description at top of file

---

## 💬 Commit Guidelines

### Commit Message Format

```
<type>: <subject>

<body>

<footer>
```

### Types

- **feat**: New feature
- **fix**: Bug fix
- **docs**: Documentation changes
- **style**: Code style changes (formatting, no logic change)
- **refactor**: Code refactoring
- **perf**: Performance improvement
- **test**: Adding or updating tests
- **chore**: Build process or tooling changes

### Examples

```
feat: Add voice chat system

Implemented voice chat using Opus codec and UDP transport.
- Added VoiceChat.cs with spatial audio
- Updated NetworkProtocol.h with voice packets
- Added settings UI for voice control

Closes #123

---

fix: Crash when disconnecting during combat

Fixed null reference exception in CombatSync when player
disconnects while in active combat.

- Added null check in ProcessAttack
- Clean up combat state on disconnect
- Added unit test for edge case

Fixes #456
```

### Commit Best Practices

- Write in imperative mood: "Add feature" not "Added feature"
- Keep subject line under 50 characters
- Wrap body at 72 characters
- Reference issues and PRs
- Separate subject from body with blank line

---

## 🔄 Pull Request Process

### Before Submitting

- [ ] Code builds without errors
- [ ] All tests pass (if applicable)
- [ ] Code follows style guidelines
- [ ] Documentation updated (if needed)
- [ ] Commit messages follow guidelines
- [ ] Branch is up to date with main

### PR Description Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
Describe how you tested your changes

## Checklist
- [ ] Code compiles
- [ ] Follows coding standards
- [ ] Documentation updated
- [ ] Tested in game

## Screenshots (if applicable)
Add screenshots or videos

## Related Issues
Closes #123
```

### Review Process

1. Maintainer will review within 1-3 days
2. Address feedback and requested changes
3. Once approved, maintainer will merge
4. PR author may be asked to squash commits

### After Merge

- Your contribution will be in next release
- You'll be added to contributors list
- Thank you! 🎉

---

## 🐛 Reporting Bugs

### Before Reporting

1. **Search existing issues** - Your bug may already be reported
2. **Try latest version** - Bug might be fixed already
3. **Gather information** - Logs, steps to reproduce, system info

### Bug Report Template

```markdown
**Describe the Bug**
Clear description of what the bug is

**To Reproduce**
Steps to reproduce:
1. Start server with '...'
2. Connect client '...'
3. Perform action '...'
4. See error

**Expected Behavior**
What you expected to happen

**Screenshots/Logs**
Add screenshots or log files

**System Information:**
- OS: Windows 11
- .NET Version: 8.0.1
- Kenshi Version: 1.0.60
- Kenshi Online Version: 2.0

**Additional Context**
Any other information
```

---

## 💡 Suggesting Features

### Before Suggesting

1. **Check existing requests** - Feature may be planned
2. **Consider scope** - Is it aligned with project goals?
3. **Think about implementation** - How would it work?

### Feature Request Template

```markdown
**Is your feature related to a problem?**
Description of the problem

**Describe the solution**
Clear description of what you want

**Describe alternatives**
Other solutions you've considered

**Implementation Ideas**
Technical approach (optional)

**Additional Context**
Screenshots, mockups, examples
```

---

## 🏗️ Project Structure

```
KenshiOnline.Launcher/      - Unified launcher application
KenshiOnline.Core/          - Core game logic (entities, sync)
KenshiOnline.Server/        - Standalone server (legacy)
KenshiOnline.ClientService/ - Standalone client (legacy)
Re_Kenshi_Plugin/           - C++ game plugin
  ├── include/              - Header files
  ├── src/                  - Source files
  └── vendor/               - Third-party libraries
```

### Key Files

- `PLAY.bat` - Entry point for users
- `Build_Plugin.bat` - C++ build automation
- `KenshiOnline.Launcher/Program.cs` - Main launcher logic
- `KenshiOnline.Core/Synchronization/EntityManager.cs` - Core sync
- `Re_Kenshi_Plugin/src/KenshiOnlinePlugin.cpp` - Plugin entry

---

## 📚 Useful Resources

- [Kenshi Modding Wiki](http://kenshi.wikia.com/wiki/Modding)
- [.NET Documentation](https://docs.microsoft.com/en-us/dotnet/)
- [CMake Documentation](https://cmake.org/documentation/)
- [nlohmann/json Documentation](https://json.nlohmann.me/)

---

## 🙋 Questions?

- **GitHub Discussions**: Ask questions, share ideas
- **Discord**: Real-time chat with community (link)
- **Email**: (your contact email)

---

## 🎉 Recognition

Contributors will be recognized in:
- README.md contributors section
- CHANGELOG.md for each release
- GitHub contributors graph

Thank you for contributing to Kenshi Online!
