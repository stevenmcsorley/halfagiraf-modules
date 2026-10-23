#include "../../src/plugin.hpp"
Plugin* pluginInstance;
void init(Plugin* p) {pluginInstance=p;p->addModel(modelSonavyrePro);}
