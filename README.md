Random Set Piece adds a Horadric Cube recipe to D2RLoader:

~~~text
1 set item + optional ingredient X + optional ingredient Y -> 1 different item from the same set
~~~

## Compatibility

Tested locally with Diablo II: Resurrected 3.3.93847 and D2RLoader plugin ABI 4.

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

[tables]
table_directory = ""
~~~

`input_x` and `input_y` accept optional 3- or 4-character D2 item codes. Blank either field to omit that ingredient; blank both to require only the set item. For example, `input_x = ""` and `input_y = "r02"` requires one set item and one Eld rune. Using the same code in both fields requires two copies.

`failure_chance_percent` is an integer from 0 to 100. The default, 0, disables recipe failure.

`table_directory` can point to a folder containing both `sets.txt` and `setitems.txt`. When blank, the plugin searches the active mod's `data/global/excel` folder and several loader-provided mod roots. The tables must be available as loose files. If they're only in a packed MPQ, unpack both files and set `table_directory` to their folder.

## Build

Building requires Windows x64, CMake 3.29 or newer, Visual Studio 2022 or newer with the Windows SDK, and D2RLoader PluginSDK ABI 4.

To build offline, provide a local PluginSDK checkout with `RANDOM_SET_PIECE_LOCAL_SDK_DIR`:

~~~powershell
cmake -S . -B ..\random-set-piece-build -A x64 `
  -DRANDOM_SET_PIECE_LOCAL_SDK_DIR="<PluginSDK checkout>"
cmake --build ..\random-set-piece-build --config Release --target random_set_piece
~~~

The DLL is created at `../random-set-piece-build/Release/d2rl-random-set-piece.dll`. Install it in the active mod's `d2rloader/plugins/` directory.

If the Visual Studio CMake generator fails because MSBuild sees duplicate `PATH` and `Path` environment keys, use an x64 Visual Studio developer shell and invoke `cl.exe`, `rc.exe`, and `link.exe` directly. Running `VsDevCmd.bat` alone does not resolve the MSBuild error. Keep CMake's generated `default_config.hpp` directory in the compiler include path.

## Runtime notes

- The plugin uses the Cube's `HoradricCubePanelMessage:Convert` event and D2RLoader's inventory, item transaction, and game-thread services.
- The Cube must contain exactly one set item plus the configured ingredients.
- Set `sound = "cursor_convert_item"` in the active mod's `horadriccubelayouthd.json` to play the game's Convert sound. The PluginSDK does not provide a public sound playback service.
- Creating the output does not trigger Chronicle Discovery.
- The plugin does not modify character save files or the game's `cubemain.txt`.
