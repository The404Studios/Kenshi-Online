# Kenshi Character Class Analysis (v1.0.68)

## Known Struct Layout

```
+0x00  void*        vtable
+0x08  void*        unknown (squad candidate - probed at +0x08/0x20/0x28/0x30/0x38)
+0x10  Faction*     faction (KServerMod verified)
+0x18  std::string  name (MSVC SSO: 16-byte buf, +0x28=size, +0x30=capacity)
+0x40  GameData*    gameDataPtr (template backpointer, KServerMod verified)
+0x48  Vec3         position (cached read-only, KServerMod verified)
+0x58  Quat         rotation (w,x,y,z floats, KServerMod verified)
...gap...
+0x298 void*        moneyChainPtr1 -> +0x78 -> +0x88 = money(int) (CE verified)
+0x2B8 void*        healthChainPtr1 -> +0x5F8 -> +0x40 = health(float) (CE verified)
+0x2E8 Inventory*   inventory (KServerMod verified)
+0x2F0..0x440       equipment[14] Item* array (runtime-probed range)
+0x450 Stats        inline stats (KServerMod verified) - at least 0x64 bytes
```

## Health Chain Detail
- `char+0x2B8` -> ptr1
- `ptr1+0x5F8` -> ptr2
- `ptr2+0x40 + bodyPart*8` -> health float
- Body parts: Head(0), Chest(1), Stomach(2), LArm(3), RArm(4), LLeg(5), RLeg(6)
- Stride 8: health float + stun float per part

## Writable Position Chain
- `char + animClassOffset` (probed 0x60-0x200) -> AnimationClassHuman*
- `AnimClass + 0xC0` (verified) -> CharMovement*
- `CharMovement + 0x320 + 0x20` -> writable Vec3

## Key Functions (all RVAs, __fastcall)
- CharacterSpawn: 0x00581770 - `void*(factory, requestStruct)` - 6410 bytes
- CharacterSerialise: 0x006280A0 - `void(character, stream)`
- HavokCharacter::setPosition: 0x00145E50 - `void(havokChar, Vec3*)`
- CharacterMoveTo: 0x002EF4E3 - `void(char, x, y, z, moveType)` (mid-func)
- ApplyDamage: 0x007A33A0 - `void(target, attacker, bodyPart, cut, blunt, pierce)`
- CharacterDeath: 0x007A6200 - `void(character, killer)`
- CharacterKO: 0x00345C10 - `void(character, attacker, reason)`
- AI::create: 0x00622110 - `void*(character, faction)`
- SquadAddMember: 0x00928423 - `void(squad, character)` (mid-func)

## Unknown/Missing
- Character class actual RTTI name (runtime VTableScanner needed)
- Character class total size
- Vtable slot indices (no virtual function mapping done)
- animClassOffset (runtime-probed, no static value)
- squad offset (heuristic probe only)
- isPlayerControlled offset (diff-scan only)
- currentTask, moveSpeed, animState offsets (all -1)
- sceneNode, aiPackage, isAlive offsets (all -1)
- Proper Character destructor (CharacterDestroy pattern is actually NodeList::destroyNodesByBuilding)
- Identity of intermediate objects at +0x298 and +0x2B8

## Investigation Priorities
1. Disassemble Character::serialise (0x006280A0) - reveals ALL serialized field offsets
2. Run VTableScanner with Character/Havok/Animation queries
3. Find real destructor via RTTI vtable slot 0
4. Disassemble AI::create (0x00622110) for aiPackage offset
5. Disassemble HavokCharacter::setPosition for physics<->game char relationship
