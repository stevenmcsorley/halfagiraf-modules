#pragma once
#include "../engine/Engine.hpp"
#include <chrono>

// UI-only illustration of measured output energy. No invented grain/FFT state.
// Work is bounded: 128 atomic sample reads, 18 ribbons and at most 96 particles.
struct SonavyreEnergyField {
 float phase = 0.f, envelope = 0.f;
 std::chrono::steady_clock::time_point previous = std::chrono::steady_clock::now();
 void draw(NVGcontext* vg, SonavyreEngine* module, float x, float y, float w, float h) {
  const auto now = std::chrono::steady_clock::now();
  const float dt = clamp(std::chrono::duration<float>(now-previous).count(), 0.f, .1f);
  previous = now;
  const bool animate=module && module->animate;
  float samples[128] = {}, sum = 0.f;
  const unsigned end = module ? module->scopeCursor.load(std::memory_order_acquire) : 0;
  if (animate) for (unsigned i=0;i<128;i++) {
   float v = module->scope[(end+i)%128].load(std::memory_order_relaxed);
   samples[i] = std::isfinite(v) ? clamp(v,-2.f,2.f) : 0.f;
   sum += samples[i]*samples[i];
  }
  const float rms=std::sqrt(sum/128.f);
  const float stereoRms=animate ? std::sqrt(std::max(0.f,module->outputPower.load(std::memory_order_relaxed))) : 0.f;
  // Extra low-level sensitivity with a soft knee, so louder input still has
  // room to move rather than pinning the field at maximum activity.
  const float level=clamp(std::max(rms,stereoRms)*3.f*(module?module->visualSensitivity:2.5f),0.f,100.f);
  const float energy=std::sqrt(level/(1.f+level));
  envelope = animate ? envelope+(energy-envelope)*(1.f-std::exp(-dt*(energy>envelope?28.f:5.f))) : 0.f;
  if (animate) phase += dt*(.15f+envelope*1.2f);
  const float gain=rms>.00001f ? std::min(36.f,.32f/rms) : 0.f;
  const NVGcolor colors[]={nvgRGB(55,215,226),nvgRGB(120,110,235),nvgRGB(204,132,218),nvgRGB(220,184,117)};
  nvgSave(vg); nvgScissor(vg,x,y,w,h);
  // The stationary idle field is dim; signal adds height, motion and illumination.
  for (int strand=0;strand<18;strand++) {
   const float f=strand/17.f;
   NVGcolor color=nvgLerpRGBA(colors[strand/6],colors[std::min(3,strand/6+1)],(strand%6)/6.f);
   color.a=.16f+envelope*.46f;
   nvgBeginPath(vg);
   for (int i=0;i<72;i++) {
    const float u=i/71.f, taper=std::sin(u*M_PI);
    const float audio=samples[std::min(127,int(u*127))]*gain;
    const float weave=std::sin(u*7.4f-phase*1.7f+f*3.f)*(.04f+envelope*.23f);
    const float fold=std::cos(u*3.4f+phase+f*4.6f)*(.02f+envelope*.13f);
    const float yy=y+h*(.5f+taper*(weave+fold+(f-.5f)*(.12f+envelope*.38f)+audio*.42f*envelope));
    if(i==0)nvgMoveTo(vg,x+u*w,yy);else nvgLineTo(vg,x+u*w,yy);
   }
   nvgStrokeWidth(vg,strand%6==0?1.05f:.55f);nvgStrokeColor(vg,color);nvgStroke(vg);
  }
  if (animate && envelope>.025f) for(int i=0;i<96;i++) {
   const float seed=std::fmod(i*.6180339887f,1.f);
   const float u=std::fmod(seed+phase*(.012f+.015f*(i%3)),1.f);
   const float spread=(std::fmod(i*.381966f,1.f)-.5f);
   const float yy=.5f+std::sin(u*7.4f-phase*1.7f+spread*3.f)*(.04f+envelope*.23f)+spread*.5f;
   NVGcolor c=colors[(i/24)%4];c.a=envelope*(.22f+.65f*std::sin(u*M_PI));
   nvgBeginPath(vg);nvgCircle(vg,x+u*w,y+clamp(yy,.03f,.97f)*h,.45f+.65f*seed);nvgFillColor(vg,c);nvgFill(vg);
  }
  nvgRestore(vg);
 }
};
