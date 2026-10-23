#include "../../src/sonavyre/engine/Engine.hpp"
Plugin* pluginInstance=nullptr;
// The comparison host is normalized from the hash-verified untouched checkout, not shipped.
namespace original {
Plugin* pluginInstance=nullptr;
#include "../../build/sonavyre-reference/Host.cpp"
}
#include <cstdio>
#include <memory>
int main(){
 auto migrated=std::unique_ptr<SonavyreEngine>(new SonavyreEngine);
 auto reference=std::unique_ptr<original::ReferenceHost>(new original::ReferenceHost);
 for(int p=0;p<8;p++)std::memcpy(reference->bank[p],migrated->bank[p],sizeof(migrated->bank[p]));
 reference->nSlots=8;
 if(migrated->nSlots!=246)return 5;
 for(int p=0;p<200;p++){
  if(std::memcmp(migrated->bank[p+8],original::SONAVYRE_COMPLETE_BANK[p],512))return 6;
  if(migrated->slotNames[p+8]!=original::SONAVYRE_COMPLETE_NAMES[p])return 7;
  for(int c=0;c<3;c++)if(migrated->slotLabels[p+8][c]!=original::SONAVYRE_COMPLETE_LABELS[p][c])return 8;
 }
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
 // Verify the complete added bank through the original host, not only the VM.
 for(int p=0;p<200;p++)std::memcpy(reference->bank[p],original::SONAVYRE_COMPLETE_BANK[p],512);
 reference->nSlots=200;
 for(float rate:{44100.f,48000.f,96000.f})for(int p=0;p<200;p++){
  migrated->setProgram(p+8);reference->setProgram(p);
  migrated->params[SonavyreEngine::PROG_PARAM].setValue(p+8);reference->params[original::ReferenceHost::PROG_PARAM].setValue(p);
  for(int i=0;i<512;i++){
   for(auto* m:std::vector<Module*>{migrated.get(),reference.get()}){
    m->inputs[SonavyreEngine::L_INPUT].setVoltage(i==0?1.f:.2f*std::sin(i*.04f));m->inputs[SonavyreEngine::R_INPUT].setVoltage(.1f*std::cos(i*.07f));
    m->process(Module::ProcessArgs{rate,1.f/rate,frames});
   }
   for(int c=0;c<2;c++)if(migrated->outputs[c].getVoltage()!=reference->outputs[c].getVoltage()){std::fprintf(stderr,"Complete-bank host mismatch %d %d\n",p,i);return 9;}
   frames++;
  }
 }
 for(float rate:{44100.f,48000.f,96000.f})for(int p=0;p<38;p++){
  std::memcpy(reference->bank[0],SONAVYRE_EXTRA_BANK[p],512);
  migrated->setProgram(p+208);reference->setProgram(0);
  migrated->params[SonavyreEngine::PROG_PARAM].setValue(p+208);reference->params[original::ReferenceHost::PROG_PARAM].setValue(0);
  for(int i=0;i<512;i++){
   for(auto* m:std::vector<Module*>{migrated.get(),reference.get()}){
    m->inputs[SonavyreEngine::L_INPUT].setVoltage(i==0?1.f:.2f*std::sin(i*.04f));m->inputs[SonavyreEngine::R_INPUT].setVoltage(.1f*std::cos(i*.07f));
    m->process(Module::ProcessArgs{rate,1.f/rate,frames});
   }
   for(int c=0;c<2;c++)if(migrated->outputs[c].getVoltage()!=reference->outputs[c].getVoltage())return 10;
   frames++;
  }
 }
 migrated->params[0].setValue(.23f);migrated->savePreset(0);migrated->params[0].setValue(.9f);migrated->recallPreset(0);
 if(migrated->params[0].getValue()!=.23f)return 2;
 auto j=migrated->dataToJson();auto restored=std::unique_ptr<SonavyreEngine>(new SonavyreEngine);restored->dataFromJson(j);json_decref(j);
 if(!restored->presets[0].used||restored->presets[0].pots[0]!=.23f)return 3;
 restored->onReset();if(!restored->presets[0].used||restored->nSlots!=246)return 4;
 // Holding both navigation buttons must leave audio processing and the bank intact.
 int selected=restored->prog;
 restored->params[SonavyreEngine::PROG_PARAM].setValue(selected);
 restored->params[SonavyreEngine::PREV_PARAM].setValue(1.f);
 restored->params[SonavyreEngine::NEXT_PARAM].setValue(1.f);
 for(int i=0;i<60000;i++) {
  restored->process(Module::ProcessArgs{48000.f,1.f/48000.f,i});
  if(restored->prog!=selected||restored->nSlots!=246||!std::isfinite(restored->outputs[0].getVoltage()))return 11;
 }
 restored->params[SonavyreEngine::PREV_PARAM].setValue(0.f);
 restored->params[SonavyreEngine::NEXT_PARAM].setValue(0.f);
 restored->process(Module::ProcessArgs{48000.f,1.f/48000.f,60000});
 restored->params[SonavyreEngine::NEXT_PARAM].setValue(1.f);
 restored->process(Module::ProcessArgs{48000.f,1.f/48000.f,60001});
 if(restored->prog!=(selected+1)%246)return 12;
 std::printf("PASS: %d host stereo frames exactly match original advanced processing at 44.1/48/96 kHz with CV and reduced clock; user preset recall, patch data and non-destructive reset.\n",frames);
}
