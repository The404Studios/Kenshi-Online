#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Engine — Master include for the engine abstraction layer
// ═══════════════════════════════════════════════════════════════════════════
// Include this single header to get the full engine pipeline infrastructure.
// Each header is self-contained — include only what you need for faster builds.
//
// Architecture Overview:
//
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │                        Engine Pipeline                               │
//   │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ │
//   │  │FrameBegin│→│NetRecv   │→│ZoneUpdate│→│SpawnProc │→│RemoteApply│ │
//   │  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ │
//   │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ │
//   │  │LocalPoll │→│CombatRes │→│InvSync   │→│WorldState│→│NetSend   │ │
//   │  └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ │
//   │  ┌──────────┐ ┌──────────┐ ┌──────────┐                           │
//   │  │UIUpdate  │→│Diag      │→│FrameEnd  │                           │
//   │  └──────────┘ └──────────┘ └──────────┘                           │
//   └──────────────────────────────────────────────────────────────────────┘
//            ↑ publishes / subscribes ↓
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │                          Event Bus                                   │
//   │  EntitySpawned, EntityMoved, DamageDealt, ItemPickedUp, ...         │
//   └──────────────────────────────────────────────────────────────────────┘
//            ↑ registers / queries ↓
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │                        Engine Context                                │
//   │  Registry, Interpolation, Client, SpawnMgr, PlayerCtrl, ...         │
//   └──────────────────────────────────────────────────────────────────────┘
//            ↑ uses ↓
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │                       Foundation Layer                               │
//   │  assert.h  result.h  safe_memory.h  diagnostics.h  types.h         │
//   └──────────────────────────────────────────────────────────────────────┘
//            ↑ reads/writes ↓
//   ┌──────────────────────────────────────────────────────────────────────┐
//   │                     Kenshi Game Process                              │
//   │  Characters, Squads, Buildings, Inventory, AI, World State          │
//   └──────────────────────────────────────────────────────────────────────┘

// Foundation (Common library — available to all projects)
#include "kmp/assert.h"
#include "kmp/result.h"
#include "kmp/safe_memory.h"
#include "kmp/diagnostics.h"

// Engine components (Core library — game-side only)
#include "engine/engine_context.h"
#include "engine/engine_pipeline.h"
#include "engine/engine_events.h"
#include "engine/engine_system.h"
#include "engine/engine_bootstrap.h"
