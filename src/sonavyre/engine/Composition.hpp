#pragma once
#include "../core/FV1.hpp"
#include <atomic>

// Fixed storage: all VMs are constructed off the audio thread. No feedback routes.
struct SonavyreComposition {
 struct Stage {
  FV1 vm;
  std::atomic<bool> enabled{false};
  std::atomic<int> program{0};
  std::atomic<float> wet{1.f};
  std::atomic<float> pots[3];
  int loaded = -1;
  float phase=0, prevL=0,prevR=0, outL=0,outR=0,oldL=0,oldR=0;
  Stage(){for(auto& p:pots)p.store(.5f);}
  void run(float& l,float& r,float dt,const uint32_t bank[][128],int n,float clock) {
   if(!enabled.load(std::memory_order_relaxed)) return;
   int p=std::max(0,std::min(n-1,program.load(std::memory_order_relaxed)));
   if(p!=loaded){vm.loadProgram(bank[p]);loaded=p;phase=prevL=prevR=outL=outR=oldL=oldR=0;}
   float inL=l/5.f,inR=r/5.f,ratio=clock*dt;
   phase+=ratio;
   while(phase>=1.f){
    phase-=1.f;
    float f=std::max(0.f,std::min(1.f,1.f-phase/ratio));
    oldL=outL;oldR=outR;
    vm.run(prevL+(inL-prevL)*f,prevR+(inR-prevR)*f,pots[0].load(),pots[1].load(),pots[2].load(),outL,outR);
   }
   prevL=inL;prevR=inR;
   float f=std::max(0.f,std::min(1.f,phase/ratio));
   float a=(oldL+(outL-oldL)*f)*5.f,b=(oldR+(outR-oldR)*f)*5.f;
   if(!std::isfinite(a)||!std::isfinite(b)){vm.clearState();a=b=0;}
   float mix=wet.load(std::memory_order_relaxed);
   l=l*(1-mix)+a*mix;r=r*(1-mix)+b*mix;
  }
 } stages[2];
 std::atomic<bool> parallel{false}, bypass{false};
 std::atomic<float> mix{1.f}, gain{1.f};
 void invalidate(){for(auto& s:stages)s.loaded=-1;}
 void process(float& l,float& r,float dt,const uint32_t bank[][128],int n,float clock){
  if(bypass.load()||(!stages[0].enabled.load()&&!stages[1].enabled.load()))return;
  float dryL=l,dryR=r;
  if(parallel.load()) {
   float a=l,b=r,c=l,d=r;int count=0;
   if(stages[0].enabled.load()){stages[0].run(a,b,dt,bank,n,clock);count++;}else a=b=0;
   if(stages[1].enabled.load()){stages[1].run(c,d,dt,bank,n,clock);count++;}else c=d=0;
   if(count){l=(a+c)/count;r=(b+d)/count;}
  }else for(auto& s:stages)s.run(l,r,dt,bank,n,clock);
  float w=mix.load(),g=gain.load();
  // Guard only the combination path; original single-effect output is untouched.
  l=dryL*(1-w)+std::max(-20.f,std::min(20.f,l*g))*w;
  r=dryR*(1-w)+std::max(-20.f,std::min(20.f,r*g))*w;
 }
 json_t* toJson(){
  json_t* j=json_object();
  json_object_set_new(j,"parallel",json_boolean(parallel.load()));json_object_set_new(j,"bypass",json_boolean(bypass.load()));
  json_object_set_new(j,"mix",json_real(mix.load()));json_object_set_new(j,"gain",json_real(gain.load()));
  json_t* a=json_array();for(auto& s:stages){json_t* x=json_object();
   json_object_set_new(x,"enabled",json_boolean(s.enabled.load()));json_object_set_new(x,"program",json_integer(s.program.load()));json_object_set_new(x,"wet",json_real(s.wet.load()));
   json_t* ps=json_array();for(auto& p:s.pots)json_array_append_new(ps,json_real(p.load()));json_object_set_new(x,"pots",ps);json_array_append_new(a,x);
  }json_object_set_new(j,"stages",a);return j;
 }
 void fromJson(json_t* j){
  if(!json_is_object(j))return;
  parallel.store(json_is_true(json_object_get(j,"parallel")));bypass.store(json_is_true(json_object_get(j,"bypass")));
  auto number=[](json_t* x,float fallback,float lo,float hi){double v=json_is_number(x)?json_number_value(x):fallback;return float(std::isfinite(v)?std::max(double(lo),std::min(double(hi),v)):fallback);};
  mix.store(number(json_object_get(j,"mix"),1,0,1));gain.store(number(json_object_get(j,"gain"),1,0,2));
  auto a=json_object_get(j,"stages");for(int i=0;i<2;i++){auto x=json_array_get(a,i);if(!json_is_object(x))continue;auto& s=stages[i];
   s.enabled.store(json_is_true(json_object_get(x,"enabled")));s.program.store(int(number(json_object_get(x,"program"),0,0,199)));s.wet.store(number(json_object_get(x,"wet"),1,0,1));
   auto ps=json_object_get(x,"pots");for(int c=0;c<3;c++)s.pots[c].store(number(json_array_get(ps,c),.5,0,1));
  }
 }
};
