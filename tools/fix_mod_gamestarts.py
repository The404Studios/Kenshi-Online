"""
Fix kenshi-online.mod: Remove Player squad references from vanilla game starts.

The mod incorrectly adds "30-kenshi-online.mod" squad to the Singleplayer game start,
causing 20 Player 1 characters to spawn in every new game. This script patches the
squad count from 2 to 1 in the Singleplayer entry, effectively removing the Player squad
while keeping the vanilla squad reference.
"""

import struct
import shutil
import os

MOD_DIR = os.path.join(os.path.dirname(__file__), '..', 'mods', 'kenshi-online')
MOD_PATH = os.path.join(MOD_DIR, 'kenshi-online.mod')

# Also patch the copy in data/
DATA_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'data')
DATA_MOD_PATH = os.path.join(DATA_DIR, 'kenshi-online.mod')

# Also patch the source copy in KenshiMP/
SRC_MOD_PATH = os.path.join(os.path.dirname(__file__), '..', 'kenshi-online.mod')

def patch_mod(path):
    if not os.path.exists(path):
        print(f"  SKIP: {path} not found")
        return False

    with open(path, 'rb') as f:
        data = bytearray(f.read())

    needle = b'30-kenshi-online.mod'

    # Find the FIRST occurrence (Singleplayer game start's squad list)
    idx = data.find(needle)
    if idx == -1:
        print(f"  SKIP: No '30-kenshi-online.mod' found in {path}")
        return False

    # The structure before this reference:
    #   05 00 00 00 "squad" 02 00 00 00 14 00 00 00 "30-kenshi-online.mod" ...
    #   "squad" property name, count=2, length=20, reference string
    #
    # We need to find the count byte (02) and change it to 01,
    # AND null out the "30-kenshi-online.mod" reference + its length prefix.

    # Walk back from the reference to find the count
    # The structure is: count(4 bytes) + length(4 bytes) + string(20 bytes)
    # So count is at idx - 8, and it should be 02 00 00 00
    count_offset = idx - 8
    count_val = struct.unpack_from('<I', data, count_offset)[0]

    print(f"  Found at offset 0x{idx:04X}, squad count = {count_val}")

    if count_val < 2:
        print(f"  SKIP: Count already {count_val}, nothing to fix")
        return False

    # Change count from 2 to 1
    struct.pack_into('<I', data, count_offset, count_val - 1)

    # Null out the length prefix (4 bytes before the string) and the string itself
    length_offset = idx - 4
    for i in range(length_offset, idx + len(needle)):
        data[i] = 0

    # Also fill the gap bytes after the string with zeros (padding that was there)
    # The reference is followed by 12 bytes of zeros then the next reference
    # We need to shift the remaining data or leave the zeros
    # Safest: just zero out the 24 bytes (4 length + 20 string) and leave the zeros

    print(f"  Patched: count {count_val} -> {count_val - 1}, nulled reference at 0x{length_offset:04X}-0x{idx + len(needle):04X}")

    # Backup original
    backup_path = path + '.bak'
    if not os.path.exists(backup_path):
        shutil.copy2(path, backup_path)
        print(f"  Backup saved to {backup_path}")

    with open(path, 'wb') as f:
        f.write(data)

    print(f"  FIXED: {path}")
    return True

if __name__ == '__main__':
    print("=== Fixing kenshi-online.mod: removing Player squad from vanilla game starts ===\n")

    patched = 0
    for path in [MOD_PATH, DATA_MOD_PATH, SRC_MOD_PATH]:
        print(f"Checking: {path}")
        if patch_mod(path):
            patched += 1
        print()

    if patched > 0:
        print(f"Done! Patched {patched} file(s). Player characters will no longer spawn in vanilla game starts.")
        print("The 'Multiplayer' game start still works correctly.")
    else:
        print("No files needed patching.")
