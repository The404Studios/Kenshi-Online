# Kenshi Online Offset Database

This folder contains the online offset database for the Kenshi-Online multiplayer mod.

## File Structure

- `kenshi_offsets.json` - Main offset database (auto-downloaded by clients)
- `README.md` - This documentation file

## JSON Schema

The offset database follows this structure:

```json
{
  "version": 1,                          // Schema version
  "gameVersion": "1.0.64",               // Target game version
  "isUniversal": false,                  // If true, works across versions
  "lastUpdated": "2025-12-08T00:00:00Z", // Last update timestamp
  "author": "The404Studios",
  "notes": "Description",
  "checksum": "",                        // SHA256 of offsets object (optional)

  "offsets": {
    // Core game pointer offsets (RVA from module base)
    "baseAddress": 5368709120,           // 0x140000000
    "worldInstance": 38633280,           // 0x24D8F40
    // ... more offsets
  },

  "functionOffsets": {
    // Function address offsets (RVA)
    "spawnCharacter": 9125000,           // 0x8B3C80
    // ... more functions
  },

  "structureOffsets": {
    // In-structure field offsets
    "character": {
      "position": 112,                   // 0x70
      // ... more fields
    }
  },

  "patterns": {
    // Pattern signatures for dynamic scanning
    "gameWorld": {
      "name": "GameWorld",
      "pattern": "48 8B 05 ?? ?? ?? ??",
      "mask": "xxx????",
      "offset": 3,
      "isRelative": true,
      "relativeBase": 7
    }
  },

  "supportedVersions": ["1.0.64", "1.0.63"]
}
```

## Offset Types

### 1. Game Offsets (`offsets`)
Relative Virtual Addresses (RVA) from the game's module base address.
- Use decimal values in JSON
- Calculate absolute: `moduleBase + offset`

### 2. Function Offsets (`functionOffsets`)
RVA addresses for game functions that can be hooked or called.

### 3. Structure Offsets (`structureOffsets`)
Byte offsets within game structures (Character, Squad, etc.)

### 4. Pattern Signatures (`patterns`)
Byte patterns for dynamic offset discovery:
- `pattern`: Hex bytes (space separated), `??` for wildcards
- `mask`: `x` = exact match, `?` = wildcard
- `offset`: Bytes from pattern start to read target
- `isRelative`: If true, value is RIP-relative offset
- `relativeBase`: Instruction size for relative calculation

## Updating Offsets

When Kenshi updates:

1. **Detect new version**: Check game executable version
2. **Pattern scan first**: Most patterns remain valid across versions
3. **Manual verification**: Use Cheat Engine or x64dbg to verify
4. **Update JSON**: Modify values and bump `lastUpdated`
5. **Test thoroughly**: Verify all mod functionality

## Hosting

The offset file is served from multiple locations for redundancy:
1. GitHub repository (raw file)
2. Dedicated API server
3. Backup Pastebin/Gist

## Client Behavior

1. On startup, client fetches latest offsets
2. Validates checksum (if provided)
3. Caches locally for offline use
4. Falls back to hardcoded values if all sources fail

## Converting Values

```
Decimal to Hex: 38633280 → 0x24D8F40
Hex to Decimal: 0x24D8F40 → 38633280

Python: hex(38633280), int('24D8F40', 16)
```

## Version-Specific Notes

### Kenshi 1.0.64
- Current stable version
- All offsets verified

### Kenshi 1.0.65+ (Future)
- May require pattern re-scanning
- Structure offsets likely unchanged
