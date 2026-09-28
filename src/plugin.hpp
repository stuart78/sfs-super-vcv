#pragma once
#include <rack.hpp>

using namespace rack;

extern Plugin* pluginInstance;

// One per module, defined in each module's source file. See docs/porting-plan.md
// for the library and the order they are ported in.
extern Model* modelTwoOpFM;
extern Model* modelPhrsr;
extern Model* modelSvfs;
extern Model* modelEg;
extern Model* modelVcas;
extern Model* modelScanner;
extern Model* modelTvca;
extern Model* modelChorus;
extern Model* modelRoom;
extern Model* modelOtavcas;
extern Model* modelSnh;
extern Model* modelPngbl;
