#include "pch.h"
#include "CLIVGMRoot.h"
#include "VGMColl.h"
#include "SF2File.h"
#include "VGMSeq.h"
#include "Root.h"
#include <iostream>
#include <string>

CLIVGMRoot cliRoot;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_dir>\n";
        return 1;
    }

    std::string inFile = argv[1];
    std::string outDir = argv[2];

    cliRoot.UI_SetRootPtr(&pRoot);
    cliRoot.Init();
    cliRoot.saveDirPath = std::wstring(outDir.begin(), outDir.end());

    std::wstring wInFile(inFile.begin(), inFile.end());
    if (!cliRoot.OpenRawFile(wInFile)) {
        std::cerr << "Failed to open file: " << inFile << "\n";
        return 1;
    }

    if (cliRoot.vVGMColl.empty()) {
        std::cerr << "No collections found in file.\n";
        return 1;
    }

    for (size_t i = 0; i < cliRoot.vVGMColl.size(); ++i) {
        VGMColl* coll = cliRoot.vVGMColl[i];
        
        std::wstring name = *coll->GetName();
        std::wstring sf2filepath = cliRoot.saveDirPath + L"/" + name + L".sf2";
        SF2File *sf2file = coll->CreateSF2File();
        if (sf2file != NULL) {
            sf2file->SaveSF2File(sf2filepath);
            delete sf2file;
        }

        std::wstring midifilepath = cliRoot.saveDirPath + L"/" + name + L".mid";
        coll->seq->SaveAsMidi(midifilepath);
    }

    cliRoot.Exit();
    return 0;
}
