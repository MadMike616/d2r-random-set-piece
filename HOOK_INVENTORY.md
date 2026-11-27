# Hook Inventory — Random Set Piece

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Random Set Piece |
| Plugin ID | random-set-piece |
| Release version | 1.4.6 |
| Inventory updated | 2026-11-27 |
| Source revision | v1.4.6 release source |
| D2R build(s) audited | Not validated in game for 1.4.5 |
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

No native hook-site changes from v0.4.5. The new file setup uses a Windows mutex to coordinate with other installers; it does not install an OS hook.

Release 1.4.6 source review: TOML installation completion and configuration mirroring add no native hook, patch, vtable, or OS input-hook targets. Hook-site changes since the previous release: none. Startup completion was checked in the testing environment; the existing native validation limits above still apply.
