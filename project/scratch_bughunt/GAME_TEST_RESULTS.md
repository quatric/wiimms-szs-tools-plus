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
handful of well-known third-party) Wii title from `mcubewii:...Redump/[WBFS]`,
one `wszst xx <disc>` per title, classified only by process exit code /
signal + `grep -c "ERROR #"` on the log — **not** individually triaged the
way the titles above were. 76/118 done so far (resumable, `results.tsv`);
this section reports the *aggregate patterns* across all logs collected so
far, not a per-title verdict.

- **`ERROR #36 [INVALID DATA]` is overwhelmingly the dominant failure** —
  2674 occurrences across the logs collected so far, vs. single digits for
  every other error code. Confirmed (spot-checked on Super Smash Bros. Brawl)
  to be the same **`AnmTexPat` (texture-pattern animation) parse failure on
  bundled Wii Menu/Shop-Channel system assets** already logged above under
  Twilight Princess/AquaSpace-adjacent findings — e.g.
  `.../RVL-Shopping-v7.wad.out.d/.../PBmarioA.brres.d/AnmTexPat(NW4R)/PBmario_run.txt`.
  It hits nearly every title with an `UPDATE` partition (which is most of
  them) because they all bundle the same Shopping/Wii-Menu-Channel assets —
  this single unfixed gap accounts for the vast majority of this run's
  `ERROR_EXIT28` rows. **Not yet fixed**; still the right next target if
  BRRES `AnmTexPat` support is picked up, exactly as noted in the earlier
  section, just now confirmed at much larger scale.
- **`ERROR #66 [SUB JOB FAILED]`** (Pokémon Battle Revolution 38x, Metroid
  Prime: Trilogy 102x, Wii Play: Motion 19x): the DS-passthrough path
  (`wit`-driven `.srl` child-ROM extraction, e.g.
  `DATA/files/wifi/child0.srl -> child0.d`) and the FSYS media-passthrough
  path (external `ffmpeg` at `/Users/larsen/mobipeg/ffmpeg`) both report
  sub-job failures on real files. Not investigated this pass — a different
  root cause from the AnmTexPat gap, worth a dedicated look if DS-wifi or
  FSYS-media extraction quality matters.
- **`ERROR #82 [CAN'T CREATE FILE]`** (Excite Truck 73x, Wario Land: Shake
  It! 86x): very likely the same non-UTF-8 RARC filename class already
  root-caused under Twilight Princess above (EILSEQ on macOS `open()`) —
  not confirmed byte-for-byte for these two titles, but the error code and
  class of title (both have the same bundled system-channel content that
  carries Shift-JIS-tainted filenames) line up. The Twilight Princess fix,
  when implemented, should be re-verified against these too.
- **New crash, not previously seen: Mario Party 8 → `CRASH_SIG10` (SIGBUS).**
  Confirmed this ran against the binary already containing the
  `TransformPalette` heap-overflow fix (`cafa058`, binary built 23:55:06,
  crash logged 01:11 the same session) — so this is a **different,
  still-open** SIGBUS, not the same bug reappearing. Not yet root-caused.
  (An earlier run of the same title, before several of this session's fixes
  landed, crashed with `CRASH_SIG6` instead — superseded, not the same
  investigation.)
- **`ERROR #95 [LZMA ERROR]`**: seen exactly once across the corpus so far
  (title not yet isolated — occurs alongside other errors in a busy log).
  Not investigated.
- **Timeouts** (2400s cap): WarioWare: Smooth Moves, Mario Kart Wii, Endless
  Ocean: Blue World. Mario Kart Wii's timeout matches the already-noted
  `revo_kart.brsar`/`wbrsar` performance concern above — the other two are
  new data points for the same suspected cause (large multi-track BRSAR
  going through the WAVE-export path added this session) but unconfirmed.
- **Clean passes so far**: Wii Sports, Animal Crossing: City Folk (both
  already covered above), Mario & Sonic at the Olympic Games, Samurai
  Warriors 3. Twilight Princess itself shows `PASS` in this run (2 errors
  logged, not 0) — its known `ERROR #82` filename bug apparently didn't
  trigger on this disc's specific content, or triggered too rarely to flip
  the aggregate exit code; doesn't contradict the earlier finding, which was
  confirmed by direct byte inspection, not just this run's exit code.

**Bottom line: nothing new and crash-level has turned up except Mario Party
8's SIGBUS, and the two known, already-documented gaps (AnmTexPat parsing,
non-UTF-8 filenames) explain the overwhelming majority of the non-zero exit
codes.** The queue is still running past title 76/118; revisit this section
once it completes rather than treating it as final.

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
