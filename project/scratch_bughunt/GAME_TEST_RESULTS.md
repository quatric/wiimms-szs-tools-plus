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
| Super Smash Bros. Brawl | ✅ | The SIGTRAP was actually **two separate, real bugs**, both root-caused via AddressSanitizer and fixed this session: a stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing (`d0a480a`) and a heap-buffer-overflow reading past a material record in `IterateStringsMDL()` (`2e917df`). ASan-verified clean full-disc re-run in progress; the production (non-ASan) binary already cleanly re-extracted Metroid Prime 3 past the same code path. |
| Pokémon Battle Revolution | ✅ | Same heap-buffer-overflow as SSBB (`2e917df`, identical crash-site address under ASan on both games) — a material's declared texture-layer count/offset didn't fit its own record. ASan-verified clean after the fix. |
| Wii Sports | ✅ | No crash, no new errors. |

## Fixes shipped this session

1. **`29d5e17`** — Export WAVE-type RSAR sounds as standalone WAV, fixing `wbrsar` total failure on sample-only BRSAR (found via Calling).
2. **`a5de51d`** — Fix HSF models exporting untextured due to texture-index timing and a double `.png` suffix (found via Calling).
3. **`7613d04`** — Fix SIGBUS crash hashing an extracted subfile with a corrupted declared size (found via Animal Crossing: City Folk, also fixed Metroid: Other M).
4. **`8b4aa8d`** — CHR0 (bone/skeletal) animation export into GLB output (found via the owner's "do the animation facets work?" question — the answer was no, at all).
5. **`14afa8e`** — Generalize LZ10/LZ11 detection past a short unrecognized prefix (AquaSpace's `CX00` tag), not yet confirmed against AquaSpace itself.
6. **`d0a480a`** — Fix stack-buffer-overflow in `GetByMagicFF()`'s OBJ-text sniffing — this **was** the SSBB SIGTRAP, root-caused via ASan and confirmed on a real disc.
7. **`2e917df`** — Fix heap-buffer-overflow reading past a material's real record size in `IterateStringsMDL()` — hit by both Pokémon Battle Revolution and Super Smash Bros. Brawl (same crash-site address on both), very likely widespread across real MDL0 material data given how many of the 118-title queue's titles hit it once ASan instrumentation was accidentally left on (see below).

## Still open

- Zelda Twilight Princess: non-UTF-8 RARC filename → `ERROR #82`.
- Mario Kart Wii: `wbrsar` performance on `revo_kart.brsar` unconfirmed.
- AquaSpace: `CX00`-prefixed LZ11 BRRES detection not confirmed fixed (the general detector change shipped, but the actual `wszst xx` archive-extraction call path that hits this was never located).
- The 118-title queue's `run_wii_queue.sh` accidentally ran for ~8 titles (Pokémon Battle Revolution through Endless Ocean) against a leftover AddressSanitizer debug build instead of the real production binary, mid-session, while this ASan build was being used to root-cause the SSBB/PBR crashes above. Every one of those 8 titles aborted on the *same* material heap-overflow bug rather than being 8 independent new crashes. Their bad `CRASH_SIG6` rows were stripped from `results.tsv` so a future queue run retries them for real with the fixed production `wszst`; don't read those 8 titles as still-broken without a fresh run.

## 118-title `wszst xx` queue sweep (2026-08-27/28, `run_wii_queue.sh`)

Broader, shallower pass than the assortment above: every first-party (and a
handful of well-known third-party) Wii/WiiWare title from
`mcubewii:...Redump/[WBFS]` / `.../No-Intro/Digital (WAD)`, one `wszst xx
<disc>` per title, classified only by process exit code/signal +
`grep -c "ERROR #"` on the log — **not** individually triaged the way the
titles in the section above were. 118/118 complete.

Legend: ✅ extracted cleanly (or with only cosmetic warnings) · 🟡 extracted,
but the process exited non-zero (real per-file errors logged — mostly the
known AnmTexPat/CAN'T-CREATE-FILE gaps below, not fresh bugs) · ❌ crashed,
timed out, or failed to download · ⚠️ **data not trustworthy** — see the
race-condition note right below the table before reading these rows.

**⚠️ Race-condition caveat, read before trusting any ⚠️ row:** partway
through this run a second copy of `run_wii_queue.sh` was accidentally
started on top of the one already running (both processes shared the same
`work/` dir and `results.tsv`). The 32 titles from Samurai Warriors 3
onward each got two interleaved, colliding extraction attempts and two
conflicting result rows — e.g. Samurai Warriors 3 logged both `PASS` and
`ERROR_EXIT78` for the same title. Those rows are marked ⚠️ with both
conflicting results shown; **none of them should be treated as a confirmed
pass or a confirmed bug** until re-run cleanly, one process at a time.

| | Game | Result |
|---|---|---|
| ✅ | Wii Sports | PASS |
| ✅ | Animal Crossing: City Folk | PASS |
| ✅ | The Legend of Zelda: Twilight Princess | PASS (2 errors) |
| ❌ | WarioWare: Smooth Moves | TIMEOUT |
| 🟡 | Metroid Prime 3: Corruption | ERROR_EXIT28 (29 errors) |
| 🟡 | Battalion Wars 2 | ERROR_EXIT28 (29 errors) |
| 🟡 | Excite Truck | ERROR_EXIT82 (73 errors) |
| 🟡 | Wii Play | ERROR_EXIT28 (2 errors) |
| 🟡 | Pokémon Battle Revolution | ERROR_EXIT66 (38 errors) |
| 🟡 | Fire Emblem: Radiant Dawn | ERROR_EXIT28 (32 errors) |
| 🟡 | Super Paper Mario | ERROR_EXIT28 (28 errors) |
| 🟡 | Big Brain Academy: Wii Degree | ERROR_EXIT28 (35 errors) |
| 🟡 | Mario Strikers Charged | ERROR_EXIT36 (32 errors) |
| ❌ | Mario Party 8 | CRASH_SIG10 (SIGBUS, 26 errors) |
| 🟡 | Donkey Kong Barrel Blast | ERROR_EXIT28 (32 errors) |
| 🟡 | Endless Ocean | ERROR_EXIT28 (70 errors) |
| 🟡 | Super Mario Galaxy | ERROR_EXIT28 (29 errors) |
| ✅ | Mario & Sonic at the Olympic Games | PASS |
| 🟡 | Link's Crossbow Training | ERROR_EXIT28 (29 errors) |
| 🟡 | Wii Fit | ERROR_EXIT28 (35 errors) |
| 🟡 | Wii Chess | ERROR_EXIT28 (31 errors) |
| 🟡 | Super Smash Bros. Brawl | ERROR_EXIT28 (251 errors) |
| ❌ | Mario Kart Wii | TIMEOUT (35 errors logged before cutoff) |
| 🟡 | Mario Super Sluggers | ERROR_EXIT28 (31 errors) |
| 🟡 | Wario Land: Shake It! | ERROR_EXIT82 (86 errors) |
| 🟡 | Fatal Frame: Mask of the Lunar Eclipse | ERROR_EXIT28 (33 errors) |
| 🟡 | Captain Rainbow | ERROR_EXIT28 (140 errors) |
| 🟡 | Disaster: Day of Crisis | ERROR_EXIT28 (33 errors) |
| 🟡 | Wii Music | ERROR_EXIT28 (33 errors) |
| 🟡 | New Play Control! Donkey Kong Jungle Beat | ERROR_EXIT28 (33 errors) |
| 🟡 | New Play Control! Pikmin | ERROR_EXIT76 (31 errors) |
| 🟡 | New Play Control! Mario Power Tennis | ERROR_EXIT28 (33 errors) |
| 🟡 | Another Code: R – A Journey into Lost Memories | ERROR_EXIT28 (33 errors) |
| 🟡 | Metroid Prime | ERROR_EXIT28 (33 errors) |
| 🟡 | New Play Control! Pikmin 2 | ERROR_EXIT28 (31 errors) |
| 🟡 | Excitebots: Trick Racing | ERROR_EXIT36 (35 errors) |
| 🟡 | Punch-Out (Wii) | ERROR_EXIT28 (33 errors) |
| 🟡 | Metroid Prime 2 | ERROR_EXIT28 (2 errors) |
| 🟡 | Chibi-Robo! | ERROR_EXIT28 (105 errors) |
| 🟡 | Wii Sports Resort | ERROR_EXIT28 (33 errors) |
| 🟡 | Metroid Prime: Trilogy | ERROR_EXIT66 (102 errors) |
| ❌ | Endless Ocean: Blue World | TIMEOUT (4 errors logged before cutoff) |
| 🟡 | Wii Fit Plus | ERROR_EXIT28 (47 errors) |
| 🟡 | Mario & Sonic at the Olympic Winter Games | ERROR_EXIT28 (33 errors) |
| 🟡 | Sin & Punishment: Star Successor | ERROR_EXIT28 (14 errors) |
| 🟡 | New Super Mario Bros. Wii | ERROR_EXIT28 (16 errors) |
| 🟡 | PokéPark Wii: Pikachu's Adventure | ERROR_EXIT28 (14 errors) |
| 🟡 | Zangeki no Reginleiv | ERROR_EXIT28 (32 errors) |
| 🟡 | And-Kensaku | ERROR_EXIT28 (18 errors) |
| 🟡 | Super Mario Galaxy 2 | ERROR_EXIT28 (14 errors) |
| 🟡 | Xenoblade Chronicles | ERROR_EXIT28 (20 errors) |
| 🟡 | Wii Party | ERROR_EXIT28 (18 errors) |
| 🟡 | Metroid: Other M | ERROR_EXIT28 (18 errors) |
| 🟡 | Kirby's Epic Yarn | ERROR_EXIT28 (450 errors) |
| 🟡 | Super Mario All-Stars 25th Anniversary Edition | ERROR_EXIT28 (19 errors) |
| 🟡 | FlingSmash | ERROR_EXIT28 (18 errors) |
| 🟡 | Donkey Kong Country Returns | ERROR_EXIT28 (18 errors) |
| 🟡 | Mario Sports Mix | ERROR_EXIT28 (18 errors) |
| 🟡 | The Last Story | ERROR_EXIT28 (20 errors) |
| 🟡 | Pandora's Tower | ERROR_EXIT36 (20 errors) |
| 🟡 | Wii Play: Motion | ERROR_EXIT66 (19 errors) |
| 🟡 | Mystery Case Files: The Malgrave Incident | ERROR_EXIT28 (18 errors) |
| 🟡 | Rhythm Heaven Fever | ERROR_EXIT28 (20 errors) |
| 🟡 | Just Dance Wii | ERROR_EXIT28 (26 errors) |
| 🟡 | Kirby's Return to Dream Land | ERROR_EXIT28 (339 errors) |
| 🟡 | Mario & Sonic at the London 2012 Olympic Games | ERROR_EXIT28 (18 errors) |
| 🟡 | PokéPark 2: Wonders Beyond | ERROR_EXIT28 (21 errors) |
| 🟡 | The Legend of Zelda: Skyward Sword | ERROR_EXIT28 (204 errors) |
| 🟡 | Fortune Street | ERROR_EXIT28 (20 errors) |
| 🟡 | Kiki Trick | ERROR_EXIT28 (111 errors) |
| 🟡 | Mario Party 9 | ERROR_EXIT28 (39 errors) |
| 🟡 | Project Zero 2: Wii Edition | ERROR_EXIT28 (823 errors) |
| 🟡 | Kirby's Dream Collection | ERROR_EXIT28 (240 errors) |
| 🟡 | Just Dance Wii 2 | ERROR_EXIT28 (26 errors) |
| ⚠️ | Samurai Warriors 3 | RACED: PASS(1 err) vs ERROR_EXIT78(2997 errs) |
| 🟡 | Pangya! Golf with Style | ERROR_EXIT66 (28 errors) |
| ⚠️ | Trauma Center: Second Opinion | RACED: ERROR_EXIT66(2) vs ERROR_EXIT14(2) |
| ⚠️ | Trauma Center: New Blood | RACED: ERROR_EXIT66(2) vs PASS(3) |
| 🟡 | Inazuma Eleven Strikers | ERROR_EXIT78 (108 errors) |
| ⚠️ | GoldenEye 007 (2010) | RACED: ERROR_EXIT66(2) vs PASS(3) |
| ⚠️ | Epic Mickey | RACED: ERROR_EXIT66(2) vs ERROR_EXIT28(15) |
| ⚠️ | Fishing Resort | RACED: ERROR_EXIT78(259) vs ERROR_EXIT28(277) |
| 🟡 | Cooking Mama | ERROR_EXIT14 (3639 errors) |
| ⚠️ | Kororinpa: Marble Mania | RACED: CRASH_SIG10(0) vs CRASH_SIG10(5) — *both* runs crashed, worth a real re-run |
| ⚠️ | Wing Island | RACED: ERROR_EXIT78(32) vs PASS(32) |
| ⚠️ | Resident Evil 4 | RACED: ERROR_EXIT66(2) vs PASS(3) |
| ⚠️ | Resident Evil: The Umbrella Chronicles | RACED: ERROR_EXIT66(2) vs PASS(5) |
| 🟡 | Zack & Wiki: Quest for Barbaros' Treasure | ERROR_EXIT14 (1126 errors) |
| ⚠️ | Naruto: Clash of Ninja | RACED: ERROR_EXIT66(2) vs PASS(3) |
| ⚠️ | Harvest Moon: Magical Melody | RACED: ERROR_EXIT28(31) vs ERROR_EXIT28(243) |
| 🟡 | We Ski | ERROR_EXIT28 (31 errors) |
| ⚠️ | Harvest Moon: Tree of Tranquility | RACED: ERROR_EXIT66(2) vs PASS(3) |
| ⚠️ | Monster Hunter Tri | RACED: ERROR_EXIT66(2) vs PASS(3) |
| ⚠️ | Tetris Party Deluxe | RACED: ERROR_EXIT66(2) vs ERROR_EXIT78(891) |
| ⚠️ | Go Vacation | RACED: ERROR_EXIT66(2) vs ERROR_EXIT90(18211) |
| 🟡 | Quiz Party | ERROR_EXIT28 (22 errors) |
| ⚠️ | Dr. Mario Online Rx | RACED: ERROR_EXIT28(2) vs ERROR_EXIT78(942) |
| ⚠️ | My Pokémon Ranch | RACED: ERROR_EXIT66(2) vs ERROR_EXIT28(4) |
| ⚠️ | Lonpos | RACED: ERROR_EXIT28(2) vs ERROR_EXIT78(216) |
| ⚠️ | Magnetica | RACED: ERROR_EXIT28(49) vs ERROR_EXIT14(127) |
| ⚠️ | MaBoShi: The Three Shape Arcade | RACED: ERROR_EXIT14(0) vs DOWNLOAD_FAILED |
| 🟡 | World of Goo | ERROR_EXIT28 (2 errors) |
| 🟡 | Orbient | ERROR_EXIT28 (3 errors) |
| ⚠️ | Cubello | RACED: ERROR_EXIT28(3) vs ERROR_EXIT78(2642) |
| ⚠️ | Rotohex | RACED: ERROR_EXIT28(2) vs ERROR_EXIT14(2) |
| ⚠️ | PictureBook Games: Pop-Up Pursuit | RACED: ERROR_EXIT66(2) vs ERROR_EXIT28(4) |
| ⚠️ | You, Me, and the Cubes | RACED: ERROR_EXIT28(2) vs ERROR_EXIT90(1105) |
| ⚠️ | Bonsai Barber | RACED: ERROR_EXIT28(2) vs ERROR_EXIT14(94) |
| ⚠️ | WarioWare: D.I.Y. Showcase | RACED: ERROR_EXIT66(2) vs ERROR_EXIT28(4) |
| ⚠️ | Pokémon Rumble | RACED: ERROR_EXIT28(2) vs ERROR_EXIT14(2) |
| ⚠️ | Rock N' Roll Climber | RACED: ERROR_EXIT28(2) vs ERROR_EXIT78(59) |
| 🟡 | Excitebike: World Rally | ERROR_EXIT28 (3 errors) |
| 🟡 | Ultra Hand | ERROR_EXIT28 (10 errors) |
| 🟡 | Eco Shooter: Plant 530 | ERROR_EXIT28 (2 errors) |
| ⚠️ | Rotozoa | RACED: ERROR_EXIT28(3) vs ERROR_EXIT78(268) |
| 🟡 | Line Attack Heroes | ERROR_EXIT28 (2 errors) |
| ⚠️ | Snowpack Park | RACED: ERROR_EXIT28(3) vs ERROR_EXIT14(3) |
| ⚠️ | Fluidity | RACED: ERROR_EXIT66(2) vs ERROR_EXIT28(4) |

**Patterns behind the 🟡 rows** (all pre-race, so trustworthy): almost every
one is the same already-documented **`AnmTexPat` parse gap** on bundled Wii
Menu/Shop-Channel assets (2674 `ERROR #36` hits total, confirmed via Super
Smash Bros. Brawl) or the same **non-UTF-8 RARC filename** class as Twilight
Princess's `ERROR #82` (Excite Truck, Wario Land: Shake It!). `ERROR #66`
clusters (Pokémon Battle Revolution, Metroid Prime: Trilogy, Wii Play:
Motion) trace to the DS-`.srl`-passthrough and FSYS-media-passthrough sub
jobs, not yet investigated. **Mario Party 8's `CRASH_SIG10` (SIGBUS) is the
one genuinely new, unexplained bug** — confirmed against the binary that
already has the `TransformPalette` fix (`cafa058`), so it's a different,
still-open crash, not a regression of that fix.

**Next real step for this section:** re-run just the 32 ⚠️ titles one at a
time (the queue script is resumable — delete their rows from `results.tsv`
first) to get trustworthy results, especially **Kororinpa: Marble Mania**,
whose *both* racing attempts crashed with SIGBUS — that one's likely a real
bug independent of the race.

### Filetype mix per title (top 5 by DECODE/EXTRACT/DECOMPRESS count)

Almost every disc is dominated by the same bundled Wii Menu/Shop-Channel
assets — **TPL, BRLAN, BRFNT, LZ10, BRLYT** — which is exactly the content
the AnmTexPat gap above lives in; that overlap is why the bug shows up on
nearly every title regardless of genre. A few titles stand out from that
pattern and are worth remembering if a format-specific bug ever needs a
sample:
- **Mario Kart Wii** / **Metroid: Other M** / **Wario Land: Shake It!** /
  **Super Smash Bros. Brawl**: TEX-heavy (61k-94k), not TPL-heavy — texture
  format, not TPL, is their dominant content.
- **Wario Land: Shake It!**: only title with real **GFA** volume (2224) —
  the good-est GFA-format sample if that decoder needs re-checking.
- **Pandora's Tower**: heavy **MSBT** (10970) — text/message-table format,
  barely appears elsewhere.
- **Pokémon Battle Revolution**: only title with real **FSYS** volume
  (1094) — matches the FSYS-related `ERROR #66` sub-job failures noted
  above.
- **Excitebots: Trick Racing**: notable **MOD** count (2767) — 3D model
  format, uncommon elsewhere in this corpus.
- **Mario Party 8**: crashed early (only 4490 TPL / 272 MPBIN / 145 TEX
  logged before the SIGBUS) — its own **MPBIN** (party-board data) content
  hadn't even been reached in volume yet, so the crash site is most likely
  in the TPL/BRFNT/LZ10 path shared with every other title, not anything
  MPBIN-specific. Worth keeping in mind when root-causing that crash next.
- Several YAZ0-heavy titles (Super Mario Galaxy 1/2, New Play Control!
  Pikmin 2/Donkey Kong Jungle Beat, Link's Crossbow Training) — all EAD/
  first-party-in-house titles, consistent with YAZ0 being that team's
  preferred SZS-family compression over LZ10/LZ11 elsewhere.

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
  Verification against a real extracted SSBB `.chr0` in progress.
- **SRT0, CLR0, PAT0, SCN0, SHP0**: not yet implemented. Same wiring pattern
  applies (sibling `AnmTexSrt(NW4R)`/`AnmClr(NW4R)`/`AnmTexPat(NW4R)`/
  `AnmScn(NW4R)`/`AnmShp(NW4R)` folders); SHP0 (vertex morph) maps to the
  exporter's existing `MODEL_ANIM_WEIGHTS` channel type.
- **VIS0 (visibility animation)**: the tool doesn't recognize this format at
  all yet — no `file-type.c` entry, no magic/folder mapping. Needs adding as
  a new file type before animation export is even possible.
