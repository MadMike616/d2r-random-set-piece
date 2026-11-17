# Random Set Piece

Random Set Piece adds a Horadric Cube recipe to D2RLoader:

**Author:** MadMike · **Source version:** 1.4.5

~~~text
1 set item + optional ingredient X + optional ingredient Y
    -> 1 different item from the same set
~~~

## Compatibility

Uses D2RLoader PluginSDK 0.4.0 and Plugin ABI 4. Earlier versions were tested with Diablo II: Resurrected 3.3.93847.

## Installation and game tables

The automatic table setup below is available in the 1.4.5 source build. See [GitHub Releases](https://github.com/MadMike616/d2r-random-set-piece/releases) for published binaries.

Place `d2rl-random-set-piece.dll` in the active mod's `d2rloader/plugins/` folder. The plugin needs `sets.txt` and `setitems.txt`. With the default configuration, it reads the unpacked mod's loose `data/global/excel/` folder first, including the folder inside `<ModName>.mpq/` when the `.mpq` is an unpacked directory.

If either table is missing, use **Ladik's Casc Viewer** to extract the original from the game data version used by your mod. Place it in `mods/<ModName>/d2rloader/plugins/basefiles-and-backups/basefiles/global/excel/`. On startup, the plugin copies any missing tables it finds there into the unpacked mod's loose data folder. The copy uses the shared mod-install lock. It preserves all existing loose files, so there is nothing to back up. If the plugin adds a table, fully exit and restart D2RLoader with the same mod to load it. The recipe stays disabled until both tables can be read.

## Recipe behavior

The plugin reads set membership and item codes from `setitems.txt`, then checks the set names against `sets.txt`. It handles the Cube's native **Convert** action when the Cube contains exactly the configured recipe.

The output is chosen uniformly from the other enabled rows in the same set, excluding rows with the source item's base code. The plugin creates the selected set row and consumes the inputs as one atomic transaction. It carries over the source item's item level and identified state.

If the source item has sockets, or the runtime rejects every eligible output, the transaction rolls back and leaves the inputs untouched. When a candidate is rejected as unsupported, the plugin tries the remaining candidates in random order.

You can configure a chance for the recipe to fail. A failed roll consumes any configured ingredients but keeps the original set item. If no ingredients are configured, the item remains unchanged on failure.

## Configuration

On first load, D2RLoader creates `d2rloader/config/random-set-piece.toml`. The defaults use El (`r01`) and Eld (`r02`) runes:

~~~toml
[recipe]
input_x = "r01"
input_y = "r02"
failure_chance_percent = 0
require_identified_set_item = false

[tables]
table_directory = ""
~~~

`input_x` and `input_y` accept optional 3- or 4-character D2 item codes. Blank either field to omit that ingredient; blank both to require only the set item. For example, `input_x = ""` and `input_y = "r02"` requires one set item and one Eld rune. Using the same code in both fields requires two copies.

`failure_chance_percent` is an integer from 0 to 100. The default, 0, disables recipe failure.

`require_identified_set_item` defaults to `false`. Set it to `true` to require the set item to be identified before the recipe can match. An unidentified set item is left untouched, and the configured ingredients are not consumed.

`table_directory` can point to a folder containing both loose tables. A nonblank value is a read-only override and skips automatic copying. If your mod uses a packed MPQ archive, unpack the same tables the mod uses into a separate folder and set `table_directory` to that folder.

## Build

Building requires Windows x64, CMake 3.29 or newer, Visual Studio 2022 or newer with the Windows SDK, and D2RLoader PluginSDK 0.4.0 (Plugin ABI 4).

To build offline, provide a local PluginSDK checkout with `RANDOM_SET_PIECE_LOCAL_SDK_DIR`:

~~~powershell
cmake -S . -B ..\random-set-piece-build -A x64 `
  -DRANDOM_SET_PIECE_LOCAL_SDK_DIR="<PluginSDK checkout>"
cmake --build ..\random-set-piece-build --config Release --target random_set_piece
~~~

The DLL is created at `../random-set-piece-build/Release/d2rl-random-set-piece.dll`. Install it in the active mod's `d2rloader/plugins/` directory.

## Runtime notes

- The plugin uses the Cube's `HoradricCubePanelMessage:Convert` event and D2RLoader's inventory, item transaction, and game-thread services.
- The Cube must contain exactly one set item plus the configured ingredients.
- Set `sound = "cursor_convert_item"` in the active mod's `horadriccubelayouthd.json` to play the game's Convert sound. The PluginSDK does not provide a public sound playback service.
- Creating the output does not trigger Chronicle Discovery.
- The plugin does not modify character save files or the game's `cubemain.txt`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).

