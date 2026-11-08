# Hook Inventory — Random Set Piece

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Random Set Piece |
| Plugin ID | random-set-piece |
| Release version | 0.4.5 |
| Inventory updated | 2026-11-08 |
| Source revision | v0.4.5 release tag |
| D2R build(s) audited | 3.3.93847 (runtime retest after SDK migration pending) |
| D2RLoader SDK / Plugin ABI | PluginSDK 0.4.0 / Plugin ABI 4 |
| Audit status | Complete |

## Scope and method

Inspected all plugin source and distribution files: `CMakeLists.txt`, `src/plugin.cpp`, `src/plugin.rc`, `config/random-set-piece.toml`, `README.md`, and `LICENSE`. Searched for native detour, CALL/JMP redirect, byte patch, vtable/shared-slot replacement, MinHook, and Win32 input/message hook installation. The plugin registers an ordinary D2RLoader shared UI-message listener and uses the loader's inventory, item, and game-thread services; it does not install a native or operating-system hook target.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

None identified in the current inventory; this is not a compatibility guarantee.

## Audit notes

No native hook-site changes from v0.4.4. This release migrates the SDK ABI field names and manifest constant, updates version/author metadata, and uses optimized Release build flags. Runtime behavior has not been retested in-game after the SDK migration.
