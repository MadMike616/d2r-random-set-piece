# Random Set Piece

This D2RLoader client plugin adds a Horadric Cube recipe:

```text
1 set item + optional configured X + optional configured Y -> 1 random different item from the same set
```

## Compatibility

Tested locally with Diablo II: Resurrected 3.3.93847 and D2RLoader plugin ABI 4.

The plugin reads each set item's row, base item code, and set membership from
`setitems.txt`, then verifies the referenced set names against `sets.txt`.
When the Cube's native **Convert** action is pressed with the exact recipe
contents present, the plugin only consumes the click if one item is a set item
and any configured ingredients match. It chooses uniformly among the
other enabled rows in that set whose base item code differs from the source.
The transaction consumes the set item and any configured ingredients, and creates
the exact selected set row in the Cube as one atomic operation. An item with
socketed contents is rejected safely; the transaction rolls back instead of
destroying socket contents.
If the runtime rejects a randomly selected set row as unsupported, the plugin
tries the remaining eligible rows in random order. If none can be created, the
transaction is rolled back and the inputs remain untouched.
The recipe can also fail at a configurable percentage: a failed roll consumes
the configured ingredients and preserves the original set item. With no
ingredients configured, a failed roll simply leaves the set item unchanged.

## Configuration

D2RLoader creates `d2rloader/config/random-set-piece.toml` on first load. The
defaults use El (`r01`) and Eld (`r02`) runes as X and Y:

```toml
[recipe]
input_x = "r01"
input_y = "r02"
failure_chance_percent = 0

[tables]
table_directory = ""
```

`input_x` and `input_y` are optional 3- or 4-character D2 item codes. Either
may be blank to omit that ingredient. For example, `input_x = ""` with
`input_y = "r02"` requires one set item and one Eld rune. Set both to empty
strings (`input_x = ""` and `input_y = ""`) to require only one set item and
no extra inputs. The same code in both fields requires two copies.
`failure_chance_percent` is
an integer from 0 to 100 (default 0, disabled); on a failed roll the plugin
consumes any configured ingredients while preserving the set item. With no
ingredients configured, a failed roll leaves the item unchanged.
`table_directory` can point
to a directory containing both `sets.txt` and `setitems.txt`; when blank, the
plugin looks under the active mod directory at `data/global/excel` and a few
loader-provided mod roots. These table files need to be available as loose
files. If the mod only keeps them inside a packed MPQ, unpack those two files
to a directory and set `table_directory` to that directory.

## Build

Requires Windows x64, CMake 3.29+, Visual Studio 2022+ with the Windows SDK,
and D2RLoader PluginSDK ABI 4. To build offline, pass a local PluginSDK checkout
with `RANDOM_SET_PIECE_LOCAL_SDK_DIR`:

```powershell
cmake -S . -B ..\random-set-piece-build -A x64 `
  -DRANDOM_SET_PIECE_LOCAL_SDK_DIR="<PluginSDK checkout>"
cmake --build ..\random-set-piece-build --config Release --target random_set_piece
```

The output is `../random-set-piece-build/Release/d2rl-random-set-piece.dll`.
Install it under the active mod's `d2rloader/plugins/` directory.

If the Visual Studio CMake generator fails with a duplicate `PATH`/`Path`
environment key when MSBuild starts, use an x64 Visual Studio developer shell
and invoke `cl.exe`, `rc.exe`, and `link.exe` directly. Calling `VsDevCmd.bat`
alone does not fix the MSBuild error; direct compilation bypasses it. Keep the
generated `default_config.hpp` from CMake's `generated` directory in the
compiler include path.

## Runtime notes

- The plugin uses the cube's `HoradricCubePanelMessage:Convert` event and the
  D2RLoader inventory, item-transaction, and game-thread services.
- The Cube must contain exactly one set item plus the number of ingredients
  configured: one, two, or three total items.
- The source set item must have another enabled row in the same set.
- The active mod's `horadriccubelayouthd.json` can set the Convert button's
  `sound` field to `cursor_convert_item` to play D2R's cube Convert sound when
  the button is pressed. The PluginSDK has no public sound-playback service.
- The source item's item level and identified state are carried to the output.
- Socketed inputs are not consumed; transaction failure restores all inputs.
- The new set item does not trigger Chronicle Discovery
- Some SetItems rows may be rejected by the runtime item service. The plugin
  retries other eligible rows after an `Unsupported` result, preserving the
  atomic rollback behavior if every candidate is rejected.
- This version does not modify character save files or edit the game's
  `cubemain.txt`.
