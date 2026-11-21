# Alien Trilogy: Remastered — v{VERSION}

A native Windows PC port of *Alien Trilogy* (Probe Entertainment / Acclaim, 1996), rebuilt in C++ from the original game's
logic, with modern renderers and controls, the full campaign and new modes.

**This is a beta.** Expect rough edges. Bug reports and feedback are welcome (see *Reporting a problem* below).

**No game files are included.** You need your own copy of the original PlayStation disc.

---

## What you need

| | |
| --- | --- |
| **The game** | *Alien Trilogy* for the **original PlayStation**, **NTSC-U (USA) release**, product code **SLUS-00007** |
| **Disc image format** | **BIN/CUE** (a `.cue` file plus its `.bin` track file or files), dumped from your own disc |
| **PC** | Windows 10 or 11, 64-bit; a DirectX 11, DirectX 12 or Vulkan graphics card (a software renderer is also included) |
| **Disk space** | About 2 GB free for the imported game data |
| **Optional** | [FFmpeg](https://ffmpeg.org/), only for the cinematics (see below) |

Other regions (PAL / Europe, Japan), the Sega Saturn and the PC/DOS versions are **not** supported: the importer checks the
disc and refuses any other release. ISO images are not supported either, because they leave out the CD music tracks.

### Making the BIN/CUE

Dump your own disc with a CD-ripping tool that writes BIN/CUE in raw mode (2352-byte sectors) **with all the audio tracks**,
for example ImgBurn (*Create image file from disc*), CDRWIN, or a similar tool. You should end up with one `.cue` file and
one or more `.bin` files in the same folder. Keep them together and keep the file names the `.cue` refers to.

## Installing

1. **Extract the whole zip** to a normal folder you can write to, for example `Documents\Games\Alien Trilogy Remastered`.
   Don't run it from inside the zip, and avoid `Program Files`.
2. Run **`Alien Trilogy Remastered.exe`** (the launcher).
3. Click **SELECT DISC (.CUE)** and choose your disc image's `.cue` file, or drag the `.cue` onto the launcher window.
4. Wait for the import to finish (a few minutes). The launcher:
   - checks that the disc is the supported NTSC-U release
   - verifies and copies the game files
   - extracts the CD music
   - decodes the cinematics (if FFmpeg is installed)
   - builds the Survival arenas and Extermination levels
5. Pick a renderer (**DirectX 11** is the default and the most tested) and click **PLAY**.

**PLAY** stays locked until a disc has been imported. You only need to import once. The imported data is kept in
`game\GameData` next to the launcher. **RE-IMPORT DISC** imports again, for example after installing FFmpeg.

### Cinematics (optional)

The intro and story movies are decoded with FFmpeg. Without it, the game imports and plays normally, just without the
movies. To add them:

1. Install FFmpeg. The easiest way is to open PowerShell and run `winget install Gyan.FFmpeg`. You can also download a
   build from [ffmpeg.org](https://ffmpeg.org/download.html) and add its `bin` folder to your `PATH`.
2. Close and reopen the launcher, then click **RE-IMPORT DISC**.

## What's in it

- **The full campaign**: all three sections, 46 maps, with secret areas, the original difficulty rules, saves and level
  passwords.
- **Renderers**: DirectX 11, DirectX 12, Vulkan and software, with resolution, FOV, MSAA and texture-filtering options,
  plus a photo mode.
- **Controls**: keyboard, mouse and controllers (Xbox and PlayStation pads), with remapping and rumble.
- **Survival**: endless waves in three arenas, with *Queen's Revenge!* and Xeno Skull modifiers.
- **Extermination**: a three-act campaign with a squad of Alien Handlers. It unlocks on the main menu once you complete the main
  game (its final level, 3.9.1).
- **Co-op**: online co-op for the full campaign, start to finish, and for Survival (see `Co-op Readme.txt`), plus
  replays and achievements.

### Default controls

| Action | Keyboard / mouse |
| --- | --- |
| Move | W A S D |
| Look | Mouse |
| Fire | Left mouse |
| Use / open | E |
| Change weapon | Q or mouse wheel, 1–5 |
| Sprint | Shift |
| Map | M |
| Pause / menu | Esc |
| Level skip (story campaign) | Page Up / Page Down |

All of these can be changed in **Options → Controls**, and controllers work out of the box.

**Level skip (for now):** during the main story campaign, **Page Down** jumps to the next level and **Page Up** to the
previous one. It's there for the beta so you can reach any part of the game quickly. It doesn't work in Survival,
Extermination or co-op.

## Where things are kept

Everything stays inside the extracted folder:

- `game\GameData`: the data imported from your disc. Don't share it.
- `game\decompilation\user`: saves and settings.
- `game\logs`: one folder per play session, with its log and any crash report (see *Reporting a problem*).

To uninstall, delete the folder.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| "Use the supported NTSC-U Alien Trilogy PS1 disc (SLUS-00007)" | The image is a different release or region. Only the USA PlayStation disc works. |
| "The disc image is missing CD audio tracks" | Dump the disc again as BIN/CUE including every audio track, not as ISO and not data-only. |
| "A file named by the disc image is missing" | The `.cue` lists `.bin` files that aren't beside it. Keep the `.cue` and all its `.bin` files together, with their original names. |
| "Disc file failed verification" | The dump is damaged. Clean the disc and dump it again. |
| The game window doesn't open, or closes at once | Pick a different renderer in the launcher (try DirectX 11, then Software) and update your graphics drivers. If it still fails, report it with that session's log (see below). |
| No movies | Install FFmpeg (see *Cinematics*) and click **RE-IMPORT DISC**. |
| Windows SmartScreen warns about the launcher | The launcher isn't code-signed. Choose *More info → Run anyway* if you trust where you got it from. |

## Reporting a problem

Report bugs and crashes on the project's GitHub page:
**https://github.com/Atlantean98/Alien-Trilogy-Remastered/issues**

The game keeps a log of every session, so you don't need to reproduce a problem to report it:

- Click **OPEN LOGS** in the launcher, or open `game\logs`. Each play session has its own folder, named by date and
  time.
- `session.log` records the version, your system, what you played and any errors, including minor ones that didn't
  stop the game.
- `crash.txt` appears after a crash. It records what went wrong, where, and what you were doing. When the game crashes,
  the launcher offers to open that folder for you.
- Sessions with a crash or an error are kept permanently; clean sessions are pruned to the newest 30.
- Your Windows user folder and user name are blanked out of these logs, so they're safe to post. `crash.dmp` is
  optional to attach: it can contain folder paths.

In your report, please include:

- what happened and what you expected
- the level, mode and difficulty, and which renderer you used
- the version number (shown on the launcher and in the game's main-menu corner: **v{VERSION}**)
- `session.log` (and `crash.txt` if there is one) from that session's folder
- for a disc import failure: `game\import.log`

Updates are worked on in spare time, so please allow a few days to a week for a reply.

### Balance feedback

**Survival** and **Extermination** aren't completely balanced yet. If you play them, tell us how it felt, on the
Issues page (there's a *Balance feedback* form). For example: waves that were too easy or too hard, enemies that hit
too hard or took too long to kill, ammo or health that ran short or piled up, or a squad that was too strong or too
weak. Playtesting like this is how the balance between the player and the enemies gets right.

## Credits

**Alien Trilogy: Remastered** was created by **Atlantean / NobleElite**: creative direction, game design, the new modes
(Survival, Queen's Revenge! and the Extermination campaign), the custom AI, split-screen and the level editor. AI tools
(Claude and Codex) assisted with engine reconstruction, debugging and code generation, under Atlantean / NobleElite's
direction, design, testing and tuning.

*Alien Trilogy* was developed by Probe Entertainment and published by Acclaim in 1996. The original game's credits are
shown in full in the in-game **Credits** screen.

The port's code is released under the MIT License (`LICENSE.txt`). Third-party components are listed in
`THIRD_PARTY_NOTICES.txt`.

## Legal

This is an unofficial, non-commercial fan project, not affiliated with or endorsed by the rights holders. *Alien*,
*Alien Trilogy* and related names belong to their respective owners. This package contains no original game assets and
needs a legally owned copy of the game. Don't redistribute your disc image or the imported `game\GameData` folder.
