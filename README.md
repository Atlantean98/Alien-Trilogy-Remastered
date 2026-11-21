# Alien Trilogy: Remastered

A native Windows port of *Alien Trilogy* (Probe/Acclaim, 1996). The game's logic was recovered from the original
executable as C++, routine by routine, and runs on the PlayStation version's data from your own disc, with modern
rendering and controls and a set of new modes.

By **Atlantean / NobleElite**. Current version: **v0.9.0-beta**.

## Download and play

You don't need to build anything to play:

1. Download the latest `Alien_Trilogy_Remastered_v….zip` from the [Releases](../../releases) page and extract it.
2. Run `Alien Trilogy Remastered.exe`, select your own disc image's `.cue` file (PlayStation, NTSC-U / USA,
   SLUS-00007, dumped as BIN/CUE), and wait for the import.
3. Click **PLAY**.

The release's `README.md` covers the requirements, the optional FFmpeg step for the cinematics, controls and
troubleshooting. No game files are included in the release or in this repository.

To build it yourself instead, see [Getting started](#getting-started) below.

## A note from me

> I used AI to mostly help me build this, specifically Claude Opus 5.5 for the gameplay you see here. The native PC
> build was built using Codex GPT Sol 6.1 originally, but will have holes patched using Claude in the future. I also make
> no money from this, and I don't wish to make any money from this in the future; This is FOR the community, FROM the
> community. I've loved this game and franchise nearly as much as I love Tomb Raider, so this is a passion project
> purely!
>
> I will always be honest about any AI usage, but I will always keep creative control and auditing, whilst adding my own
> ideas of modernised add-ons like free-look, 60fps+, anti-aliasing, unique game modes, and more.
>
> You WILL be required to use your own copy of Alien Trilogy, and you are to provide your own game's assets. I will never
> distribute any game files, just the program itself, and the program will extract your own copy for play.
>
> Thank you all for playing, and I will be happy to update this in my free time.

### Balance is still being tuned

**Survival** and **Extermination** aren't completely balanced yet. If you play them, your feedback helps find the right
balance between the player and the enemies. For example: waves that felt too easy or too hard, enemies that hit too
hard or soak up too much damage, ammo or health that ran short or piled up, or a squad that's too strong or too weak.
Please share it on the [Issues](../../issues) page; there's a *Balance feedback* form.

## Reporting bugs and crashes

The game keeps a log of every session, so problems can be tracked down and fixed:

- Each run gets its own folder in `logs\` (in the release: `game\logs\`, or click **OPEN LOGS** in the launcher). It
  holds `session.log`, with the version, your system, what you played and any errors (minor ones too), and after a
  crash `crash.txt` with what went wrong and where.
- Sessions with a crash or an error are kept permanently; clean sessions are pruned to the newest 30.
- Your Windows user folder and user name are blanked out of these logs, so you can post them publicly. `crash.dmp` is
  optional to attach: it can contain folder paths.

Open a [bug report](../../issues/new/choose) and attach `session.log` (and `crash.txt` if there is one) from that run's
folder.

## Credits, AI Disclosure & Licensing

### Creative direction and original modes

**Atlantean / NobleElite** is the creator, creative director and game designer of this native C++ source port and of
everything it adds to the original game:

- the original game modes: **Survival**, **Queen's Revenge!** and the three-act **Extermination** campaign
- the custom AI behaviours, including the Extermination squad
- the split-screen implementation
- the level editor

*Alien Trilogy* itself was made by Probe Entertainment and published by Acclaim in 1996; the original game's credits
are shown in full in the in-game Credits screen.

### Forks, other ports and attribution

This project is open source under the MIT License (see [`LICENSE`](LICENSE)). You are welcome to fork the code, build
on it, or adapt the custom game modes for your own projects and ports. When you do:

- keep the copyright and license notices, as the MIT License requires, and
- please include visible credit to **Atlantean / NobleElite**, with a link back to this repository, in your project's
  README or credits (and in-game, where your project has a credits screen).

### AI assistance

AI tools (Claude and Codex) were used during development to help with reconstructing the C++ engine from the original
executable, debugging, and generating code. This was done under the creative direction, system design, testing and
gameplay tuning of Atlantean / NobleElite, who decided what was built, how the game should play, and what was kept.

### Maintenance and support

Post-beta updates are worked on in spare time, so please allow a few days to a week for a reply to bug reports or
feedback. Community forks and pull requests are welcome; see [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Before you start

**No game files are included.** You need your own copy of the PlayStation version (NTSC-U, *Alien Trilogy (USA)*) as
a CUE/BIN dump. The import step reads it and writes the game's data into `GameData/`, which is never committed.

## What's in it

- The full campaign, 46 maps across all three sections, including secret areas, original difficulty rules, saves and
  level passwords.
- DirectX 11, DirectX 12, Vulkan and software renderers, with resolution/FOV/MSAA/texture-filtering options and a
  photo mode.
- Keyboard, mouse and controller support (Xbox and PlayStation pads), with remapping and rumble.
- Level skip for the beta: Page Down / Page Up jump to the next / previous level in the story campaign (not in
  Survival, Extermination or co-op).
- **Survival**: endless waves in three arenas, plus *Queen's Revenge!* and Xeno Skull modifiers.
- **Extermination**: unlocked by finishing the campaign's final level (3.9.1). Three levels with a
  three-strong squad of Alien Handlers who fight, can be downed and revived; recover the crew ID tags and clear every alien
  before extraction. Levels 1 and 2 each join two campaign maps into one.
- Online co-op for the full campaign and Survival (see `Co-op Readme.txt`), replays and achievements.

## Requirements

- Windows 10 or 11, x64
- CMake 3.20 or newer and Ninja
- A Clang toolchain that targets MinGW-w64 (for example [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)); the
  bundled SDL3 is the MinGW build
- Git and an internet connection the first time you configure (CMake fetches bgfx's sources)
- Python 3 with Pillow (`pip install pillow`) for the import and level tools
- .NET Framework 4 (part of Windows) for the co-op connection helper, built automatically

## Getting started

Run these from the repository root.

1. Import your disc:

   ```
   python tools/import_pc_ps1.py --disc "D:\path\to\Alien Trilogy (USA).cue"
   ```

2. Build the generated levels (the Survival arenas and the Extermination levels):

   ```
   python tools/build_game_data.py
   ```

3. Configure and build:

   ```
   cmake -S decompilation/cpp -B decompilation/cpp/build -G Ninja -DCMAKE_CXX_COMPILER=clang++
   powershell -ExecutionPolicy Bypass -File tools/build_playable.ps1
   ```

4. Play with one of the launchers in the root folder: `Play DirectX 11.cmd`, `Play DirectX 12.cmd`, `Play Vulkan.cmd`
   or `Play Software.cmd`.

### Packaging a release

`python tools/package_release.py` builds the player package: the launcher, a stripped Release build, the importer
with a private Python, and the player README. It writes `Release/Alien_Trilogy_Remastered_v<version>/` and a zip of
it, and refuses to finish if any game data or local user path ends up inside. It needs a Release build first:

```
cmake -S decompilation/cpp -B decompilation/cpp/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build decompilation/cpp/build-release --target recovered_level_explorer
```

The version number lives in `decompilation/cpp/ExplorerVersion.h`.

## Testing

The game carries its own self-tests. Each runs headless and exits non-zero on failure (the reason is written to
`decompilation/reports/exploration-error.txt`):

```
decompilation/cpp/build/recovered_world_explorer.exe --menu-self-test
decompilation/cpp/build/recovered_world_explorer.exe --survival-self-test
decompilation/cpp/build/recovered_world_explorer.exe --isolation-self-test
```

`tools/check_playable.ps1` runs the wider playable suite. `recovered_checks.exe` (built alongside) checks the
recovered routines themselves, and the `tools/check_*.py` scripts compare them against the original executable's
instructions (they need the DOS version's `TRILOGY.EXE` in `GameData/`; see `tools/setup_decompiler.py`).

## Layout

| Path | What it is |
| --- | --- |
| `decompilation/cpp/` | The game. The recovered original routines (`PlayerMovement.cpp`, `EnemyAI.cpp`, `LevelLoading.cpp` and so on) build into a library; `LevelExplorer.cpp` and the `Explorer*` files are the PC host around them (window, renderers, audio, HUD, menus, modes). |
| `decompilation/third_party/` | bgfx (GPU renderers) and SDL3 (controllers). |
| `tools/` | Build scripts, the disc importer, level builders (`build_extermination_decks.py`, `stitch_maps.py`), and checks. |
| `Xbox360/tools/` | The level reader/writer and Survival arena generators, shared with a console port (only these scripts are published). |
| `DemoWork/` | The disc-image reader and movie decoder the importer uses (only these scripts are published). |
| `LevelEditor/` | A level editor (`Level Editor.bat`, or `Level Editor (2D).bat`). |

## Contributing

Forks and pull requests are welcome; see [`CONTRIBUTING.md`](CONTRIBUTING.md) for the rules. In short:

- Source files use CRLF line endings.
- Keep the recovered routines faithful to the original. New behaviour belongs in the host (`Explorer*`, `LevelExplorer.cpp`)
  or in the mode code, not inside a recovered routine.
- Add or extend a self-test for what you change, and run the three self-tests above before opening a pull request.
- Never commit game data: anything read from the disc, the `GameData/` folder, or files generated from them.
- Forks and ports keep the copyright notices and credit Atlantean / NobleElite with a link back here.

## License

The project's code is under the MIT License (see `LICENSE`), copyright Atlantean / NobleElite. The license covers the
project's own code only: it grants no rights to *Alien Trilogy*'s game data, which is not included. Third-party
libraries in `decompilation/third_party` keep their own licenses.

## Legal

*Alien Trilogy* and the *Alien* franchise belong to their respective owners. This project contains no game assets and
needs a legally owned copy of the game to run.
