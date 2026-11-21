ALIEN TRILOGY LEVEL EDITOR (3D)
===============================

A standalone program for building new Alien Trilogy levels, or reworking the campaign's, in the game's own level
format - built and textured in 3D, a little like Halo 3's Forge. Double-click "Level Editor.bat": a small window
starts (close it to quit) and the editor opens in your web browser (Chrome, Edge or Firefox). It needs Python 3
with Pillow and the game's data (GameData\PS1\CD next to this folder). Nothing is downloaded.

What you see is what Export writes: the 3D view is drawn from the same level builder.

Camera
  Middle mouse drag .......... grab the map and slide it around
  Right mouse drag ........... turn the view (orbit) / look around (fly camera)
  Mouse wheel ................ zoom toward the cursor
  W A S D, arrows ............ move (Shift: faster); Q / E turn (orbit) or go down / up (fly)
  Tab ........................ fly camera: fly through the level from inside
  F frame level, C show ceilings (hidden from above), G grid lines

Building (left; key in brackets)
  Dig floor (1) .............. click or drag over solid rock to open it up; click a wall to dig through it
  Fill wall (2) .............. fill floor back in with solid wall
  Raise / lower (3) .......... click raises the floor by the height step, Shift+click lowers it; drag to paint
  Flatten (4) ................ click a floor, drag: everything you cross gets its height
  Ceiling height (5) ......... raise / Shift lower / Ctrl+click back to the automatic dome
  Ramp (6) ................... a ramp rising away from the camera (128 per cell); click again to turn it
  Paint texture (T) .......... pick a texture on the right, click any floor, wall or ceiling. Shift+click floods
                               connected floor or ceiling; Alt+click picks a texture up from the level
  Door (7) ................... a 4-cell door across a wall (floor on both sides); R turns it
  Spawn room (8), Hive wall (9), Player start (P), Enemy (N), Pickup (I), Eraser (X)
  Select / move (V) .......... click an enemy, pickup or the start to carry it, click a floor to put it down;
                               Delete removes it
  Brush size 1-7 ([ and ]), height step 32-512 game units (a cell is 512 wide)
  Ctrl+Z undo, Ctrl+Y redo, Ctrl+S save

Heights: the player climbs at most 256 from one cell to the next, so use ramps (2 ramp cells = one 256 step) for
paths between levels; taller steps make ledges. The Checks tab counts steps that are too high to climb.

Files (top)
  New ........................ a blank room or a copy of the Survival hive arena
  Edit a campaign level ...... start from one of the game's levels: layout, floor heights, ramps, ceilings, doors,
                               enemies, pickups and textures come across (lifts and sloped cave walls do not)
  Open / Save / Save as ...... your projects (.atlevel) in "Custom Levels" beside this folder
  Export ..................... writes the level into Custom Levels\<name>: L<code>LEV.MAP, <code>GFX.BIN, level.txt

Level tab: name, look (whose textures, sounds and music), Survival or Exterminate, lives, waves, enemy mix,
lighting, starting weapons and map size. Checks tab: what still needs fixing before Export.

The older top-down editor is still here as "Level Editor (2D).bat" (same project files). The level builder is
shared with the Xbox 360 build's Survival arena (Xbox360\tools\trilogy_level.py).
