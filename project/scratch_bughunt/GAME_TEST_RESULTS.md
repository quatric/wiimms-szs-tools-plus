# First-Party Wii Game Extraction Test Results

Tracking `wszst xx <disc.wbfs>` (full disc extraction) against real retail dumps,
as part of an owner-requested bug hunt across an assortment of first-party
Wii titles. Source: `mcubewii:Nintendo - Wii/Redump/[WBFS]/Games/`.

Legend: ✅ passes (clean, or fixed this session and re-verified clean) ·
🟡 passes but with a known gap/caveat · ❌ fails (crash or blocking error) ·
⏳ in progress

| Game | | Notes |
|---|---|---|
| Animal Crossing: City Folk | ✅ | Crashed with **SIGBUS** on `DATA/files/BgData/BgModel/017_1.brres` — a subfile's declared size was corrupted/huge independent of its data pointer, and the post-extraction SHA1 hash-cache builder read off the end of the buffer. Fixed (`SafeSubfileHashSize()`, commit `7613d04`, pushed). Full disc re-verified clean. |
| AquaSpace (WiiWare) | 🟡 | Character/prop `.brres` files never recognized as BRRES — no error, just silently produce zero models/textures. Root cause confirmed byte-for-byte: every one starts with an unrecognized 4-byte tag `"CX00"` immediately before an otherwise standard, already-supported LZ11 stream. Fix needs to reach the extraction-time LZ dispatch, not yet implemented. |
| Calling | ✅ | Originally: `wbrsar` total failure on WAVE-type BRSAR sounds, and HSF models exported untextured. Both fixed this session. Full disc now extracts clean. |
| Legend of Zelda, The: Twilight Princess | ❌ | `ERROR #82 [CAN'T CREATE FILE]` — a RARC member filename contains a raw non-UTF-8 byte sequence (likely Shift-JIS). macOS rejects the `open()` call outright (EILSEQ). Root-caused to the exact read/write sites, fix not yet implemented. |
| Mario Kart Wii | 🟡 | No crash. Its main music `wbrsar` conversion (`revo_kart.brsar`) was still running after 13+ minutes when interrupted by an unrelated sandbox reset — not yet confirmed whether that's a real performance issue or just a legitimately large archive. Needs a clean timed re-run. |
| Metroid: Other M | ✅ | Crashed with the same **SIGBUS** signature as Animal Crossing. Re-ran full extraction with the fix in place: completed cleanly, confirming the same fix resolved this title too. |
| Super Smash Bros. Brawl | ❌ | Crashes with **SIGTRAP** (`Trace/BPT trap: 5`) — a different signal from the SIGBUS bug, likely a failed `assert()`/`DASSERT()`. Not yet reproduced/root-caused in isolation. |
| Wii Sports | ✅ | No crash, no new errors. |

## Fixes shipped this session

1. **`29d5e17`** — Export WAVE-type RSAR sounds as standalone WAV, fixing `wbrsar` total failure on sample-only BRSAR (found via Calling).
2. **`a5de51d`** — Fix HSF models exporting untextured due to texture-index timing and a double `.png` suffix (found via Calling).
3. **`7613d04`** — Fix SIGBUS crash hashing an extracted subfile with a corrupted declared size (found via Animal Crossing: City Folk, also fixed Metroid: Other M).

## Still open

- Zelda Twilight Princess: non-UTF-8 RARC filename → `ERROR #82`.
- Super Smash Bros. Brawl: SIGTRAP crash, root cause unknown.
- Mario Kart Wii: `wbrsar` performance on `revo_kart.brsar` unconfirmed.
- AquaSpace: `CX00`-prefixed LZ11 BRRES detection gap.
