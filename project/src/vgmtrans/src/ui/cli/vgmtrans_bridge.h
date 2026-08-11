#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Converts a game audio bank (e.g. Wii BRSAR) to MIDI + SF2 files written
// into outDir, using the vgmtrans format scanners and SF2/MIDI exporters.
// Returns 0 on success, non-zero on failure. No subprocess is spawned;
// this call runs entirely in-process.
int VgmtransConvertFile(const char *inFile, const char *outDir);

#ifdef __cplusplus
}
#endif
