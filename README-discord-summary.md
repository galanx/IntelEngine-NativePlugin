# 🧠 IntelEngine
### NPC Scheduling & Smart Dispatch for SkyrimNet

*"Meet me at the Bannered Mare at sunset."*
*She agrees. Hours pass. The sun dips. You arrive — and she's already there.*

IntelEngine turns NPCs into people you can coordinate with. Schedule errands, plan ahead, and let the world move on its own.

⏰ **Schedule anything for later** — meetings, fetches, deliveries
- NPCs calculate travel time and depart early to arrive on time
- Lateness works both ways — tracked and remembered for both player and NPC
- No-shows aren't forgotten. Up to 10 scheduled + 5 active tasks

🧭 **Smart destination resolution** — no hardcoded location lists
- Indexes every location from your actual load order at runtime
- Fuzzy name matching with typo tolerance + semantic directions (*upstairs, kitchen, my bed*)
- Mod-added locations automatically discoverable

🏃 **Cross-world execution** — fetch, deliver messages, search for NPCs across the entire game world including unloaded cells. No range limit. Messages chain into meeting requests.

🔧 **Stuck recovery** — locked doors, bad navmesh, mountain passes. 3-layer recovery (waypoint navigation, multi-angle pathfinding, progressive teleport) before the NPC gives up gracefully.

🧠 **Full NPC memory** — task history, meeting outcomes, and delivered messages persist in character context. NPCs remember who stood them up.

🔌 **Developer API** — SKSE DLL exposes fuzzy actor search (cross-cell, unloaded), location resolution, and time parsing for other SkyrimNet mod authors.

⚠️ Semantic directions depend on cell geometry — reliable in vanilla, may vary in heavily modded environments. Named locations are always accurate.

📦 SKSE, SkyrimNet, SkyUI, PapyrusUtil, po3's Papyrus Extender
🔗 https://github.com/galanx/IntelEngine-GamePlugin
