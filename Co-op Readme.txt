Alien Trilogy: Remastered - online co-op
========================================

Two players fight the same enemies through the full campaign, from the first
mission to the end of Section 3, or team up in Survival (Classic Survival or
Queen's Revenge!). Each player starts a new run with a pistol and has their own
inventory and pickup copies. Taking an item leaves your friend's copy
available. Destroyed crates, doors, switches, enemies and mission progress are
shared. Both external player appearances use the original white biohazard-suit
character.

Both computers need the SAME version of the game and their own imported game
data. The encrypted connection helper (TrilogyCoopTransport.exe) uses Windows
.NET Framework and must stay beside the game executable (the packaged game
already has it in place).

Starting a game
---------------
1. Launch the game and choose Online Co-Op from the main menu.
2. Host: choose the mode (Campaign, Classic Survival or Queen's Revenge!), the
   difficulty, the starting level or arena, and the TCP port (default 27871),
   then Host game.
3. Host: select Copy invitation code. Share it privately with your friend,
   together with your reachable IPv4 address and chosen TCP port.
4. Guest: enter that IPv4 address and port, choose Paste invitation code, then
   Join game. Both players must be running the same version.
5. Guest: choose Ready in the lobby. Host: choose Start session.
6. Host: press Enter to reveal the briefing, then Enter again to begin.

For computers on the same home network, use the host computer's LAN IPv4.
For different houses, use the host's public IPv4. The host's router must forward
the chosen TCP port to the host computer, and Windows Firewall must allow the
connection. Router setup varies; the game does not change your router or
firewall settings. Some connections use CGNAT and cannot accept direct
incoming connections without an ISP change or a suitable VPN/relay. Entering
an IP address alone cannot bypass those network restrictions.

Rules
-----
- The host runs the enemies, combat, shared objectives and team score.
- Pickups, weapons, ammo, health and armour are personal. Crate/enemy item
  drops are available separately to both players. Duplicate ID-card copies
  count only once toward the team's mission objective.
- Both living players must reach extraction, close to one another.
- Only the host controls briefings and the between-level results/menus.
  The guest sees the host's menu selections and cannot advance the campaign.
- A dead player respawns beside the survivor after two continuous seconds
  without a living hostile within approximately eight map squares.
- If both die, both return to the connected co-op lobby. The guest readies up
  again and the host can start a fresh run.
- Esc opens a local pause/options menu. The shared world keeps running.
- Cheats, passwords, level skipping, manual restarting, solo saves and photo
  mode are unavailable during a co-op run. Single-player remains separate.
- Finishing the campaign returns both players to the lobby.

Team points: regular enemy kill 100; egg 50; queen 1,000; personal pickup 10;
passed mission 1,000; death -100. The total cannot fall below zero.
Individual kill/death totals appear alongside the shared score.

Connection security
-------------------
The connection uses Windows TLS, a pinned host certificate and a private
invitation secret. Invitation codes change when hosting a new session.
Packet sizes, input values, object lists and connection queues have bounds;
the host calculates damage and points rather than accepting client claims.

Direct hosting reveals the host's IP address. No game can promise to be
hack-proof or stop a DDoS flood from saturating the host's internet line.
Share invitations only with people you trust. A protected relay/server is
needed to hide the host IP and provide upstream attack protection.

Beta notes
----------
Co-op is part of the beta. Automated tests cover the encrypted connection,
shared combat, personal pickups, safe respawning, lobby return, mirrored menus
and host-driven level transitions across all three sections; long two-house
playthroughs and latency testing are still being done. The guest receives
authoritative world updates at 15 Hz, rendered at the selected frame rate;
movement is not client-predicted.

Connection or packet errors are recorded in:
  decompilation/user/coop/errors.log
(in the packaged game: game/decompilation/user/coop/errors.log)
