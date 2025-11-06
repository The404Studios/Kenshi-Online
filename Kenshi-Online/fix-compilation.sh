#!/bin/bash

# Comprehensive compilation fix script for Kenshi Online
# Run from the Kenshi-Online directory

echo "=========================================="
echo "Kenshi Online - Comprehensive Fix Script"
echo "=========================================="

# 1. Fix Logger usage (change from instance to static)
echo "Step 1: Fixing Logger usage..."
find Game Systems -name "*.cs" -type f 2>/dev/null | while read file; do
    # Change: private Logger logger = new Logger("ClassName");
    # To: private const string LoggerPrefix = "ClassName";
    sed -i 's/private readonly Logger logger = new Logger/private const string LoggerPrefix =/g' "$file"
    sed -i 's/private Logger logger = new Logger/private const string LoggerPrefix =/g' "$file"

    # Change: logger.Log(message)
    # To: Logger.Log($"[{LoggerPrefix}] {message}")
    sed -i 's/logger\.Log(/Logger.Log($"[{LoggerPrefix}] " + /g' "$file"
done

# 2. Rename duplicate Program.cs
echo "Step 2: Handling duplicate Program.cs..."
if [ -f "Program.cs" ] && [ -f "EnhancedProgram.cs" ]; then
    mv Program.cs Program.cs.original
    echo "  Renamed Program.cs to Program.cs.original"
fi

# 3. Fix PlayerData.Position property
echo "Step 3: Fixing PlayerData.Position property..."
sed -i 's/playerData\.Position/playerData.CurrentPosition/g' Game/*.cs
sed -i 's/playerData\.Position/playerData.CurrentPosition/g' Systems/*.cs
sed -i 's/player\.Position/player.CurrentPosition/g' Game/*.cs Systems/*.cs

# 4. Fix Health type (int to float)
echo "Step 4: Fixing Health type conversions..."
# This is complex - will need manual fixes for each case

# 5. Add Position property alias to PlayerData
echo "Step 5: Will add Position property to PlayerData..."
cat >> Data/PlayerDataExtensions.cs << 'EOL'
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Data
{
    public partial class PlayerData
    {
        // Compatibility alias for Position
        public Position Position
        {
            get => CurrentPosition;
            set => CurrentPosition = value;
        }
    }
}
EOL

# 6. Fix Vector3 references
echo "Step 6: Fixing Vector3 references..."
# Vector3 is defined in KenshiStructures.cs, need to use it properly

# 7. Clean build
echo "Step 7: Cleaning build artifacts..."
rm -rf bin obj

echo ""
echo "=========================================="
echo "Fix script completed!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "1. Review changes"
echo "2. Run: dotnet build"
echo "3. Fix remaining manual issues"
echo ""
