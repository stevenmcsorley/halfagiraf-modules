#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <jansson.h>
namespace baseline {
#include "../../../upstream-halfagiraf-vcv/src/FV1.hpp"
}
#include "../../src/sonavyre/core/FV1.hpp"
#include "../../src/sonavyre/presets/OriginalBank.hpp"
#include "../../src/sonavyre/engine/Composition.hpp"
int main(){
 auto a=std::unique_ptr<baseline::FV1>(new baseline::FV1);
 auto b=std::unique_ptr<FV1>(new FV1);
 for(int p=0;p<8;p++)for(int setting=0;setting<3;setting++){
  a->loadProgram(FXAID_DEFAULT_BANK[p]);b->loadProgram(FXAID_DEFAULT_BANK[p]);
  for(int i=0;i<32768;i++){
   float inL=i==0?.5f:.1f*std::sin(i*.031f),inR=.1f*std::cos(i*.043f);
   float al,ar,bl,br,v=setting*.5f;
   a->run(inL,inR,v,v,v,al,ar);b->run(inL,inR,v,v,v,bl,br);
   if(std::memcmp(&al,&bl,4)||std::memcmp(&ar,&br,4)){std::fprintf(stderr,"Mismatch %d %d %d\n",p,setting,i);return 1;}
  }
 }
 auto c=std::unique_ptr<SonavyreComposition>(new SonavyreComposition);
 float l=.3f,r=-.2f;c->process(l,r,1.f/48000,FXAID_DEFAULT_BANK,8,32768);
 if(l!=.3f||r!=-.2f)return 2;
 for(bool parallel:{false,true}){
  c->parallel.store(parallel);for(auto& s:c->stages){s.enabled.store(true);s.program.store(2);}
  double energy=0;
  for(int i=0;i<96000;i++){l=i==0?1:0;r=0;c->process(l,r,1.f/48000,FXAID_DEFAULT_BANK,8,32768);if(!std::isfinite(l)||!std::isfinite(r))return 3;energy+=l*l+r*r;}
  if(energy==0)return 4;
 }
 json_t* state=c->toJson();auto d=std::unique_ptr<SonavyreComposition>(new SonavyreComposition);d->fromJson(state);json_t* restored=d->toJson();if(!json_equal(state,restored))return 5;json_decref(state);json_decref(restored);
 std::puts("PASS: 786432 stereo VM frames bit-identical; disabled-chain identity; serial/parallel impulse output finite and nonzero; composition JSON round trip.");
}
