# Contributing

Thanks for your interest in this port. Bug reports, feedback, forks and pull requests are all welcome, from players
and modders alike.

## Response times

Post-beta updates are worked on in spare time, so please allow a few days to a week for a reply to an issue, a bug
report or a pull request.

## Reporting a bug

Use the **Bug or crash report** form on the Issues page (there's also a **Balance feedback** form for Survival and
Extermination). Please include:

- what happened and what you expected, and the level, mode and difficulty
- the renderer you were using (DirectX 11, DirectX 12, Vulkan or Software)
- that session's log. The game writes one folder per run in `logs/` (`game/logs/` in the release; the launcher's
  **OPEN LOGS** button opens it). Attach `session.log`, plus `crash.txt` after a crash. User folders and names are
  blanked out of both; `crash.dmp` is optional, since it can contain folder paths.

Never attach game data (anything from your disc or the `GameData/` folder).

Crash reports list the call stack as `module+offset`. To map an offset back to the code, use the symbol copy of that
release's executable, which the packager keeps in `Release/symbols/v<version>/`:
`llvm-addr2line -f -C -e Release/symbols/v<version>/recovered_world_explorer.exe 0x<offset + 0x140000000>`.

## Pull requests

- Source files use CRLF line endings.
- Keep the recovered routines faithful to the original. New behaviour belongs in the host (`Explorer*`,
  `LevelExplorer.cpp`) or in the mode code, not inside a recovered routine.
- Add or extend a self-test for what you change, and run these before opening a pull request:

  ```
  decompilation/cpp/build/recovered_world_explorer.exe --menu-self-test
  decompilation/cpp/build/recovered_world_explorer.exe --survival-self-test
  decompilation/cpp/build/recovered_world_explorer.exe --isolation-self-test
  ```

- Never commit game data: anything read from the disc, the `GameData/` folder, or files generated from them.
- By opening a pull request you agree that your contribution is released under the project's MIT License.

## Forks, other ports and attribution

The project is open source under the MIT License (see [`LICENSE`](LICENSE)). You are welcome to fork it, or adapt its
custom game modes (Survival, Queen's Revenge! and the Extermination campaign) for your own projects and ports. When
you do:

- keep the copyright and license notices, as the MIT License requires, and
- please include visible credit to **Atlantean / NobleElite**, with a link back to this repository, in your project's
  README or credits (and in-game, where your project has a credits screen).

## AI assistance

AI tools (Claude and Codex) were used during development to help with reconstructing the C++ engine, debugging and
generating code, under the creative direction, system design, testing and gameplay tuning of Atlantean / NobleElite.
AI-assisted contributions are welcome on the same terms: you are responsible for what you submit, so test it and make
sure you understand it.
