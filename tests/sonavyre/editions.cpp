#include "../../src/sonavyre/engine/Engine.hpp"
#include "../../src/sonavyre/presets/OriginalBank.hpp"
#include "../../src/sonavyre/presets/CompleteBank.hpp"
#include <memory>
#include <cstdio>
Plugin* pluginInstance=nullptr;
int main(){
 auto m=std::unique_ptr<SonavyreEngine>(new SonavyreEngine);
 if(m->nSlots!=PRO_N||SONAVYRE_COMBINATIONS)return 1;
 bool black=false,plate=false;
 for(int p=0;p<PRO_N;p++) {
  int global=sonavyreFactoryId(p);
  const uint32_t* expected=global<8?FXAID_DEFAULT_BANK[global]:FXAIDPRO_DEFAULT_BANK[global-8];
  if(std::memcmp(m->bank[p],expected,512))return 2;
  black|=m->slotNames[p]=="Reverb Black Hole";plate|=m->slotNames[p]=="Reverb Plate Classic";
  m->params[SonavyreEngine::PROG_PARAM].setValue(p);
  for(int i=0;i<1024;i++) {
   m->inputs[SonavyreEngine::L_INPUT].setVoltage(i==0?5.f:0.f);
   m->process(Module::ProcessArgs{48000.f,1.f/48000.f,i});
   if(m->prog!=p||!std::isfinite(m->outputs[0].getVoltage()))return 3;
  }
 }
 if(!black||!plate)return 4;
 // A full-edition preset must resolve by catalogue identity, not subset position.
 auto j=json_object();json_object_set_new(j,"prog",json_integer(132));
 m->dataFromJson(j);if(sonavyreFactoryId(m->prog)!=132)return 5;
 json_decref(j);
 j=m->dataToJson();m->dataFromJson(j);json_decref(j);
 if(sonavyreFactoryId(m->prog)!=132)return 6;
 // Curated builds must reject arbitrary-bank and combination payloads.
 j=json_object();json_object_set_new(j,"customBank",json_true());
 json_object_set_new(j,"nSlots",json_integer(246));
 m->chain.stages[0].enabled.store(true);m->dataFromJson(j);json_decref(j);
 if(m->nSlots!=PRO_N||m->customBank||m->chain.stages[0].enabled.load())return 7;
 m->onReset();if(m->nSlots!=PRO_N||m->prog!=0)return 8;
 std::printf("PASS: edition %d: %d exact factory binaries, reverbs, audio/navigation, stable preset identities, bank/composition boundaries and reset.\n",SONAVYRE_EDITION,PRO_N);
}
