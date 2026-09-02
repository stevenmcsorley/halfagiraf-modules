#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelBadSector);
	p->addModel(modelMOD1);
	p->addModel(modelEntwine);
	p->addModel(modelConstellate);
}
