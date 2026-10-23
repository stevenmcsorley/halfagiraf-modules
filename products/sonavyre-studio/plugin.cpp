#include "../../src/plugin.hpp"
extern Model* modelSonavyreStudio;
Plugin* pluginInstance;
void init(Plugin* p) {pluginInstance=p;p->addModel(modelSonavyreStudio);}
