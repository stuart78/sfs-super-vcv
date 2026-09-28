#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelTwoOpFM);
	p->addModel(modelPhrsr);
	p->addModel(modelSvfs);
	p->addModel(modelEg);
	p->addModel(modelVcas);
	p->addModel(modelScanner);
	p->addModel(modelTvca);
	p->addModel(modelChorus);
	p->addModel(modelRoom);
	p->addModel(modelOtavcas);
	p->addModel(modelSnh);
	p->addModel(modelPngbl);
}
