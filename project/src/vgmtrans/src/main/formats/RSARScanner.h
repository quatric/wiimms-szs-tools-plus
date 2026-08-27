#pragma once
#include "Scanner.h"

struct FileRange {
  uint32_t offset;
  uint32_t size;
};

class RSAR {
public:
  RSAR(RawFile *file_) : file(file_) {}

  RawFile *file;

  struct RBNK {
    std::string name;

    enum SampCollType {
      WAVE,
      RWAR,
    };
    SampCollType type;

    FileRange instr;
    FileRange wave;
    uint32_t waveDataOffset;
  };

  struct RSEQ {
    std::string name;
    uint32_t rseqOffset;
    uint32_t dataOffset;
    uint32_t bankIdx;
  };

  /* A single-sample "Wave" type Sound (as opposed to a Sequence).  Only the
   * first note of the first RWSD track is used -- covers the overwhelmingly
   * common one-shot-sample sound effect case; a layered/multi-note wave
   * sound only yields its first layer.
   *
   * The referenced RWSD file may or may not carry its own embedded "WAVE"
   * block of WaveInfo structs (mrst's SoundWsd::containsWaveInfo, gated on
   * the RWSD header's waveOffset field being non-zero). When it doesn't,
   * the actual sample lives in a separate RWAR wave archive at the same
   * file-group's waveData offset instead, addressed by waveIdx into that
   * archive's own TABL entry table -- the exact format RSARSampCollRWAR
   * already parses for RBNK-less sample collections. */
  struct WSD {
    std::string name;
    bool valid;
    bool useExternalRwar;
    /* useExternalRwar == false */
    uint32_t waveInfoOffset;
    uint32_t waveDataOffset;
    /* useExternalRwar == true */
    uint32_t rwarOffset;
    uint32_t waveIdx;
  };

  bool Parse();

  /* Parsed stuff. */
  std::vector<RBNK> rbnks;
  std::vector<RSEQ> rseqs;
  std::vector<WSD> wsds;

private:
  FileRange CheckBlock(uint32_t offs, const char *magic) {
    if (!file->MatchBytes(magic, offs))
      return { 0, 0 };

    uint32_t size = file->GetWordBE(offs + 0x04);
    return { offs + 0x08, size - 0x08 };
  }

  /* RSAR structure internals... */
  struct Sound {
    std::string name;
    uint32_t fileID;

    enum Type {
      SEQ = 1,
      STRM = 2,
      WAVE = 3
    };
    Type type;
    struct {
      uint32_t dataOffset;
      uint32_t bankID;
      uint32_t allocTrack;
    } seq;
    struct {
      uint32_t wsdIdx;
    } wave;
  };

  struct Bank {
    std::string name;
    uint32_t fileID;
  };

  struct GroupItem {
    uint32_t fileID;
    FileRange data;
    FileRange waveData;
  };

  struct Group {
    std::string name;
    FileRange data;
    FileRange waveData;
    std::vector<GroupItem> items;
  };

  struct File {
    uint32_t groupID;
    uint32_t index;
  };

  std::vector<std::string> stringTable;
  std::vector<Sound> soundTable;
  std::vector<Bank> bankTable;
  std::vector<File> fileTable;
  std::vector<Group> groupTable;

  std::vector<std::string> ParseSymbBlock(uint32_t blockBase);
  std::vector<Sound> ReadSoundTable(uint32_t infoBlockOffs);
  std::vector<Bank> ReadBankTable(uint32_t infoBlockOffs);
  std::vector<File> ReadFileTable(uint32_t infoBlockOffs);
  std::vector<Group> ReadGroupTable(uint32_t infoBlockOffs);

  RBNK ParseRBNK(Bank *bank);
  RSEQ ParseRSEQ(Sound *sound);
  WSD ParseWSD(Sound *sound);
};

/* Count of standalone WAVE-type sounds successfully exported as .wav during
 * the most recent Scan() -- these never produce a VGMColl (there's no
 * sequence/bank to route through the MIDI+SF2 pipeline), so a caller like
 * vgmtrans_bridge.cpp that only checks cliRoot.vVGMColl would otherwise
 * treat a WAVE-only BRSAR as a hard failure even though real audio was
 * written out. The bridge should reset this to 0 before each conversion. */
extern uint32_t g_rsarWaveSoundsExported;

class RSARScanner :
  public VGMScanner {
public:
  RSARScanner(void) {
    USE_EXTENSION(L"brsar")
  }
  virtual ~RSARScanner(void) {
  }

  virtual void Scan(RawFile *file, void *info = 0);

private:
  VGMSampColl * LoadBankSampColl(RawFile *file, RSAR::RBNK *rbnk);
};
