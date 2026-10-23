#include "../../src/sonavyre/engine/Engine.hpp"
Plugin* pluginInstance=nullptr;
// The comparison module is compiled from the untouched checkout, not shipped.
namespace original {
Plugin* pluginInstance=nullptr;
#include "../../../upstream-halfagiraf-vcv/src/FXAidPro.cpp"
}
#include <cstdio>
#include <memory>
int main(){
 auto migrated=std::unique_ptr<SonavyreEngine>(new SonavyreEngine);
 auto reference=std::unique_ptr<original::FXAidPro>(new original::FXAidPro);
 for(int p=0;p<8;p++)std::memcpy(reference->bank[p],migrated->bank[p],sizeof(migrated->bank[p]));
 reference->nSlots=8;
 int frames=0;
 for(float rate:{44100.f,48000.f,96000.f})for(int p=0;p<8;p++){
  migrated->setProgram(p);reference->setProgram(p);
  for(auto* m:std::vector<Module*>{migrated.get(),reference.get()}){
   m->params[SonavyreEngine::PROG_PARAM].setValue(p);m->params[SonavyreEngine::SRR_PARAM].setValue(.7f);
   for(auto& in:m->inputs)in.setChannels(1);
   for(auto& out:m->outputs)out.setChannels(1);
  }
  for(int i=0;i<8192;i++){
   for(auto* m:std::vector<Module*>{migrated.get(),reference.get()}){
    m->inputs[SonavyreEngine::L_INPUT].setVoltage(.4f*std::sin(i*.04f));m->inputs[SonavyreEngine::R_INPUT].setVoltage(i==0?1.f:0.f);
    m->inputs[SonavyreEngine::CV1_INPUT].setVoltage(.3f*std::sin(i*.002f));
    m->process(Module::ProcessArgs{rate,1.f/rate,frames});
   }
   for(int c=0;c<2;c++)if(migrated->outputs[c].getVoltage()!=reference->outputs[c].getVoltage()){std::fprintf(stderr,"Host mismatch rate %g program %d frame %d\n",rate,p,i);return 1;}
   frames++;
  }
 }
 migrated->params[0].setValue(.23f);migrated->savePreset(0);migrated->params[0].setValue(.9f);migrated->recallPreset(0);
 if(migrated->params[0].getValue()!=.23f)return 2;
 auto j=migrated->dataToJson();auto restored=std::unique_ptr<SonavyreEngine>(new SonavyreEngine);restored->dataFromJson(j);json_decref(j);
 if(!restored->presets[0].used||restored->presets[0].pots[0]!=.23f)return 3;
 restored->onReset();if(!restored->presets[0].used||restored->nSlots!=8)return 4;
 std::printf("PASS: %d host stereo frames exactly match original advanced processing at 44.1/48/96 kHz with CV and reduced clock; user preset recall, patch data and non-destructive reset.\n",frames);
}
