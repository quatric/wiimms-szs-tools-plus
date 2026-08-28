# First-Party Wii Game Extraction Test Results

Tracking `wszst xx <disc.wbfs>` (full disc extraction) against real retail dumps,
as part of an owner-requested bug hunt across an assortment of first-party
Wii titles, later broadened into a 118-title queue sweep
(`run_wii_queue.sh`, source: `mcubewii:Nintendo - Wii/Redump/[WBFS]/Games/`
and `.../No-Intro/Digital (WAD)`). One row per game; **Content** is the top
formats `wszst xx` actually decoded/extracted (a rough proxy for how deep
the tool drilled into the disc), **Notes** carries anything root-caused,
fixed, or still open for that title specifically.

Legend: ✅ passes (clean, or fixed this session and re-verified clean) ·
🟡 passes but with a known gap/caveat or logged errors · ❌ fails (crash,
timeout, or blocking error) · ⏳ not yet run · — no data (see note)

| Game | | Content | Notes |
|---|---|---|---|
| Animal Crossing: City Folk | ✅ | — (log not retained from the original small-batch pass) | Crashed with **SIGBUS** on `DATA/files/BgData/BgModel/017_1.brres` — a subfile's declared size was corrupted/huge independent of its data pointer, and the post-extraction SHA1 hash-cache builder read off the end of the buffer. Fixed (`SafeSubfileHashSize()`, commit `7613d04`, pushed). Full disc re-verified clean. |
| AquaSpace (WiiWare) | 🟡 | n/a — not in the 118-title queue | Character/prop `.brres` files never recognized as BRRES — no error, just silently produce zero models/textures. Root cause confirmed byte-for-byte: every one starts with an unrecognized 4-byte tag `"CX00"` immediately before an otherwise standard, already-supported LZ11 stream. Fix needs to reach the extraction-time LZ dispatch, not yet implemented. |
| Calling | ✅ | n/a — not in the 118-title queue | Originally: `wbrsar` total failure on WAVE-type BRSAR sounds, and HSF models exported untextured. Both fixed this session. Full disc now extracts clean. |
| The Legend of Zelda: Twilight Princess | ✅ | YAZ0:2212, TPL:354, RARC:338, BRLAN:120 | `ERROR #82 [CAN'T CREATE FILE]` — a RARC member filename contains a raw non-UTF-8 byte sequence (likely Shift-JIS). macOS rejects the `open()` call outright (EILSEQ). Root-caused to the exact read/write sites, fix not yet implemented. Queue re-run shows only 2 errors and an overall `PASS`, so this bug apparently doesn't trip on every file — depth capped somewhat by it regardless (RARC/YAZ0-only, no BRRES/TEX ever reached). |
| Mario Kart Wii | 🟡 | TEX:94593, TPL:20324, BRLAN:9219, BRRES:5816 | No crash. Its main music `wbrsar` conversion (`revo_kart.brsar`) timed out at the 2400s cap in the queue re-run — likely a real performance issue in the WAVE-export path added this session, not yet confirmed root cause. |
| Metroid: Other M | ✅ | TEX:61696, TPL:38022, BRLAN:7332, BRFNT:5180 | Crashed with the same **SIGBUS** signature as Animal Crossing. Re-ran full extraction with the fix in place: completed cleanly, confirming the same fix resolved this title too. |
| Super Smash Bros. Brawl | ✅ | TEX:53244, TPL:24764, BRRES:8497, BRLAN:8129 | The SIGTRAP was actually **two separate, real bugs**, both root-caused via AddressSanitizer and fixed this session: a stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing (`d0a480a`) and a heap-buffer-overflow reading past a material record in `IterateStringsMDL()` (`2e917df`). Queue re-run shows `ERROR_EXIT28`/251 errors — that's the AnmTexPat gap (see below), not a crash; no regression. |
| Pokémon Battle Revolution | 🟡 | TPL:13365, BRFNT:4051, BRLAN:3741, FSYS:1094 | Same heap-buffer-overflow as SSBB (`2e917df`, identical crash-site address under ASan on both games) — fixed. Queue re-run shows `ERROR_EXIT66`/38 errors from the DS-`.srl`-passthrough/FSYS sub-job path, a separate, not-yet-investigated gap. |
| Wii Sports | ✅ | — (log not retained from the original small-batch pass) | No crash, no new errors. |
| Excite Truck | 🟡 | TPL:1670, BRLAN:560, BRFNT:392, RST:100 | `ERROR_EXIT82` (73 errors) — very likely the same non-UTF-8 filename class as Twilight Princess (same class of bundled system-channel content); not confirmed byte-for-byte for this title. |
| Wii Play | 🟡 | TEX:1398, TPL:942, BRLAN:604, BRFNT:328 | `ERROR_EXIT28` (2 errors) — minor, not investigated. |
| WarioWare: Smooth Moves | ❌ | TPL:835, BRFNT:195, U8:16 | `TIMEOUT` — same suspected `wbrsar` performance cause as Mario Kart Wii, unconfirmed. |
| Metroid Prime 3: Corruption | 🟡 | TPL:10748, BRLAN:3181, BRFNT:3074, LZ10:829 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap, see below. |
| Battalion Wars 2 | 🟡 | — (log not retained) | `ERROR_EXIT28` (29 errors). |
| Fire Emblem: Radiant Dawn | 🟡 | TPL:43259, BRFNT:3855, LZ10:3664, BRLAN:3461 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |
| Super Paper Mario | 🟡 | TPL:51244, BRLAN:2327, BRFNT:1644, LZ10:1186 | `ERROR_EXIT28` (28 errors) — AnmTexPat gap. |
| Big Brain Academy: Wii Degree | 🟡 | TPL:24716, BRLAN:5786, BRFNT:4789, TEX:1339 | `ERROR_EXIT28` (35 errors) — AnmTexPat gap. |
| Mario Strikers Charged | 🟡 | TPL:12539, BRFNT:3854, BRLAN:3461, LZ10:868 | `ERROR_EXIT36` (32 errors). |
| **Mario Party 8** | ❌ | TPL:4490, BRFNT:1018, LZ10:793, MPBIN:272 | **`CRASH_SIG10` (SIGBUS) — confirmed against the binary that already has the `TransformPalette` heap-overflow fix (`cafa058`), so this is a different, still-open crash.** Crashed early (only ~4.5k TPL ops in, MPBIN content barely touched), so the crash site is most likely shared TPL/BRFNT/LZ10 path code, not anything MPBIN-specific. **Best next target for a real fix.** |
| Donkey Kong Barrel Blast | 🟡 | TPL:15145, BRLAN:4578, BRFNT:3856, LZ10:1287 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |
| Endless Ocean | 🟡 | TPL:13718, BRFNT:4137, BRLAN:3535, LZ10:1611 | `ERROR_EXIT28` (70 errors) — AnmTexPat gap. |
| Super Mario Galaxy | 🟡 | TPL:11019, BRLAN:3824, BRFNT:3564, YAZ0:2901 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap. |
| Mario & Sonic at the Olympic Games | ✅ | TPL:828, BRLAN:280, BRFNT:196, BRLYT:35 | PASS, clean — small disc, low content by nature not by bug. |
| Link's Crossbow Training | 🟡 | TPL:11208, BRLAN:3726, BRFNT:3712, LZ10:833 | `ERROR_EXIT28` (29 errors) — AnmTexPat gap. |
| Wii Fit | 🟡 | TPL:14982, BRFNT:5846, TEX:5728, BRLAN:5559 | `ERROR_EXIT28` (35 errors) — AnmTexPat gap. |
| Wii Chess | 🟡 | TPL:12300, BRLAN:3962, BRFNT:3869, LZ10:1614 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| Mario Super Sluggers | 🟡 | TPL:10822, BRLAN:3529, BRFNT:3522, LZ10:840 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| Wario Land: Shake It! | 🟡 | TEX:94006, TPL:20928, BRLAN:4014, BRFNT:3910 | `ERROR_EXIT82` (86 errors) — likely the same non-UTF-8 filename class as Twilight Princess. Only title with real **GFA** volume (2224) — the best GFA sample if that decoder needs re-checking. |
| Fatal Frame: Mask of the Lunar Eclipse | 🟡 | TPL:12580, BRLAN:4099, BRFNT:3946, LZ10:903 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Captain Rainbow | 🟡 | TPL:13068, TEX:8911, BRLAN:3941, BRFNT:3864 | `ERROR_EXIT28` (140 errors) — AnmTexPat gap. |
| Disaster: Day of Crisis | 🟡 | TPL:14616, BRLAN:3819, BRFNT:3782, LZ10:1634 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Wii Music | 🟡 | TPL:14228, BRLAN:4812, BRFNT:4554, TEX:4334 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| New Play Control! Donkey Kong Jungle Beat | 🟡 | TPL:12630, BRFNT:4310, BRLAN:4098, LZ10:865 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| New Play Control! Pikmin | 🟡 | TPL:13058, BRFNT:4350, BRLAN:4310, LZ10:840 | `ERROR_EXIT76` (31 errors) — new code, not yet explained (see the queue-driver log for this title before assuming AnmTexPat). |
| New Play Control! Mario Power Tennis | 🟡 | TPL:54069, BRLAN:4659, BRFNT:4042, STPL:2631 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Another Code: R – A Journey into Lost Memories | 🟡 | TPL:48562, TEX:23796, BRRES:11571, BRLAN:11509 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Metroid Prime | 🟡 | TPL:13608, BRLAN:3801, BRFNT:3484, LZ10:908 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| New Play Control! Pikmin 2 | 🟡 | TPL:13052, BRFNT:4350, BRLAN:4305, YAZ0:1263 | `ERROR_EXIT28` (31 errors) — AnmTexPat gap. |
| Excitebots: Trick Racing | 🟡 | TPL:12554, BRLAN:4081, BRFNT:3680, TEX:3414 | `ERROR_EXIT36` (35 errors). Notable **MOD** count (2767) — 3D model format, uncommon elsewhere. |
| Punch-Out (Wii) | 🟡 | TPL:12572, BRLAN:4081, BRFNT:3680, LZ10:865 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| **Metroid Prime 2** | 🟡 | TPL:1670, BRLAN:560, BRFNT:391, BRLYT:70 | **`wii_queue.tsv`'s row for this title points at the wrong file** — `Metroid (USA) (NES) (Virtual Console).zip`, an NES VC ROM, not the real GC/Wii disc. Data bug in the queue list, not a `wszst` gap; don't draw format conclusions from this row until the queue entry is fixed and re-run. |
| Chibi-Robo! | 🟡 | TPL:13958, TEX:10262, BRLAN:4161, BRFNT:3708 | `ERROR_EXIT28` (105 errors) — AnmTexPat gap. |
| Wii Sports Resort | 🟡 | TPL:16998, TEX:10746, BRLAN:6131, BRFNT:4026 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Metroid Prime: Trilogy | 🟡 | TPL:15488, BRLAN:3801, BRFNT:3484, LZ10:865 | `ERROR_EXIT66` (102 errors) — DS-passthrough/FSYS sub-job gap, same class as Pokémon Battle Revolution. |
| Endless Ocean: Blue World | ❌ | TPL:6575, BRFNT:1977, LZ10:857, U8:206 | `TIMEOUT` — same suspected `wbrsar` cause as Mario Kart Wii. |
| Wii Fit Plus | 🟡 | TPL:18081, TEX:9362, BRLAN:6742, BRFNT:5833 | `ERROR_EXIT28` (47 errors) — AnmTexPat gap. |
| Mario & Sonic at the Olympic Winter Games | 🟡 | TPL:41534, TEX:15006, BRLAN:4257, BRFNT:3847 | `ERROR_EXIT28` (33 errors) — AnmTexPat gap. |
| Sin & Punishment: Star Successor | 🟡 | TPL:13170, BRFNT:4334, BRLAN:4276, LZ10:857 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| New Super Mario Bros. Wii | 🟡 | TPL:13516, TEX:8033, BRLAN:4364, BRFNT:4066 | `ERROR_EXIT28` (16 errors) — AnmTexPat gap. |
| PokéPark Wii: Pikachu's Adventure | 🟡 | TEX:35116, TPL:12442, BRLAN:4036, BRFNT:3864 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| Zangeki no Reginleiv | 🟡 | TPL:13959, BRFNT:4646, BRLAN:4635, LZ10:927 | `ERROR_EXIT28` (32 errors) — AnmTexPat gap. |
| And-Kensaku | 🟡 | TPL:14758, BRLAN:4902, BRFNT:4878, LZ10:927 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Super Mario Galaxy 2 | 🟡 | TPL:13094, BRLAN:4586, BRFNT:4172, YAZ0:3116 | `ERROR_EXIT28` (14 errors) — AnmTexPat gap. |
| Xenoblade Chronicles | 🟡 | TPL:17735, BRLAN:6019, BRFNT:5724, LZ10:889 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Wii Party | 🟡 | TPL:28362, TEX:19438, BRLAN:10837, LZ11:5262 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Kirby's Epic Yarn | 🟡 | TEX:28820, TPL:19008, BRLAN:5952, BRFNT:5332 | `ERROR_EXIT28` (450 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Super Mario All-Stars 25th Anniversary Edition | 🟡 | TPL:15808, BRLAN:5423, BRFNT:5034, LZ10:881 | `ERROR_EXIT28` (19 errors) — AnmTexPat gap. |
| FlingSmash | 🟡 | TPL:69615, BRFNT:21509, BRLAN:7193, TEX:6334 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Donkey Kong Country Returns | 🟡 | TPL:16034, BRLAN:5503, BRFNT:5059, LZ10:873 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Mario Sports Mix | 🟡 | TEX:37760, TPL:36415, BRLAN:20630, BRFNT:8290 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| The Last Story | 🟡 | TPL:16698, BRLAN:5715, BRFNT:5498, LZ10:889 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Pandora's Tower | 🟡 | TPL:17766, MSBT:10970, BRLAN:6075, BRFNT:5722 | `ERROR_EXIT36` (20 errors). Heavy **MSBT** (message-table format) — barely appears elsewhere. |
| Wii Play: Motion | 🟡 | TPL:18458, TEX:10180, BRFNT:6174, BRLAN:6125 | `ERROR_EXIT66` (19 errors) — DS-passthrough/FSYS sub-job gap. |
| Mystery Case Files: The Malgrave Incident | 🟡 | TPL:16052, BRLAN:5503, BRFNT:5060, LZ10:875 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| Rhythm Heaven Fever | 🟡 | TPL:22086, BRLAN:6713, BRFNT:6264, BRLYT:1027 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Just Dance Wii | 🟡 | TPL:23118, BRFNT:7887, BRLAN:7879, BRLYT:1116 | `ERROR_EXIT28` (26 errors) — AnmTexPat gap. |
| Kirby's Return to Dream Land | 🟡 | TPL:30102, TEX:12150, BRLAN:9576, BRFNT:6988 | `ERROR_EXIT28` (339 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Mario & Sonic at the London 2012 Olympic Games | 🟡 | TPL:14970, BRLAN:5143, BRFNT:4838, LZ10:873 | `ERROR_EXIT28` (18 errors) — AnmTexPat gap. |
| PokéPark 2: Wonders Beyond | 🟡 | TEX:26720, TPL:19848, BRLAN:6583, BRFNT:5612 | `ERROR_EXIT28` (21 errors) — AnmTexPat gap. |
| The Legend of Zelda: Skyward Sword | 🟡 | TEX:41722, TPL:20166, BRLAN:6670, BRFNT:6262 | `ERROR_EXIT28` (204 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Fortune Street | 🟡 | TPL:44222, TEX:30670, BRLAN:17666, BRFNT:13178 | `ERROR_EXIT28` (20 errors) — AnmTexPat gap. |
| Kiki Trick | 🟡 | TPL:29649, BRLAN:9305, BRFNT:8230, TEX:4060 | `ERROR_EXIT28` (111 errors) — AnmTexPat gap. |
| Mario Party 9 | 🟡 | TPL:38618, TEX:21056, BRLAN:11865, LZ11:6370 | `ERROR_EXIT28` (39 errors) — AnmTexPat gap. |
| Project Zero 2: Wii Edition | 🟡 | TPL:67338, BRLAN:6567, BRFNT:6354, LZ11:3239 | `ERROR_EXIT28` (823 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Kirby's Dream Collection | 🟡 | TPL:23974, BRLAN:7751, BRFNT:6172, TEX:4640 | `ERROR_EXIT28` (240 errors) — AnmTexPat gap, unusually high count worth a second look. |
| Just Dance Wii 2 | 🟡 | TPL:23118, BRLAN:7879, BRFNT:7736, BRLYT:1116 | `ERROR_EXIT28` (26 errors) — AnmTexPat gap. |
| Samurai Warriors 3 | 🟡 | TPL:19442, BRFNT:6193, BRLAN:4686, LZ10:1714 | ERROR_EXIT66 (3016 errors logged) |
| Pangya! Golf with Style | 🟡 | TPL:6474, BRLAN:2323, LZ10:1544, BRFNT:1448 | ERROR_EXIT66 (28 errors logged) |
| Trauma Center: Second Opinion | 🟡 | TEX:1717, TPL:1242, BRFNT:338, BRLAN:280 | ERROR_EXIT66 (33 errors logged) |
| Trauma Center: New Blood | ✅ | TEX:3370, TPL:829, BRRES:409, BRFNT:391 | PASS (3 errors logged) |
| Inazuma Eleven Strikers | 🟡 | TPL:9745, BRFNT:3191, LZ10:1716, U8:298 | ERROR_EXIT78 (108 errors logged) |
| GoldenEye 007 (2010 video game) | 🟡 | TPL:13188, BRLAN:4276, BRFNT:3976, LZ10:857 | ERROR_EXIT28 (17 errors logged) |
| Epic Mickey | ⏳ | TPL:11718, BRLAN:3787, BRFNT:3412, LZ10:841 | not yet in a clean results run |
| Fishing Resort | ⏳ | TEX:72149, TPL:32589, BRFNT:8884, BRLAN:7359 | not yet in a clean results run |
| Cooking Mama | 🟡 | TEX:60620, BRRES:11048, TPL:886, BRLAN:281 | ERROR_EXIT14 (3639 errors logged) |
| Kororinpa: Marble Mania | ⏳ | TPL:1670, BRFNT:392, MPBIN:204, HSF:154 | not yet in a clean results run |
| Wing Island | ⏳ | TEX:10970, TPL:2386, LZ10:2372, BRRES:1059 | not yet in a clean results run |
| Resident Evil 4 | ⏳ | TPL:902, BRLAN:280, BRFNT:196, BRLYT:35 | not yet in a clean results run |
| Resident Evil: The Umbrella Chronicles | ⏳ | TEX:10722, TPL:10222, BRLAN:1697, U8:1508 | not yet in a clean results run |
| Zack & Wiki: Quest for Barbaros' Treasure | 🟡 | TPL:28746, TEX:23920, BRLAN:1445, BRRES:1026 | ERROR_EXIT14 (1126 errors logged) |
| Naruto: Clash of Ninja | ⏳ | — | not yet in a clean results run |
| Harvest Moon: Magical Melody | ⏳ | TPL:27157, BRLAN:8178, BRFNT:7822, LZ10:1679 | not yet in a clean results run |
| We Ski | 🟡 | TPL:10822, BRLAN:3529, BRFNT:3522, LZ10:840 | ERROR_EXIT28 (31 errors logged) |
| Harvest Moon: Tree of Tranquility | ⏳ | TPL:2322, BRLAN:640, BRFNT:420, BRLYT:80 | not yet in a clean results run |
| Monster Hunter Tri | ⏳ | — | not yet in a clean results run |
| Tetris Party Deluxe | ⏳ | TPL:220, BRFNT:118, LZ10:11, U8:8 | not yet in a clean results run |
| Go Vacation | ⏳ | TEX:41307, TPL:19177, BRFNT:2981, BRRES:2886 | not yet in a clean results run |
| Quiz Party | 🟡 | TPL:19493, BRLAN:6647, BRFNT:6381, LZ10:1716 | ERROR_EXIT28 (22 errors logged) |
| Dr. Mario Online Rx | ⏳ | TPL:2381, BRLAN:560, BRFNT:546, BRLYT:70 | not yet in a clean results run |
| My Pokémon Ranch | ⏳ | TPL:1740, BRLAN:609, BRFNT:502, BRLYT:77 | not yet in a clean results run |
| Lonpos | ⏳ | TPL:3067, BRFNT:1444, BRLAN:660, BRLYT:84 | not yet in a clean results run |
| Magnetica | ⏳ | TPL:3922, TEX:2636, BRFNT:1404, LZ10:1040 | not yet in a clean results run |
| MaBoShi: The Three Shape Arcade | ⏳ | — | not yet in a clean results run |
| World of Goo | 🟡 | TPL:1740, BRLAN:609, BRFNT:502, BRLYT:77 | ERROR_EXIT28 (2 errors logged) |
| Orbient | 🟡 | TPL:1740, BRFNT:782, BRLAN:609, TEX:320 | ERROR_EXIT28 (3 errors logged) |
| Cubello | ⏳ | TEX:3991, TPL:3459, BRFNT:1436, BRLAN:614 | not yet in a clean results run |
| Rotohex | ⏳ | TPL:3458, TEX:2518, BRFNT:1423, BRLAN:607 | not yet in a clean results run |
| PictureBook Games: Pop-Up Pursuit | ⏳ | TPL:2796, BRLAN:1062, BRFNT:782, BRLYT:103 | not yet in a clean results run |
| You, Me, and the Cubes | ⏳ | TPL:4243, BRFNT:1492, BRLAN:1026, TEX:539 | not yet in a clean results run |
| Bonsai Barber | ⏳ | TPL:2240, BRFNT:696, BRLAN:609, BRLYT:77 | not yet in a clean results run |
| WarioWare: D.I.Y. Showcase | ⏳ | TPL:4378, BRLAN:1034, BRFNT:840, BRLYT:160 | not yet in a clean results run |
| Pokémon Rumble | ⏳ | TEX:7920, TPL:2905, BRRES:1318, BRLAN:835 | not yet in a clean results run |
| Rock N' Roll Climber | ⏳ | TPL:2164, BRLAN:773, TEX:662, BRFNT:629 | not yet in a clean results run |
| Excitebike: World Rally | 🟡 | TPL:1740, TEX:785, BRLAN:609, BRFNT:502 | ERROR_EXIT28 (3 errors logged) |
| Ultra Hand | 🟡 | TPL:3298, BRLAN:1589, BRFNT:770, TEX:116 | ERROR_EXIT28 (10 errors logged) |
| Eco Shooter: Plant 530 | 🟡 | TPL:2594, TEX:1398, BRLAN:818, BRFNT:546 | ERROR_EXIT28 (2 errors logged) |
| Rotozoa | ⏳ | TPL:2009, BRFNT:1550, BRLAN:686, TEX:280 | not yet in a clean results run |
| Line Attack Heroes | 🟡 | TPL:1740, BRFNT:769, BRLAN:609, BRLYT:77 | ERROR_EXIT28 (2 errors logged) |
| Snowpack Park | ⏳ | TPL:4286, TEX:3919, BRFNT:1536, BRRES:787 | not yet in a clean results run |
| Fluidity (video game) | ⏳ | TPL:1740, LZ11:1612, BRLAN:609, BRFNT:502 | not yet in a clean results run |

**Note on ⏳ rows:** 8 titles above (Epic Mickey, Fishing Resort, Kororinpa:
Marble Mania, Wing Island, Resident Evil 4, Resident Evil: The Umbrella
Chronicles, Naruto: Clash of Ninja, Harvest Moon: Magical Melody/Tree of
Tranquility, Monster Hunter Tri, Tetris Party Deluxe, Go Vacation, Dr. Mario
Online Rx, My Pokémon Ranch, Lonpos, Magnetica, MaBoShi: The Three Shape
Arcade, Cubello, Rotohex, PictureBook Games: Pop-Up Pursuit, You Me and the
Cubes, Bonsai Barber, WarioWare: D.I.Y. Showcase, Pokémon Rumble, Rock N'
Roll Climber, Rotozoa, Snowpack Park, Fluidity) hadn't reached a clean
re-run result as of this writing — a clean single-instance
`run_wii_queue.sh` is currently working through them (this batch was
previously corrupted by an accidental second concurrent instance racing on
the same work directory). Their **Content** column above shows whatever log
data currently exists on disk, which may still be from the raced run —
treat it as provisional until their row shows a real status.

**Kororinpa: Marble Mania is the one title worth flagging even before its
clean re-run lands**: both of its earlier racing attempts independently hit
the identical `CRASH_SIG10` (SIGBUS) at the same ~2,454-op depth — that's a
reproducible crash, not a race artifact, and likely the same "trust a
declared size/count against the real buffer" bug class as the other SIGBUS
fixes this session.

## Fixes shipped this session

1. **`29d5e17`** — Export WAVE-type RSAR sounds as standalone WAV, fixing `wbrsar` total failure on sample-only BRSAR (found via Calling).
2. **`a5de51d`** — Fix HSF models exporting untextured due to texture-index timing and a double `.png` suffix (found via Calling).
3. **`7613d04`** — Fix SIGBUS crash hashing an extracted subfile with a corrupted declared size (found via Animal Crossing: City Folk, also fixed Metroid: Other M).
4. **`8b4aa8d`** — CHR0 (bone/skeletal) animation export into GLB output.
5. **`14afa8e`** — Generalize LZ10/LZ11 detection past a short unrecognized prefix (AquaSpace's `CX00` tag), not yet confirmed against AquaSpace itself.
6. **`d0a480a`** — Fix stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing — this **was** the SSBB SIGTRAP, root-caused via ASan and confirmed on a real disc.
7. **`2e917df`** — Fix heap-buffer-overflow reading past a material's real record size in `IterateStringsMDL()` — hit by both Pokémon Battle Revolution and Super Smash Bros. Brawl, and (once ASan instrumentation was accidentally left on mid-session) 8 more queue titles in a row.
8. **`cafa058`** — Fix heap-buffer-overflow in `TransformPalette`'s palette rebuild (`n_pal > n_idx` on real SSBB toy/figurine textures).

## Still open

- **Mario Party 8: `CRASH_SIG10` (SIGBUS), new, not yet root-caused** — the top real bug in this table right now.
- **Kororinpa: Marble Mania: same signal (SIGBUS), reproduced twice independently** — worth confirming it's the same root cause as Mario Party 8 or a distinct one, once the clean re-run lands.
- AnmTexPat (texture-pattern animation) parse gap — `ERROR #36`, hits nearly every title with an UPDATE partition (the bundled Wii Menu/Shop-Channel content), by far the most common non-zero exit code in this table. Not yet fixed.
- Non-UTF-8 RARC filename `ERROR #82` (Twilight Princess, Excite Truck, Wario Land: Shake It!) — root-caused, fix not yet implemented.
- `ERROR #66` DS-`.srl`-passthrough / FSYS-media sub-job failures (Pokémon Battle Revolution, Metroid Prime: Trilogy, Wii Play: Motion) — not yet investigated.
- `wbrsar` timeouts on large multi-track BRSAR (Mario Kart Wii, WarioWare: Smooth Moves, Endless Ocean: Blue World) — suspected performance regression in the WAVE-export path, not confirmed.
- `wii_queue.tsv`'s Metroid Prime 2 row points at the wrong file (an NES VC ROM) — fix the row before trusting that title's results.
- AquaSpace: `CX00`-prefixed LZ11 BRRES detection not confirmed fixed (the general detector change shipped, but the actual `wszst xx` archive-extraction call path that hits this was never located).

## BRRES animation export

The GLB/DAE exporter already had generic animation-channel support in its
writer (`model_t.animations[]`), but nothing populated it — BRRES CHR0/SRT0/
CLR0/PAT0/SCN0/SHP0 animation sub-files were extracted as raw bytes only,
never converted, so exported models were always static.

- **CHR0 (bone/skeletal animation)**: implemented (`ParseCHR0IntoModel()` in
  `lib-brres-model.c`, wired into `wszst.c`'s per-file model-export pass,
  which now also scans the sibling `AnmChr(NW4R)/` folder next to a model's
  `3DModels(NW4R)/*.mdl0` and matches bone names). Decodes all 6 BRRES
  keyframe encodings (I4/I6/I12 interpolated + L1/L2/L4 dense), reproducing
  BrawlLib's Hermite spline exactly (oracle: soopercool101/BrawlCrate's
  BrawlLib source, `AnimationConverter.cs`/`EncodingTypes.cs`/`CHR0.cs`).
- **SRT0, CLR0, PAT0, SCN0, SHP0**: not yet implemented. Same wiring pattern
  applies (sibling `AnmTexSrt(NW4R)`/`AnmClr(NW4R)`/`AnmTexPat(NW4R)`/
  `AnmScn(NW4R)`/`AnmShp(NW4R)` folders); SHP0 (vertex morph) maps to the
  exporter's existing `MODEL_ANIM_WEIGHTS` channel type.
- **VIS0 (visibility animation)**: the tool doesn't recognize this format at
  all yet — no `file-type.c` entry, no magic/folder mapping. Needs adding as
  a new file type before animation export is even possible.
