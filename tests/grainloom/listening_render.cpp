// Offline listening artefacts only. No file access in the shipped DSP callback.
#include "../../src/GrainloomAdapter.hpp"
#include <cstdio>
#include <memory>
#include <vector>
#include <string>
#include <cassert>

static constexpr int rate = 48000;
static constexpr double pi = 3.14159265358979323846;
static void word(FILE* f, uint32_t v) {
  unsigned char b[4]; for(int i=0;i<4;++i)b[i]=uint8_t(v>>(8*i));
  assert(std::fwrite(b,1,4,f)==4);
}
static void wave(const std::string& path, const std::vector<grainloom::Resampler::Frame>& audio) {
  FILE* f=std::fopen(path.c_str(),"wb");assert(f);
  std::fwrite("RIFF",1,4,f);word(f,uint32_t(audio.size()*8+36));
  std::fwrite("WAVEfmt ",1,8,f);word(f,16);word(f,0x00020003);
  word(f,rate);word(f,rate*8);word(f,0x00200008);
  std::fwrite("data",1,4,f);word(f,uint32_t(audio.size()*8));
  for(auto s:audio) {
    uint32_t bits;std::memcpy(&bits,&s.l,4);word(f,bits);
    std::memcpy(&bits,&s.r,4);word(f,bits);
  }
  assert(std::fclose(f)==0);
}
static float source(int fixture, double t, uint32_t& random) {
  random=random*1664525u+1013904223u;
  float noise=float(random>>8)/8388608.f-1.f;
  if(fixture==0) {
    double beat=std::fmod(t,.5), hat=std::fmod(t,.125);
    return float(.25*std::sin(2*pi*(55*beat+6*(1-std::exp(-beat*40))))*std::exp(-beat*18)
      + .09*noise*std::exp(-hat*130) + (beat>.25?.13*noise*std::exp(-(beat-.25)*50):0));
  }
  if(fixture==2) {
    double p=std::fmod(t,.4);
    return float(.35*std::exp(-p*1000)+.12*std::sin(2*pi*880*p)*std::exp(-p*45));
  }
  return float(.12*std::sin(2*pi*110*t)+.09*std::sin(2*pi*164.813778*t)
    +.07*std::sin(2*pi*220*t)) * float(.7+.3*std::sin(t*.6)*std::sin(t*.6));
}
int main(int argc,char**argv) {
  if(argc!=3)return 2;
  // User-provided vocal, converted externally to stereo 48 kHz f32le.
  FILE* vocalFile=std::fopen(argv[2],"rb");if(!vocalFile)return 3;
  std::fseek(vocalFile,0,SEEK_END);long bytes=std::ftell(vocalFile);std::rewind(vocalFile);
  assert(bytes>0 && bytes%8==0);
  std::vector<grainloom::Resampler::Frame> vocal(size_t(bytes/8));
  assert(std::fread(vocal.data(),1,size_t(bytes),vocalFile)==size_t(bytes));std::fclose(vocalFile);
  const char* cases[9]={"drums","pad","transients","reverse","frozen","feedback","wavetable","vocals","clocked-delay"};
  const char* qualities[4]={"cassette","tape","vintage","digital"};
  for(int fixture=0;fixture<9;++fixture) {
    int frames=rate*(fixture==6?18:12);
    std::vector<grainloom::Resampler::Frame> input(frames),output(frames);
    uint32_t random=0x12345678;
    for(int i=0;i<frames;++i) {
      double t=i/double(rate);
      float s=source(fixture,t,random);
      input[i].l=s;input[i].r=s*.85f;
      if(fixture==7)input[i]=vocal[size_t(i)%vocal.size()];
      if(fixture==6)input[i]=grainloom::Resampler::Frame();
    }
    wave(std::string(argv[1])+"/"+cases[fixture]+"-dry.wav",input);
    for(int quality=0;quality<4;++quality) {
      std::unique_ptr<grainloom::Adapter> a(new grainloom::Adapter());
      auto& c=a->controls;c.quality=quality;c.gain=64;
      c.leftConnected=c.rightConnected=fixture!=6;
      c.mix=.8f;c.reverb=.25f;c.density=.25f;c.size=.68f;c.shape=.55f;
      c.time=.25f;c.random[0]=.25f;c.random[1]=.04f;
      if(fixture==3)c.size=.25f;
      if(fixture==4){c.reverb=.6f;c.size=.75f;}
      if(fixture==5){c.feedback=.92f;c.reverb=.7f;c.mix=.95f;}
      if(fixture==6){c.mix=.65f;c.feedback=.4f;c.random[1]=.08f;}
      if(fixture==8){c.size=1.f;c.seedConnected=true;c.feedback=.5f;c.random[0]=0.f;}
      double energy=0;float peak=0;int limit=0;
      for(int i=0;i<frames;++i) {
        if(fixture==4)c.freezeButton=i==rate*4;
        float clock=fixture==8 && i%(rate/2)<240 ? 10.f:0.f;
        output[i]=a->process(input[i].l,input[i].r,clock,0);
        auto s=output[i];assert(std::isfinite(s.l)&&std::isfinite(s.r));
        peak=std::max(peak,std::max(std::fabs(s.l),std::fabs(s.r)));
        energy+=double(s.l)*s.l+double(s.r)*s.r;
        if(std::fabs(s.l)>.999f||std::fabs(s.r)>.999f)++limit;
      }
      assert(energy>0);
      std::string name=std::string(cases[fixture])+"-"+qualities[quality]+".wav";
      wave(std::string(argv[1])+"/"+name,output);
      std::printf("%s peak=%.8f rms=%.8f near_limit_frames=%d\n",name.c_str(),peak,
        std::sqrt(energy/(2*frames)),limit);
    }
  }
}
