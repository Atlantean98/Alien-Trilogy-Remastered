# Xbox360/tools

The Xbox 360 references in this repository come from a **side project**: an Xbox 360 homebrew build of the game that
Atlantean / NobleElite worked on alongside the PC version. That build isn't part of this repository and isn't released.

These three scripts started there and are shared with the PC version, which is why they live in this folder:

| Script | What the PC version uses it for |
| --- | --- |
| `trilogy_level.py` | Reads and writes levels in the game's own format. Used by the Extermination level builders and the level editor. |
| `make_survival_map.py` | Builds Survival arena 1. |
| `make_survival_lairs.py` | Builds Survival arenas 2 and 3. |

`tools/build_game_data.py` runs them after the disc import. Comments that mention the Xbox 360 build, or files of that
build such as `src/xenon_survival.h`, refer to the side project.
