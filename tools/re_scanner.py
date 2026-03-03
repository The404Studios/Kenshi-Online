"""
Kenshi-Online Reverse Engineering Scanner v3
Analyzes kenshi_x64.exe to find function signatures, struct offsets, and vtables.
Outputs IDA-style byte patterns for use in patterns.h

v3 improvements over v2:
- Structure offset discovery (health chains, position, name, faction, inventory)
- Vtable scanning (Ogre::FrameListener, Character class)
- Global singleton discovery (GameWorld, ZoneManager)
- Auto-generates offsets.json and C++ code blocks for patterns.h and game_types.h
"""

import struct
import sys
import os
import json
from collections import defaultdict


class PEParser:
    def __init__(self, filepath):
        with open(filepath, 'rb') as f:
            self.data = f.read()
        self.base = 0x140000000
        self.sections = []
        self.text_start = 0
        self.text_size = 0
        self.text_rva = 0
        self.rdata_start = 0
        self.rdata_size = 0
        self.rdata_rva = 0
        self.data_start = 0
        self.data_size = 0
        self.data_rva = 0
        self._parse_pe()

    def _parse_pe(self):
        if self.data[:2] != b'MZ':
            raise ValueError("Not a valid PE file")
        e_lfanew = struct.unpack_from('<I', self.data, 0x3C)[0]
        if self.data[e_lfanew:e_lfanew+4] != b'PE\x00\x00':
            raise ValueError("Invalid PE signature")
        coff_offset = e_lfanew + 4
        num_sections = struct.unpack_from('<H', self.data, coff_offset + 2)[0]
        optional_header_size = struct.unpack_from('<H', self.data, coff_offset + 16)[0]
        opt_offset = coff_offset + 20
        magic = struct.unpack_from('<H', self.data, opt_offset)[0]
        if magic == 0x20b:
            self.base = struct.unpack_from('<Q', self.data, opt_offset + 24)[0]
        section_offset = opt_offset + optional_header_size
        for i in range(num_sections):
            off = section_offset + i * 40
            name = self.data[off:off+8].rstrip(b'\x00').decode('ascii', errors='ignore')
            vsize = struct.unpack_from('<I', self.data, off + 8)[0]
            rva = struct.unpack_from('<I', self.data, off + 12)[0]
            raw_size = struct.unpack_from('<I', self.data, off + 16)[0]
            raw_offset = struct.unpack_from('<I', self.data, off + 20)[0]
            self.sections.append({
                'name': name, 'vsize': vsize, 'rva': rva,
                'raw_size': raw_size, 'raw_offset': raw_offset,
            })
            if name == '.text':
                self.text_start = raw_offset
                self.text_size = raw_size
                self.text_rva = rva
            elif name == '.rdata':
                self.rdata_start = raw_offset
                self.rdata_size = raw_size
                self.rdata_rva = rva
            elif name == '.data':
                self.data_start = raw_offset
                self.data_size = raw_size
                self.data_rva = rva

    def rva_to_offset(self, rva):
        for s in self.sections:
            if s['rva'] <= rva < s['rva'] + s['raw_size']:
                return rva - s['rva'] + s['raw_offset']
        return None

    def offset_to_rva(self, offset):
        for s in self.sections:
            if s['raw_offset'] <= offset < s['raw_offset'] + s['raw_size']:
                return offset - s['raw_offset'] + s['rva']
        return None

    def find_string(self, target):
        if isinstance(target, str):
            target = target.encode('ascii')
        results = []
        start = 0
        while True:
            idx = self.data.find(target, start)
            if idx == -1:
                break
            results.append(idx)
            start = idx + 1
        return results

    def find_string_xrefs(self, string_offset, search_all=True):
        """Find code that references a string via RIP-relative addressing."""
        string_rva = self.offset_to_rva(string_offset)
        if string_rva is None:
            return []
        string_va = self.base + string_rva
        xrefs = []
        text_data = self.data[self.text_start:self.text_start + self.text_size]
        text_end = len(text_data) - 7

        for i in range(text_end):
            b0 = text_data[i]
            b1 = text_data[i + 1] if i + 1 < text_end else 0

            # REX.W LEA: 48 8D xx [RIP+disp32] (7 bytes)
            # REX.WR LEA: 4C 8D xx [RIP+disp32] (7 bytes)
            if b0 in (0x48, 0x4C) and b1 == 0x8D and i + 6 < text_end:
                modrm = text_data[i + 2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', text_data, i + 3)[0]
                    target_va = (self.base + self.text_rva + i) + 7 + disp32
                    if target_va == string_va:
                        xrefs.append(self.text_start + i)

            # Non-REX LEA: 8D xx [RIP+disp32] (6 bytes)
            if b0 == 0x8D and i + 5 < text_end:
                modrm = b1
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', text_data, i + 2)[0]
                    target_va = (self.base + self.text_rva + i) + 6 + disp32
                    if target_va == string_va:
                        xrefs.append(self.text_start + i)

            # REX.W MOV reg, [RIP+disp32]: 48 8B xx (7 bytes)
            if b0 in (0x48, 0x4C) and b1 == 0x8B and i + 6 < text_end:
                modrm = text_data[i + 2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', text_data, i + 3)[0]
                    target_va = (self.base + self.text_rva + i) + 7 + disp32
                    if target_va == string_va:
                        xrefs.append(self.text_start + i)

            # MOV [RIP+disp32], reg: 48 89 xx (7 bytes)
            if b0 in (0x48, 0x4C) and b1 == 0x89 and i + 6 < text_end:
                modrm = text_data[i + 2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', text_data, i + 3)[0]
                    target_va = (self.base + self.text_rva + i) + 7 + disp32
                    if target_va == string_va:
                        xrefs.append(self.text_start + i)

            # CMP [RIP+disp32]: 48 39/3B xx (7 bytes)
            if b0 == 0x48 and b1 in (0x39, 0x3B) and i + 6 < text_end:
                modrm = text_data[i + 2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', text_data, i + 3)[0]
                    target_va = (self.base + self.text_rva + i) + 7 + disp32
                    if target_va == string_va:
                        xrefs.append(self.text_start + i)

        return xrefs

    def find_function_start(self, offset, max_search=2048):
        """Walk backwards from an offset to find the function prologue."""
        search_start = max(self.text_start, offset - max_search)
        d = self.data

        # Strategy 1: Find CC/C3 padding then next prologue
        for i in range(offset - 1, search_start, -1):
            if d[i] in (0xCC, 0xC3):
                candidate = i + 1
                while candidate < offset and d[candidate] == 0xCC:
                    candidate += 1
                if candidate < offset and self._is_prologue(candidate):
                    return candidate

        # Strategy 2: Prologue with alignment verification
        for i in range(offset - 1, search_start, -1):
            if self._is_prologue(i) and (offset - i) < 512:
                if i > 0 and d[i - 1] in (0xCC, 0xC3, 0xCB):
                    return i

        # Strategy 3: Nearest prologue
        for i in range(offset - 1, search_start, -1):
            if self._is_prologue(i) and (offset - i) < 256:
                return i

        return None

    def _is_prologue(self, offset):
        if offset + 5 > len(self.data):
            return False
        d = self.data

        # mov [rsp+xx], rbx
        if d[offset] == 0x48 and d[offset+1] == 0x89 and d[offset+2] == 0x5C and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], rsi
        if d[offset] == 0x48 and d[offset+1] == 0x89 and d[offset+2] == 0x74 and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], rcx
        if d[offset] == 0x48 and d[offset+1] == 0x89 and d[offset+2] == 0x4C and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], rdx
        if d[offset] == 0x48 and d[offset+1] == 0x89 and d[offset+2] == 0x54 and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], rbp
        if d[offset] == 0x48 and d[offset+1] == 0x89 and d[offset+2] == 0x6C and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], r8
        if d[offset] == 0x4C and d[offset+1] == 0x89 and d[offset+2] == 0x44 and d[offset+3] == 0x24:
            return True
        # mov [rsp+xx], r9
        if d[offset] == 0x4C and d[offset+1] == 0x89 and d[offset+2] == 0x4C and d[offset+3] == 0x24:
            return True
        # push rbx
        if d[offset] == 0x40 and d[offset+1] == 0x53:
            return True
        # push rbp
        if d[offset] == 0x40 and d[offset+1] == 0x55:
            return True
        # push rsi
        if d[offset] == 0x40 and d[offset+1] == 0x56:
            return True
        # push rdi
        if d[offset] == 0x40 and d[offset+1] == 0x57:
            return True
        # sub rsp, imm8
        if d[offset] == 0x48 and d[offset+1] == 0x83 and d[offset+2] == 0xEC:
            return True
        # sub rsp, imm32
        if d[offset] == 0x48 and d[offset+1] == 0x81 and d[offset+2] == 0xEC:
            return True
        # mov rbp, rsp
        if d[offset] == 0x48 and d[offset+1] == 0x8B and d[offset+2] == 0xEC:
            return True
        # push rbp; REX
        if d[offset] == 0x55 and d[offset+1] in (0x48, 0x8B):
            return True
        # push rbx; REX
        if d[offset] == 0x53 and d[offset+1] == 0x48:
            return True
        # push rsi; REX
        if d[offset] == 0x56 and d[offset+1] == 0x48:
            return True
        # push rdi; REX
        if d[offset] == 0x57 and d[offset+1] == 0x48:
            return True
        # push r12/r13/r14/r15
        if d[offset] == 0x41 and d[offset+1] in (0x54, 0x55, 0x56, 0x57):
            return True

        return False

    def make_smart_pattern(self, func_offset, length=32):
        """Create IDA-style pattern wildcarding displacement bytes."""
        if func_offset + length > len(self.data):
            length = len(self.data) - func_offset
        data = self.data[func_offset:func_offset + length]
        wildcards = set()

        i = 0
        while i < len(data):
            # mov [rsp+disp8], reg -> wildcard disp8
            if i + 4 < len(data) and data[i] in (0x48, 0x4C) and data[i+1] == 0x89:
                modrm = data[i+2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 1:
                    if rm == 4:
                        wildcards.add(i + 4)
                    else:
                        wildcards.add(i + 3)

            # sub rsp, imm8
            if i + 3 < len(data) and data[i] == 0x48 and data[i+1] == 0x83 and data[i+2] == 0xEC:
                wildcards.add(i + 3)

            # sub rsp, imm32
            if i + 6 < len(data) and data[i] == 0x48 and data[i+1] == 0x81 and data[i+2] == 0xEC:
                for j in range(3, 7):
                    wildcards.add(i + j)

            # CALL rel32
            if data[i] == 0xE8 and i + 4 < len(data):
                for j in range(1, 5):
                    wildcards.add(i + j)
                i += 5
                continue

            # JMP rel32
            if data[i] == 0xE9 and i + 4 < len(data):
                for j in range(1, 5):
                    wildcards.add(i + j)
                i += 5
                continue

            # LEA with RIP-relative
            if i + 6 < len(data) and data[i] in (0x48, 0x4C) and data[i+1] == 0x8D:
                modrm = data[i+2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    for j in range(3, 7):
                        wildcards.add(i + j)
                    i += 7
                    continue

            # MOV with RIP-relative
            if i + 6 < len(data) and data[i] in (0x48, 0x4C) and data[i+1] in (0x8B, 0x89):
                modrm = data[i+2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    for j in range(3, 7):
                        wildcards.add(i + j)
                    i += 7
                    continue

            i += 1

        parts = []
        for j, b in enumerate(data):
            if j in wildcards:
                parts.append('?')
            else:
                parts.append(f'{b:02X}')
        return ' '.join(parts)

    def find_all_functions_referencing_string(self, string_bytes, label=""):
        """Find ALL functions that reference a given string."""
        offsets = self.find_string(string_bytes)
        if not offsets:
            return []

        results = []
        seen_functions = set()

        for str_off in offsets:
            xrefs = self.find_string_xrefs(str_off)
            for xref in xrefs:
                func_start = self.find_function_start(xref)
                if func_start and func_start not in seen_functions:
                    seen_functions.add(func_start)
                    func_rva = self.offset_to_rva(func_start)
                    pattern = self.make_smart_pattern(func_start, 32)
                    raw = self.data[func_start:func_start + 32]
                    raw_hex = ' '.join(f'{b:02X}' for b in raw)
                    results.append({
                        'func_offset': func_start,
                        'func_rva': func_rva,
                        'xref_offset': xref,
                        'string_offset': str_off,
                        'distance': xref - func_start,
                        'pattern': pattern,
                        'raw': raw_hex,
                    })
        return results

    def scan_for_vtable_pattern(self, rva_list):
        """Look for a sequence of RVAs in .rdata (vtable detection)."""
        if len(rva_list) < 2:
            return []
        results = []
        target = b''
        for rva in rva_list:
            target += struct.pack('<Q', self.base + rva)

        idx = 0
        while True:
            pos = self.data.find(target, idx)
            if pos == -1:
                break
            results.append(pos)
            idx = pos + 1
        return results

    def read_pointer(self, offset):
        if offset + 8 <= len(self.data):
            return struct.unpack_from('<Q', self.data, offset)[0]
        return None

    def read_u32(self, offset):
        if offset + 4 <= len(self.data):
            return struct.unpack_from('<I', self.data, offset)[0]
        return None

    def read_i32(self, offset):
        if offset + 4 <= len(self.data):
            return struct.unpack_from('<i', self.data, offset)[0]
        return None

    def find_global_pointer_to_rva(self, target_rva):
        target_va = self.base + target_rva
        target_bytes = struct.pack('<Q', target_va)
        results = []
        idx = self.data_start
        end = self.data_start + self.data_size
        while idx < end:
            pos = self.data.find(target_bytes, idx, end)
            if pos == -1:
                break
            results.append(pos)
            idx = pos + 8
        return results

    # ── New v3: Structure offset discovery ──

    def extract_struct_offsets_from_function(self, func_offset, max_scan=512):
        """Scan a function body for MOV/MOVSS instructions that reveal struct field offsets.
        Returns dict of offset -> instruction description."""
        offsets_found = {}
        end = min(func_offset + max_scan, len(self.data) - 8)
        d = self.data

        i = func_offset
        while i < end:
            # Stop at RET
            if d[i] == 0xC3 or d[i] == 0xCB:
                break

            # MOV reg, [reg+disp32]: 48 8B xx [mod=10 rm!=4,5]
            if i + 6 < end and d[i] in (0x48, 0x4C) and d[i+1] in (0x8B, 0x89, 0x8D):
                modrm = d[i+2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                reg = (modrm >> 3) & 7
                if mod == 2 and rm != 4:  # [reg+disp32]
                    disp = struct.unpack_from('<i', d, i + 3)[0]
                    if 0 < disp < 0x1000:
                        opname = {0x8B: 'MOV', 0x89: 'MOV_STORE', 0x8D: 'LEA'}[d[i+1]]
                        offsets_found[disp] = f"{opname} r{reg}, [r{rm}+0x{disp:X}]"
                    i += 7
                    continue
                elif mod == 1 and rm != 4:  # [reg+disp8]
                    disp = d[i+3]
                    if 0 < disp < 0xFF:
                        opname = {0x8B: 'MOV', 0x89: 'MOV_STORE', 0x8D: 'LEA'}[d[i+1]]
                        offsets_found[disp] = f"{opname} r{reg}, [r{rm}+0x{disp:X}]"
                    i += 4
                    continue

            # MOVSS xmm, [reg+disp32]: F3 0F 10 xx
            if i + 7 < end and d[i] == 0xF3 and d[i+1] == 0x0F and d[i+2] == 0x10:
                modrm = d[i+3]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 2 and rm != 4:
                    disp = struct.unpack_from('<i', d, i + 4)[0]
                    if 0 < disp < 0x1000:
                        offsets_found[disp] = f"MOVSS xmm, [r{rm}+0x{disp:X}]"
                    i += 8
                    continue
                elif mod == 1 and rm != 4:
                    disp = d[i+4]
                    if 0 < disp < 0xFF:
                        offsets_found[disp] = f"MOVSS xmm, [r{rm}+0x{disp:X}]"
                    i += 5
                    continue

            # MOVSS [reg+disp32], xmm: F3 0F 11 xx
            if i + 7 < end and d[i] == 0xF3 and d[i+1] == 0x0F and d[i+2] == 0x11:
                modrm = d[i+3]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 2 and rm != 4:
                    disp = struct.unpack_from('<i', d, i + 4)[0]
                    if 0 < disp < 0x1000:
                        offsets_found[disp] = f"MOVSS_STORE [r{rm}+0x{disp:X}], xmm"
                    i += 8
                    continue

            i += 1

        return offsets_found

    def find_data_globals_near_xref(self, xref_offset, search_range=128):
        """Find RIP-relative references to .data section near a code xref."""
        globals_found = []
        scan_start = max(self.text_start, xref_offset - search_range)
        scan_end = min(self.text_start + self.text_size, xref_offset + search_range)

        for i in range(scan_start, scan_end - 6):
            b0 = self.data[i]
            b1 = self.data[i + 1]

            if b0 in (0x48, 0x4C) and b1 in (0x8B, 0x8D):
                modrm = self.data[i + 2]
                mod = (modrm >> 6) & 3
                rm = modrm & 7
                if mod == 0 and rm == 5:
                    disp32 = struct.unpack_from('<i', self.data, i + 3)[0]
                    instr_rva = self.offset_to_rva(i)
                    if instr_rva is None:
                        continue
                    target_rva = instr_rva + 7 + disp32
                    if self.data_rva <= target_rva < self.data_rva + self.data_size:
                        instr_type = "LEA" if b1 == 0x8D else "MOV"
                        globals_found.append({
                            'instr_offset': i,
                            'instr_rva': instr_rva,
                            'target_rva': target_rva,
                            'instr_type': instr_type,
                        })
        return globals_found

    def scan_vtable_in_rdata(self, known_func_rvas):
        """Scan .rdata for vtables containing known function RVAs."""
        if not known_func_rvas:
            return []
        results = []
        rdata = self.data[self.rdata_start:self.rdata_start + self.rdata_size]
        target_vas = set(self.base + rva for rva in known_func_rvas)

        # Scan for aligned pointers in .rdata
        for i in range(0, len(rdata) - 8, 8):
            val = struct.unpack_from('<Q', rdata, i)[0]
            if val in target_vas:
                vtable_start = self._find_vtable_start(rdata, i)
                if vtable_start is not None:
                    vtable_rva = self.rdata_rva + vtable_start
                    results.append({
                        'vtable_rva': vtable_rva,
                        'vtable_offset': self.rdata_start + vtable_start,
                        'matched_at_index': (i - vtable_start) // 8,
                        'matched_rva': val - self.base,
                    })
        return results

    def _find_vtable_start(self, rdata, ptr_offset):
        """Walk backwards from a matched pointer to find the start of the vtable."""
        # Vtables typically start after a zero or RTTI pointer
        i = ptr_offset
        while i >= 8:
            val = struct.unpack_from('<Q', rdata, i - 8)[0]
            # Check if previous pointer is in .text (valid function pointer)
            rva = val - self.base if val > self.base else 0
            if self.text_rva <= rva < self.text_rva + self.text_size:
                i -= 8
            else:
                break
        return i


def discover_struct_offsets(pe, patterns_found):
    """Phase 1b: Discover struct field offsets by analyzing function bodies."""
    offsets = {}

    print()
    print("=" * 70)
    print("PHASE 1b: Structure Offset Discovery")
    print("=" * 70)

    # setPosition function -> position offset
    if 'CHARACTER_SET_POSITION' in patterns_found:
        info = patterns_found['CHARACTER_SET_POSITION']
        func_offsets = pe.extract_struct_offsets_from_function(info['offset'])
        if func_offsets:
            # Position writes are typically MOVSS stores to consecutive floats
            float_stores = {k: v for k, v in func_offsets.items()
                          if 'MOVSS' in v or 'MOV_STORE' in v}
            # Look for 3 consecutive float offsets (x, y, z)
            sorted_offsets = sorted(float_stores.keys())
            for j in range(len(sorted_offsets) - 2):
                a, b, c = sorted_offsets[j], sorted_offsets[j+1], sorted_offsets[j+2]
                if b - a == 4 and c - b == 4:
                    offsets['position'] = a
                    print(f"  Position offset: 0x{a:X} (from setPosition float stores)")
                    break
            if 'position' not in offsets:
                # Fallback: first MOVSS store is likely position.x
                for off in sorted(float_stores.keys()):
                    offsets['position'] = off
                    print(f"  Position offset: 0x{off:X} (first MOVSS store in setPosition)")
                    break

            # Also check for rotation (4 consecutive floats after position)
            if 'position' in offsets:
                pos = offsets['position']
                # Rotation is typically at position + 12 or position + 16 (with padding)
                for candidate in [pos + 12, pos + 16, pos + 0x10]:
                    if candidate in func_offsets:
                        offsets['rotation'] = candidate
                        print(f"  Rotation offset: 0x{candidate:X} (near position)")
                        break

    # Health system: analyze blood loss / damage functions
    for label in ['HEALTH_UPDATE', 'APPLY_DAMAGE']:
        if label in patterns_found:
            info = patterns_found[label]
            func_offsets = pe.extract_struct_offsets_from_function(info['offset'])
            if func_offsets:
                # Health chains typically involve multiple pointer dereferences
                # Look for characteristic offsets from CE chain: +2B8, +5F8, +40
                for off in sorted(func_offsets.keys()):
                    if off == 0x2B8:
                        offsets['health_chain_1'] = off
                        print(f"  Health chain step 1: 0x{off:X}")
                    elif off == 0x5F8:
                        offsets['health_chain_2'] = off
                        print(f"  Health chain step 2: 0x{off:X}")
                    elif off == 0x40 and 'health_chain_1' in offsets:
                        offsets['health_base'] = off
                        print(f"  Health base offset: 0x{off:X}")

                # Print all offsets found for analysis
                print(f"  [{label}] All struct offsets in function:")
                for off in sorted(func_offsets.keys())[:20]:
                    print(f"    +0x{off:X}: {func_offsets[off]}")

    # Character creation -> template ID, faction offsets
    if 'CHARACTER_CREATE' in patterns_found:
        info = patterns_found['CHARACTER_CREATE']
        func_offsets = pe.extract_struct_offsets_from_function(info['offset'])
        if func_offsets:
            print(f"  [CHARACTER_CREATE] Offsets found in function:")
            for off in sorted(func_offsets.keys())[:20]:
                print(f"    +0x{off:X}: {func_offsets[off]}")
            # Name is typically one of the early pointer loads
            # Faction is typically a pointer at a medium offset
            pointer_loads = {k: v for k, v in func_offsets.items() if 'MOV' in v and 'STORE' not in v}
            if pointer_loads:
                sorted_ptr = sorted(pointer_loads.keys())
                # Heuristic: name is often at +0x10 or +0x18 (MSVC std::string)
                for candidate in [0x10, 0x18, 0x20, 0x28, 0x30]:
                    if candidate in pointer_loads:
                        offsets['name'] = candidate
                        print(f"  Name offset (heuristic): 0x{candidate:X}")
                        break

    # Squad creation -> squad member list offset
    if 'SQUAD_CREATE' in patterns_found:
        info = patterns_found['SQUAD_CREATE']
        func_offsets = pe.extract_struct_offsets_from_function(info['offset'])
        if func_offsets:
            print(f"  [SQUAD_CREATE] Offsets found:")
            for off in sorted(func_offsets.keys())[:15]:
                print(f"    +0x{off:X}: {func_offsets[off]}")

    # Inventory system
    for label in ['ITEM_PICKUP', 'INVENTORY_TRANSFER']:
        if label in patterns_found:
            info = patterns_found[label]
            func_offsets = pe.extract_struct_offsets_from_function(info['offset'])
            if func_offsets:
                print(f"  [{label}] Offsets found:")
                for off in sorted(func_offsets.keys())[:15]:
                    print(f"    +0x{off:X}: {func_offsets[off]}")

    # Apply CE-known fallback offsets for anything not discovered
    ce_fallbacks = {
        'health_chain_1': 0x2B8,
        'health_chain_2': 0x5F8,
        'health_base': 0x40,
        'position': 0x0A0,
        'rotation': 0x0B0,
        'name': 0x10,
        'faction': 0x50,
        'inventory': 0x200,
        'stats': 0x300,
        'equipment': 0x380,
        'scene_node': 0x100,
        'ai_package': 0x1A0,
        'current_task': 0x400,
        'is_alive': 0x408,
        'is_player_controlled': 0x410,
        'squad_name': 0x10,
        'squad_member_list': 0x28,
        'squad_member_count': 0x30,
        'squad_faction_id': 0x38,
        'squad_is_player': 0x40,
        'world_time_of_day': 0x48,
        'world_game_speed': 0x50,
        'world_weather': 0x58,
        'world_character_list': 0x60,
        'world_building_list': 0x68,
        'world_zone_manager': 0x70,
    }

    for key, val in ce_fallbacks.items():
        if key not in offsets:
            offsets[key] = val
            print(f"  {key}: 0x{val:X} (CE fallback)")

    return offsets


def discover_vtables(pe, patterns_found):
    """Phase 1c: Scan for vtables containing known function RVAs."""
    vtables = {}

    print()
    print("=" * 70)
    print("PHASE 1c: Vtable Discovery")
    print("=" * 70)

    # Collect known function RVAs
    known_rvas = []
    for label, info in patterns_found.items():
        if info.get('rva'):
            known_rvas.append(info['rva'])

    if known_rvas:
        vtable_hits = pe.scan_vtable_in_rdata(known_rvas)
        for hit in vtable_hits:
            print(f"  Vtable at RVA 0x{hit['vtable_rva']:08X}, "
                  f"matched func RVA 0x{hit['matched_rva']:08X} at index {hit['matched_at_index']}")
            vtables[f"vtable_0x{hit['vtable_rva']:08X}"] = {
                'rva': hit['vtable_rva'],
                'offset': hit['vtable_offset'],
                'matched_func': hit['matched_rva'],
                'index': hit['matched_at_index'],
            }

    # Search for Ogre FrameListener vtable via string xrefs
    for s in [b"frameRenderingQueued", b"frameStarted", b"frameEnded"]:
        str_offsets = pe.find_string(s)
        if str_offsets:
            print(f"  Ogre string \"{s.decode()}\": found at {len(str_offsets)} location(s)")
            for str_off in str_offsets[:2]:
                xrefs = pe.find_string_xrefs(str_off)
                for xref in xrefs[:3]:
                    func = pe.find_function_start(xref)
                    if func:
                        func_rva = pe.offset_to_rva(func)
                        print(f"    -> Function at RVA 0x{func_rva:08X}")

    return vtables


def discover_singletons(pe, patterns_found):
    """Phase 1d: Find global singleton pointers in .data section."""
    singletons = {}

    print()
    print("=" * 70)
    print("PHASE 1d: Global Singleton Discovery")
    print("=" * 70)

    singleton_strings = [
        (b"Kenshi 1.0.", "GAME_VERSION"),
        (b"zone.%d.%d.zone", "ZONE_MANAGER"),
        (b"setPosition moved", "CHARACTER_SYSTEM"),
        (b"game speed", "GAME_SPEED"),
        (b"GameWorld", "GAME_WORLD"),
        (b"timeOfDay", "TIME_MANAGER"),
        (b"platoon", "SQUAD_MANAGER"),
    ]

    for target_str, ptr_name in singleton_strings:
        str_offsets = pe.find_string(target_str)
        if not str_offsets:
            continue

        for str_off in str_offsets[:1]:
            xrefs = pe.find_string_xrefs(str_off)
            if not xrefs:
                continue

            for xref in xrefs[:3]:
                globals_found = pe.find_data_globals_near_xref(xref)
                if globals_found:
                    g = globals_found[0]
                    target_file_off = pe.rva_to_offset(g['target_rva'])
                    val = pe.read_pointer(target_file_off) if target_file_off else None
                    singletons[ptr_name] = {
                        'rva': g['target_rva'],
                        'instr_rva': g['instr_rva'],
                        'type': g['instr_type'],
                    }
                    val_str = f"0x{val:016X}" if val else "N/A"
                    print(f"  [{ptr_name}] {g['instr_type']} at RVA 0x{g['instr_rva']:08X} -> "
                          f".data RVA 0x{g['target_rva']:08X} (static value: {val_str})")
                    break
            break

    return singletons


def generate_cpp_output(patterns_found, offsets, singletons, vtables):
    """Generate C++ code blocks for patterns.h and game_types.h."""
    output_lines = []

    output_lines.append("")
    output_lines.append("=" * 70)
    output_lines.append("C++ PATTERNS.H UPDATE")
    output_lines.append("=" * 70)
    output_lines.append("")
    output_lines.append("// Copy into kmp::patterns namespace in patterns.h:")
    output_lines.append("namespace kmp { namespace patterns {")
    output_lines.append("")

    # Map pattern labels to C++ constant names
    pattern_mapping = {
        'CHARACTER_CREATE': 'CHARACTER_SPAWN',
        'CHARACTER_DESTROY': 'CHARACTER_DESTROY',
        'CHARACTER_SET_POSITION': 'CHARACTER_SET_POSITION',
        'CHARACTER_MOVE_TO': 'CHARACTER_MOVE_TO',
        'CHARACTER_GET_POSITION': 'CHARACTER_GET_POSITION',
        'APPLY_DAMAGE': 'APPLY_DAMAGE',
        'COMBAT_ATTACK': 'START_ATTACK',
        'CHARACTER_DEATH': 'CHARACTER_DEATH',
        'ZONE_LOAD': 'ZONE_LOAD',
        'ZONE_UNLOAD': 'ZONE_UNLOAD',
        'BUILDING_PLACE': 'BUILDING_PLACE',
        'FRAME_LISTENER': 'GAME_FRAME_UPDATE',
        'TIME_UPDATE': 'TIME_UPDATE',
        'SAVE_GAME': 'SAVE_GAME',
        'LOAD_GAME': 'LOAD_GAME',
        'HEALTH_UPDATE': 'HEALTH_UPDATE',
        'SQUAD_CREATE': 'SQUAD_CREATE',
        'SQUAD_ADD_MEMBER': 'SQUAD_ADD_MEMBER',
        'ITEM_PICKUP': 'ITEM_PICKUP',
        'ITEM_DROP': 'ITEM_DROP',
    }

    for scan_label, cpp_name in sorted(pattern_mapping.items()):
        if scan_label in patterns_found:
            info = patterns_found[scan_label]
            output_lines.append(f'// {info.get("description", scan_label)}')
            output_lines.append(f'// Found via string: "{info["string"]}"')
            output_lines.append(f'constexpr const char* {cpp_name} = "{info["pattern"]}";')
            output_lines.append(f'// RVA: 0x{info["rva"]:08X}')
            output_lines.append("")
        else:
            output_lines.append(f'constexpr const char* {cpp_name} = nullptr; // Not found by scanner')
            output_lines.append("")

    output_lines.append("}} // namespace kmp::patterns")
    output_lines.append("")

    # Offsets for game_types.h
    output_lines.append("=" * 70)
    output_lines.append("C++ GAME_TYPES.H OFFSET UPDATE")
    output_lines.append("=" * 70)
    output_lines.append("")
    output_lines.append("// Copy into GetOffsets() initialization:")
    output_lines.append("")

    char_offset_mapping = {
        'name': 'name', 'faction': 'faction', 'position': 'position',
        'rotation': 'rotation', 'scene_node': 'sceneNode',
        'ai_package': 'aiPackage', 'inventory': 'inventory',
        'stats': 'stats', 'equipment': 'equipment',
        'current_task': 'currentTask', 'is_alive': 'isAlive',
        'is_player_controlled': 'isPlayerControlled',
    }
    output_lines.append("// CharacterOffsets:")
    for scan_key, cpp_field in char_offset_mapping.items():
        if scan_key in offsets:
            output_lines.append(f"offsets.character.{cpp_field} = 0x{offsets[scan_key]:X};")

    squad_offset_mapping = {
        'squad_name': 'name', 'squad_member_list': 'memberList',
        'squad_member_count': 'memberCount', 'squad_faction_id': 'factionId',
        'squad_is_player': 'isPlayerSquad',
    }
    output_lines.append("")
    output_lines.append("// SquadOffsets:")
    for scan_key, cpp_field in squad_offset_mapping.items():
        if scan_key in offsets:
            output_lines.append(f"offsets.squad.{cpp_field} = 0x{offsets[scan_key]:X};")

    world_offset_mapping = {
        'world_time_of_day': 'timeOfDay', 'world_game_speed': 'gameSpeed',
        'world_weather': 'weatherState', 'world_zone_manager': 'zoneManager',
        'world_character_list': 'characterList', 'world_building_list': 'buildingList',
    }
    output_lines.append("")
    output_lines.append("// WorldOffsets:")
    for scan_key, cpp_field in world_offset_mapping.items():
        if scan_key in offsets:
            output_lines.append(f"offsets.world.{cpp_field} = 0x{offsets[scan_key]:X};")

    return '\n'.join(output_lines)


def main():
    exe_path = sys.argv[1] if len(sys.argv) > 1 else \
        r"C:\Program Files (x86)\Steam\steamapps\common\Kenshi\kenshi_x64.exe"

    if not os.path.exists(exe_path):
        print(f"ERROR: File not found: {exe_path}")
        sys.exit(1)

    print(f"Analyzing: {exe_path}")
    print(f"Size: {os.path.getsize(exe_path):,} bytes")
    print()

    pe = PEParser(exe_path)

    print(f"Image base: 0x{pe.base:X}")
    print(f"Sections:")
    for s in pe.sections:
        print(f"  {s['name']:8s}  RVA: 0x{s['rva']:08X}  Size: 0x{s['raw_size']:08X}  "
              f"Raw: 0x{s['raw_offset']:08X}")
    print()

    # ═══════════════════════════════════════════════════════
    # PHASE 1a: Targeted Function Discovery
    # ═══════════════════════════════════════════════════════

    patterns_found = {}

    targets = [
        ("ZONE_LOAD", [
            b"zone.%d.%d.zone", b"zone.%d.%d",
        ], "Zone loading function"),

        ("ZONE_UNLOAD", [
            b"ZONE UNLOADED", b"zone unloaded", b"UnloadZone",
        ], "Zone unloading"),

        ("CHARACTER_CREATE", [
            b"Creating character", b"CHARACTER CREATE", b"createCharacter",
            b"creating character", b"new character",
        ], "Character creation/spawn"),

        ("CHARACTER_DESTROY", [
            b"Removing character", b"removeCharacter", b"CHARACTER DESTROY",
            b"destroying character", b"deleteCharacter",
        ], "Character destruction/despawn"),

        ("CHARACTER_SET_POSITION", [
            b"setPosition moved someone off the navmesh",
            b"setPosition moved someone off",
        ], "Character position setter"),

        ("CHARACTER_GET_POSITION", [
            b"getPosition", b"GetPosition",
        ], "Character position getter"),

        ("CHARACTER_MOVE_TO", [
            b"move task", b"TASK_MOVE", b"move to position", b"moveTo(",
            b"pathfind", b"pathFind", b"path_find", b"findPath", b"PATHFINDING",
        ], "Character movement/pathfinding"),

        ("APPLY_DAMAGE", [
            b"applyDamage", b"apply damage", b"APPLY_DAMAGE", b"ApplyDamage",
            b"damage dealt", b"damageDealt", b"inflict damage",
        ], "Damage application"),

        ("COMBAT_ATTACK", [
            b"attack animation", b"attackAnim", b"combat attack", b"meleeAttack",
            b"melee attack", b"startAttack", b"doAttack",
        ], "Combat attack initiation"),

        ("CHARACTER_DEATH", [
            b"Character died", b"character died", b"onDeath",
            b"death event", b"isDead",
        ], "Character death handler"),

        ("CHARACTER_KO", [
            b"knocked out", b"knockout", b"unconscious",
            b"KO_DURATION", b"knocked unconscious",
        ], "Character knockout"),

        ("HEALTH_UPDATE", [
            b"blood loss", b"bloodLoss", b"blood_loss",
            b"hunger damage", b"starvation",
        ], "Health/blood system"),

        ("SQUAD_CREATE", [
            b"Creating squad", b"creating squad", b"createSquad",
            b"new squad", b"SQUAD CREATE",
        ], "Squad creation"),

        ("SQUAD_ADD_MEMBER", [
            b"addToSquad", b"add to squad", b"joinSquad", b"squad member",
        ], "Squad membership"),

        ("SAVE_GAME", [
            b"Saving game", b"saving game", b"QUICKSAVE",
            b"quicksave", b"SaveGame", b"saveGame",
        ], "Game save function"),

        ("LOAD_GAME", [
            b"Loading game", b"loading game", b"LoadGame",
            b"loadGame", b"LOAD_GAME",
        ], "Game load function"),

        ("BUILDING_PLACE", [
            b"placeBuilding", b"place building", b"PlaceBuilding",
            b"building placed", b"buildingPlaced", b"constructBuilding",
        ], "Building placement"),

        ("TIME_UPDATE", [
            b"timeOfDay", b"time_of_day", b"dayNightCycle",
            b"updateTime", b"game time", b"gameTime", b"worldTime",
        ], "Time of day update"),

        ("GAME_SPEED", [
            b"game speed", b"gameSpeed", b"setGameSpeed", b"speedMultiplier",
        ], "Game speed control"),

        ("ITEM_PICKUP", [
            b"pickupItem", b"pickup item", b"item picked up",
            b"itemPickup", b"pick up item",
        ], "Item pickup"),

        ("ITEM_DROP", [
            b"dropItem", b"drop item", b"item dropped", b"itemDrop",
        ], "Item drop"),

        ("INVENTORY_TRANSFER", [
            b"transferItem", b"transfer item", b"inventoryTransfer", b"moveItem",
        ], "Inventory transfer"),

        ("FRAME_LISTENER", [
            b"frameStarted", b"frameRenderingQueued",
        ], "Ogre frame listener"),

        ("GAME_INIT", [
            b"Kenshi 1.0.",
        ], "Game initialization"),

        ("AI_TASK_UPDATE", [
            b"CHARACTER_STEP", b"TaskAI", b"updateAI", b"aiUpdate",
        ], "AI task update loop"),
    ]

    print("=" * 70)
    print("PHASE 1a: Targeted Function Discovery")
    print("=" * 70)

    for label, strings, desc in targets:
        if label in patterns_found:
            continue

        found_any = False
        for s in strings:
            results = pe.find_all_functions_referencing_string(s, label)
            if results:
                results.sort(key=lambda r: r['distance'])
                best = results[0]

                patterns_found[label] = {
                    'pattern': best['pattern'],
                    'rva': best['func_rva'],
                    'offset': best['func_offset'],
                    'string': s.decode('ascii', errors='replace'),
                    'raw': best['raw'],
                    'xref_distance': best['distance'],
                    'description': desc,
                    'all_functions': len(results),
                }

                rva_hex = f"0x{best['func_rva']:08X}"
                print(f"  [{label}] FOUND at RVA {rva_hex} via \"{s.decode('ascii', errors='replace')}\" "
                      f"({len(results)} function(s))")
                found_any = True
                break

        if not found_any:
            print(f"  [{label}] not found")

    # ═══════════════════════════════════════════════════════
    # PHASE 1b: Structure Offset Discovery
    # ═══════════════════════════════════════════════════════

    offsets = discover_struct_offsets(pe, patterns_found)

    # ═══════════════════════════════════════════════════════
    # PHASE 1c: Vtable Discovery
    # ═══════════════════════════════════════════════════════

    vtables = discover_vtables(pe, patterns_found)

    # ═══════════════════════════════════════════════════════
    # PHASE 1d: Singleton Discovery
    # ═══════════════════════════════════════════════════════

    singletons = discover_singletons(pe, patterns_found)

    # ═══════════════════════════════════════════════════════
    # PHASE 2: Known Offset Validation
    # ═══════════════════════════════════════════════════════

    print()
    print("=" * 70)
    print("PHASE 2: Known Offset Validation")
    print("=" * 70)

    known_offsets_1052 = {
        'PLAYER_BASE_PTR': 0x01AC8A90,
    }

    for name, rva in known_offsets_1052.items():
        file_off = pe.rva_to_offset(rva)
        if file_off is not None and file_off < len(pe.data):
            val = pe.read_pointer(file_off)
            print(f"  [{name}] RVA 0x{rva:08X} -> file offset 0x{file_off:08X}, "
                  f"value: 0x{val:016X}" if val else
                  f"  [{name}] RVA 0x{rva:08X} -> file offset 0x{file_off:08X}, could not read")
        else:
            print(f"  [{name}] RVA 0x{rva:08X} -> NOT in any section (version mismatch?)")

    # ═══════════════════════════════════════════════════════
    # PHASE 3: Additional Pattern Mining
    # ═══════════════════════════════════════════════════════

    print()
    print("=" * 70)
    print("PHASE 3: Additional Pattern Mining")
    print("=" * 70)

    fallback_strings = {
        'ENTITY_UPDATE': [b"entity update", b"updateEntity", b"EntityUpdate",
                         b"entityTick", b"tickEntity"],
        'NAVMESH': [b"navmesh", b"NavMesh", b"navigation mesh", b"detour"],
        'ANIMATION': [b"playAnimation", b"setAnimation", b"animState",
                     b"AnimationState", b"skeleton"],
        'EQUIPMENT': [b"equip item", b"equipItem", b"equipment slot",
                     b"equipmentSlot", b"wearItem"],
    }

    for label, strings in fallback_strings.items():
        if label in patterns_found:
            continue
        for s in strings:
            results = pe.find_all_functions_referencing_string(s, label)
            if results:
                results.sort(key=lambda r: r['distance'])
                best = results[0]
                patterns_found[label] = {
                    'pattern': best['pattern'],
                    'rva': best['func_rva'],
                    'offset': best['func_offset'],
                    'string': s.decode('ascii', errors='replace'),
                    'raw': best['raw'],
                    'xref_distance': best['distance'],
                    'description': f'{label} system',
                    'all_functions': len(results),
                }
                print(f"  [{label}] FOUND at RVA 0x{best['func_rva']:08X} "
                      f"via \"{s.decode('ascii', errors='replace')}\"")
                break
        else:
            print(f"  [{label}] not found")

    # ═══════════════════════════════════════════════════════
    # PHASE 4: Game Structure Discovery
    # ═══════════════════════════════════════════════════════

    print()
    print("=" * 70)
    print("PHASE 4: Game Structure Discovery")
    print("=" * 70)

    version_offsets = pe.find_string(b"Kenshi 1.0.")
    if version_offsets:
        str_off = version_offsets[0]
        ver_end = pe.data.find(b'\x00', str_off)
        ver_str = pe.data[str_off:ver_end].decode('ascii', errors='replace')
        print(f"  Game version: \"{ver_str}\"")

    for category, strings in [
        ("Ogre", [b"dllStartPlugin", b"Plugins_x64.cfg", b"RenderSystem_Direct3D11"]),
        ("D3D11", [b"d3d11.dll", b"dxgi.dll", b"D3D11CreateDevice"]),
        ("OIS", [b"OIS::Keyboard", b"OIS::Mouse", b"keyPressed"]),
        ("FCS", [b".base", b"GAME_DATA", b"gamedata"]),
    ]:
        for s in strings:
            found = pe.find_string(s)
            if found:
                print(f"  {category} string \"{s.decode()}\": {len(found)} occurrence(s)")

    # ═══════════════════════════════════════════════════════
    # OUTPUT
    # ═══════════════════════════════════════════════════════

    print()
    print("=" * 70)
    print(f"RESULTS: {len(patterns_found)} Patterns Discovered")
    print("=" * 70)

    for name, info in sorted(patterns_found.items()):
        print(f'\n  {name}:')
        print(f'    RVA: 0x{info["rva"]:08X}')
        print(f'    Pattern: {info["pattern"]}')
        print(f'    String:  "{info["string"]}"')

    # Save patterns.json
    output_dir = os.path.join(os.path.dirname(exe_path), 'KenshiMP', 'docs')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'patterns.json')

    serializable = {}
    for name, info in sorted(patterns_found.items()):
        serializable[name] = {
            'pattern': info['pattern'],
            'rva': f'0x{info["rva"]:08X}',
            'offset': f'0x{info["offset"]:08X}',
            'string': info['string'],
            'raw_bytes': info['raw'],
            'description': info['description'],
        }

    with open(output_path, 'w') as f:
        json.dump(serializable, f, indent=2)
    print(f"\nPatterns saved to: {output_path}")

    # Save offsets.json (NEW in v3)
    offsets_path = os.path.join(output_dir, 'offsets.json')
    offsets_output = {
        'struct_offsets': {k: f'0x{v:X}' for k, v in offsets.items()},
        'singletons': {k: {kk: (f'0x{vv:X}' if isinstance(vv, int) else vv)
                          for kk, vv in v.items()}
                      for k, v in singletons.items()},
        'vtables': {k: {kk: (f'0x{vv:X}' if isinstance(vv, int) else vv)
                       for kk, vv in v.items()}
                   for k, v in vtables.items()},
    }
    with open(offsets_path, 'w') as f:
        json.dump(offsets_output, f, indent=2)
    print(f"Offsets saved to: {offsets_path}")

    # Generate C++ output
    cpp_output = generate_cpp_output(patterns_found, offsets, singletons, vtables)
    print(cpp_output)

    print(f"\nTotal patterns: {len(patterns_found)}")
    print(f"Total struct offsets: {len(offsets)}")
    print(f"Total singletons: {len(singletons)}")
    print(f"Total vtables: {len(vtables)}")

    return patterns_found, offsets, singletons, vtables


if __name__ == '__main__':
    main()
