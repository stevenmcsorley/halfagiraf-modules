#pragma once
#include <osdialog.h>
#include "../engine/Engine.hpp"
#include "../presets/Library.hpp"
#include "../visualisations/EnergyField.hpp"

struct SonavyrePort : SvgPort {SonavyrePort(){setSvg(Svg::load(asset::plugin(pluginInstance,"res/sonavyre/components/input.svg")));}};
struct SonavyreOutputPort : SvgPort {SonavyreOutputPort(){setSvg(Svg::load(asset::plugin(pluginInstance,"res/sonavyre/components/output.svg")));}};
struct SonavyreScrew : SvgScrew {SonavyreScrew(){setSvg(Svg::load(asset::plugin(pluginInstance,"res/sonavyre/components/screw.svg")));}};
struct SonavyreButton : SvgSwitch {SonavyreButton(){momentary=true;auto s=Svg::load(asset::plugin(pluginInstance,"res/sonavyre/components/button.svg"));addFrame(s);addFrame(s);}};

struct SonavyreKnob : SvgKnob {
 SonavyreKnob() {
  setSvg(Svg::load(asset::plugin(pluginInstance, "res/sonavyre/components/knob.svg")));
  minAngle = -0.83f * M_PI; maxAngle = 0.83f * M_PI;
 }
 void drawLayer(const DrawArgs& a, int layer) override {
  SvgKnob::drawLayer(a, layer);
  if (layer != 1 || !getParamQuantity()) return;
  float angle = minAngle + (maxAngle-minAngle)*getParamQuantity()->getScaledValue();
  nvgBeginPath(a.vg);
  nvgArc(a.vg, box.size.x/2, box.size.y/2, box.size.x/2+3, minAngle-M_PI/2, angle-M_PI/2, NVG_CW);
  nvgStrokeWidth(a.vg, 1.5); nvgStrokeColor(a.vg, nvgRGB(72,215,222)); nvgStroke(a.vg);
 }
};

struct SonavyreDisplay : widget::OpaqueWidget {
 SonavyreEngine* module = nullptr;
 SonavyreEnergyField energyField;
 bool studio=false;
 std::function<void(int)> openCategory;
 std::function<void()> openBrowser;
 void onButton(const ButtonEvent& e) override {
  if(e.action==GLFW_PRESS && e.button==GLFW_MOUSE_BUTTON_LEFT){
   if(studio && e.pos.x<88 && openCategory){openCategory(clamp(int((e.pos.y-8)/16),0,7));e.consume(this);return;}
   if(studio && e.pos.y>=109 && e.pos.y<129 && module){
    for(int stage=0;stage<2;stage++){
     float x=160+stage*(box.size.x-168)/2, width=(box.size.x-178)/2;
     if(e.pos.x>=x && e.pos.x<x+width){auto& v=module->chain.stages[stage].enabled;v.store(!v.load());e.consume(this);return;}
    }
   }
   if(openBrowser){openBrowser();e.consume(this);}
  }
 }
 void drawLayer(const DrawArgs& a, int layer) override {
  if (layer != 1) return;
  auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
  if (!font) return;
  nvgSave(a.vg); nvgScissor(a.vg, 0, 0, box.size.x, box.size.y);
  nvgFontFaceId(a.vg, font->handle);
  auto label = [&](float x, float y, float size, NVGcolor color, const std::string& text) {
   nvgFontSize(a.vg,size); nvgFillColor(a.vg,color); nvgTextAlign(a.vg,NVG_ALIGN_LEFT|NVG_ALIGN_BASELINE);
   nvgText(a.vg,x,y,text.c_str(),nullptr);
  };
  NVGcolor cyan=nvgRGB(85,225,230), dim=nvgRGB(120,148,165), gold=nvgRGB(213,185,135);
  int p=module ? clamp(module->prog,0,module->nSlots-1) : 0;
  if(studio){
   const char* cats[]={"Factory","User","Favourites","Reverb","Delay","Modulation","Pitch","Big reverbs"};
   for(int i=0;i<8;i++)label(7,18+i*16,9,i<3 ? gold:dim,cats[i]);
   nvgBeginPath(a.vg);nvgMoveTo(a.vg,89,6);nvgLineTo(a.vg,89,box.size.y-5);nvgStrokeColor(a.vg,nvgRGB(35,61,73));nvgStrokeWidth(a.vg,1);nvgStroke(a.vg);
   label(101,17,8,gold,module&&module->customBank ? "IMPORTED PROGRAM" : "FACTORY / 246 EFFECTS");
   label(101,36,14,cyan,module?module->slotNames[p]:"Room Reverb");
   energyField.draw(a.vg,module,101,43,box.size.x-135,45);
   for(int ch=0;ch<2;ch++){
    float v=module?std::fabs((ch?module->meterR:module->meterL).load())/10.f:0;
    for(int seg=0;seg<12;seg++){nvgBeginPath(a.vg);nvgRect(a.vg,box.size.x-23+ch*10,82-seg*3,6,2);nvgFillColor(a.vg,v>seg/12.f ? (seg>8?gold:cyan):nvgRGB(20,40,48));nvgFill(a.vg);}
   }
   for(int c=0;c<3;c++)label(101+c*(box.size.x-110)/3,98,8,dim,module?module->slotLabels[p][c]:std::vector<std::string>{"Decay","Damping","Pre-Delay"}[c]);
   label(101,124,8,dim,module&&module->chain.parallel.load()?"PARALLEL":"SERIAL");
   for(int i=0;i<2;i++){
    float x=160+i*(box.size.x-168)/2;
    bool on=module&&module->chain.stages[i].enabled.load();
    nvgBeginPath(a.vg);nvgRoundedRect(a.vg,x,109,(box.size.x-178)/2,20,3);nvgStrokeColor(a.vg,on?cyan:dim);nvgStrokeWidth(a.vg,1);nvgStroke(a.vg);
    std::string stageName=on&&module?module->slotNames[clamp(module->chain.stages[i].program.load(),0,module->nSlots-1)]:string::f("STAGE %d OFF",i+1);
    float fontSize=8.f;nvgFontSize(a.vg,fontSize);float textWidth=nvgTextBounds(a.vg,0,0,stageName.c_str(),nullptr,nullptr);
    float available=(box.size.x-178)/2-10;if(textWidth>available)fontSize*=available/textWidth;
    label(x+5,122,fontSize,on?cyan:dim,stageName);
   }
   nvgRestore(a.vg);return;
  }
  label(8,15,9,gold,module && module->customBank ? "IMPORTED BANK" : "FACTORY / 246 EFFECTS");
  label(8,32,16,cyan,module ? module->slotNames[p] : "Room Reverb");
  label(8,45,8,dim,"CLICK TO BROWSE / EDIT PRESETS");
  energyField.draw(a.vg,module,8,47,box.size.x-16,37);
  for(int c=0;c<3;c++)label(8+c*(box.size.x-16)/3,94,9,dim,module?module->slotLabels[p][c]:std::vector<std::string>{"Decay","Damping","Pre-Delay"}[c]);
  nvgRestore(a.vg);
 }
};

struct SonavyreParameterLabels : widget::Widget {
 SonavyreEngine* module=nullptr;
 bool pro=false;
 void drawLayer(const DrawArgs& a,int layer) override {
  if(layer!=1)return;
  auto font=APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));if(!font)return;
  nvgFontFaceId(a.vg,font->handle);nvgTextAlign(a.vg,NVG_ALIGN_CENTER|NVG_ALIGN_BASELINE);
  const float w=pro?142.24f:91.44f;
  int p=module?clamp(module->prog,0,module->nSlots-1):0;
  const char* defaults[]={"Decay","Damping","Pre-Delay"};
  for(int c=0;c<3;c++){
   const std::string name=module?module->slotLabels[p][c]:defaults[c];
   float x=pro?w*(.375f+.25f*c):(c==0?w*.5f:(c==1?w*.26f:w*.74f));
   float y=pro?67.5f:(c==0?54.f:69.5f);
   float size=7.3f;nvgFontSize(a.vg,size);float width=nvgTextBounds(a.vg,0,0,name.c_str(),nullptr,nullptr);
   if(width>mm2px(pro?28.f:19.f)){size*=mm2px(pro?28.f:19.f)/width;nvgFontSize(a.vg,size);}
   nvgFillColor(a.vg,nvgRGB(180,202,214));nvgText(a.vg,mm2px(x),mm2px(y),name.c_str(),nullptr);
  }
 }
};

struct SonavyreWidget : ModuleWidget {
 SonavyreLibrary library;
 std::string activePreset;
 json_t* savedState=nullptr;
 SonavyreEngine* engine;
 bool pro;
 SonavyreWidget(SonavyreEngine* m,bool isPro=false):engine(m),pro(isPro){
  setModule(m);
  setPanel(createPanel(asset::plugin(pluginInstance,pro?"res/sonavyre/panels/SonavyrePro.svg":"res/sonavyre/panels/Sonavyre.svg")));
  float w=pro?142.24f:91.44f;
  for(float x:{2.5f,w-2.5f})for(float y:{2.5f,125.5f})addChild(createWidgetCentered<SonavyreScrew>(mm2px(Vec(x,y))));
  auto d=createWidget<SonavyreDisplay>(mm2px(Vec(6,17)));d->box.size=mm2px(Vec(w-12,pro?45:34));d->module=m;d->studio=pro;
  d->openBrowser=[this](){auto menu=createMenu();appendContextMenu(menu);};
  d->openCategory=[this](int c){auto menu=createMenu();if(c==1)presetMenu(menu);else effectMenu(menu,c);};addChild(d);
  if(pro){
   addParam(createParamCentered<SonavyreKnob>(mm2px(Vec(w*.125f,77)),m,SonavyreEngine::DW_PARAM));
   for(int c=0;c<3;c++)addParam(createParamCentered<SonavyreKnob>(mm2px(Vec(w*(.375f+.25f*c),77)),m,SonavyreEngine::KNOB1_PARAM+c));
   int ids[]={SonavyreEngine::INGAIN_PARAM,SonavyreEngine::PROG_PARAM,SonavyreEngine::SRR_PARAM};
   for(int c=0;c<3;c++)addParam(createParamCentered<SonavyreKnob>(mm2px(Vec(w*(.25f+.25f*c),99.5f)),m,ids[c]));
   int ins[]={SonavyreEngine::L_INPUT,SonavyreEngine::R_INPUT,SonavyreEngine::CV1_INPUT,SonavyreEngine::CV2_INPUT,SonavyreEngine::CV3_INPUT,SonavyreEngine::SRR_CV_INPUT,SonavyreEngine::DW_CV_INPUT};
   for(int c=0;c<7;c++)addInput(createInputCentered<SonavyrePort>(mm2px(Vec(8+c*(w-16)/8,116.5f)),m,ins[c]));
   for(int c=0;c<2;c++)addOutput(createOutputCentered<SonavyreOutputPort>(mm2px(Vec(8+(c+7)*(w-16)/8,116.5f)),m,SonavyreEngine::L_OUTPUT+c));
  }else{
   for(int c=0;c<3;c++) {
    float x=(c==0 ? w*.5f : (c==1 ? w*.26f : w*.74f));float y=c==0 ? 65.f : 81.f;
    addParam(createParamCentered<SonavyreKnob>(mm2px(Vec(x,y)),m,SonavyreEngine::KNOB1_PARAM+c));
    addInput(createInputCentered<SonavyrePort>(mm2px(Vec(c==0?8.f:(c==1?w*.5f:w-8.f),c==1?83.f:80.f)),m,SonavyreEngine::CV1_INPUT+c));
   }
   int ids[]={SonavyreEngine::INGAIN_PARAM,SonavyreEngine::PROG_PARAM,SonavyreEngine::DW_PARAM,SonavyreEngine::SRR_PARAM};
   for(int c=0;c<4;c++)addParam(createParamCentered<SonavyreKnob>(mm2px(Vec(w*(.13f+c*(.74f/3)),99.5f)),m,ids[c]));
   float x[]={.09f,.21f,.39f,.51f,.72f,.84f};
   int ins[]={SonavyreEngine::L_INPUT,SonavyreEngine::R_INPUT,SonavyreEngine::SRR_CV_INPUT,SonavyreEngine::DW_CV_INPUT};
   for(int c=0;c<4;c++)addInput(createInputCentered<SonavyrePort>(mm2px(Vec(w*x[c],116.5f)),m,ins[c]));
   for(int c=0;c<2;c++)addOutput(createOutputCentered<SonavyreOutputPort>(mm2px(Vec(w*x[c+4],116.5f)),m,SonavyreEngine::L_OUTPUT+c));
  }
  auto labels=new SonavyreParameterLabels;labels->module=m;labels->pro=pro;labels->box.size=box.size;addChild(labels);
  // Program navigation and LFO restart.
  addParam(createParamCentered<SonavyreButton>(mm2px(Vec(pro?6.5f:8.f,60.f+(pro?8.f:0.f))),m,SonavyreEngine::PREV_PARAM));
  addParam(createParamCentered<SonavyreButton>(mm2px(Vec(w-(pro?6.5f:8.f),60.f+(pro?8.f:0.f))),m,SonavyreEngine::NEXT_PARAM));
  addParam(createParamCentered<SonavyreButton>(mm2px(Vec(pro?w-8.f:w*.5f,pro?96.f:91.f)),m,SonavyreEngine::LFO_PARAM));

 }
 ~SonavyreWidget(){json_decref(savedState);}
 struct NameField : ui::TextField {
  std::function<void(std::string)> accept;
  void onSelectKey(const SelectKeyEvent& e) override {
   if(e.action==GLFW_PRESS && e.key==GLFW_KEY_ENTER){if(accept&&!text.empty())accept(text);e.consume(this);return;}
   ui::TextField::onSelectKey(e);
  }
 };
 void promptName(Menu* menu,const std::string& initial,std::function<void(std::string)> fn){
  auto f=new NameField;f->box.size=Vec(230,25);f->placeholder="Name, then Enter";f->setText(initial);f->accept=fn;menu->addChild(f);
 }
 json_t* capture(){
  auto j=APP->engine->moduleToJson(engine);json_object_del(j,"id");return j;
 }
 void recall(json_t* state,const std::string& name){
  if(!json_is_object(state))return;
  auto target=json_deep_copy(state);
  // Development editions share state; adapt Rack's product identifiers only.
  json_object_set_new(target,"plugin",json_string(engine->model->plugin->slug.c_str()));
  json_object_set_new(target,"model",json_string(engine->model->slug.c_str()));
  auto h=new history::ModuleChange;h->name="recall SONAVYRE preset";h->moduleId=engine->id;h->oldModuleJ=capture();
  APP->engine->moduleFromJson(engine,target);json_decref(target);
  h->newModuleJ=capture();APP->history->push(h);
  activePreset=name;json_decref(savedState);savedState=capture();
 }
 struct SearchField : ui::TextField {
  std::vector<std::pair<std::string,MenuItem*>> items;
  void onChange(const ChangeEvent& e) override {
   ui::TextField::onChange(e);std::string q=text;std::transform(q.begin(),q.end(),q.begin(),[](unsigned char c){return std::tolower(c);});
   for(auto& i:items)i.second->visible=i.first.find(q)!=std::string::npos;
  }
 };
 void effectMenu(Menu* menu,int category=0){
  auto search=new SearchField;search->placeholder="Search effects";search->box.size=Vec(240,25);menu->addChild(search);
  for(int p=0;p<engine->nSlots;p++){
   auto name=engine->slotNames[p];std::string lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
   if(category==2&&!library.favourite(p))continue;
   if(category==3&&lower.find("reverb")==std::string::npos)continue;
   if(category==4&&lower.find("delay")==std::string::npos)continue;
   if(category==5&&lower.find("chorus")==std::string::npos&&lower.find("flanger")==std::string::npos&&lower.find("tremolo")==std::string::npos)continue;
   if(category==6&&lower.find("pitch")==std::string::npos)continue;
   if(category==7){
    bool big=lower.find("reverb")!=std::string::npos && (lower.find("black")!=std::string::npos||lower.find("cloud")!=std::string::npos||lower.find("infinite")!=std::string::npos||lower.find("hall")!=std::string::npos||lower.find("shimmer")!=std::string::npos||lower.find("space")!=std::string::npos);
    if(!big)continue;
   }
   auto item=createMenuItem(name,"",[=](){engine->params[SonavyreEngine::PROG_PARAM].setValue(p);});menu->addChild(item);search->items.push_back({lower,item});
  }
 }
 void presetMenu(Menu* menu){
  library.reload();
  if(!library.error.empty())menu->addChild(createMenuLabel(library.error));
  menu->addChild(createSubmenuItem("Save As / Create", "",[=](Menu* sm){promptName(sm,"",[=](std::string name){auto j=capture();library.add(name,j);json_decref(j);activePreset=name;json_decref(savedState);savedState=capture();});}));
  for(size_t i=0;i<json_array_size(library.presets());i++){
   auto name=library.name(i);
   menu->addChild(createSubmenuItem(name,"",[=](Menu* sm){
    sm->addChild(createMenuItem("Recall","",[=](){recall(library.state(i),name);}));
    sm->addChild(createSubmenuItem("Rename","",[=](Menu* nm){promptName(nm,name,[=](std::string n){library.rename(i,n);});}));
    sm->addChild(createMenuItem("Duplicate","",[=](){auto j=json_deep_copy(library.state(i));library.add(name+" copy",j);json_decref(j);}));
    sm->addChild(createMenuItem("Save edits","",[=](){auto j=capture();library.reload();auto p=json_array_get(library.presets(),i);json_object_set(p,"state",j);library.save();json_decref(j);}));
    sm->addChild(createSubmenuItem("Delete...","",[=](Menu* confirm){confirm->addChild(createMenuItem("Confirm delete user preset","",[=](){library.remove(i);}));}));
    sm->addChild(createMenuItem("Export...","",[=](){char* path=osdialog_file(OSDIALOG_SAVE,nullptr,"sonavyre-preset.json",nullptr);if(path){auto j=json_object();json_object_set_new(j,"format",json_string("sonavyre-preset-v1"));json_object_set_new(j,"name",json_string(name.c_str()));json_object_set(j,"state",library.state(i));json_dump_file(j,path,JSON_INDENT(2));json_decref(j);std::free(path);}}));
   }));
  }
  menu->addChild(createMenuItem("Import preset...","",[=](){
   char* path=osdialog_file(OSDIALOG_OPEN,nullptr,nullptr,nullptr);if(!path)return;
   json_error_t e;auto j=json_load_file(path,0,&e);std::free(path);
   auto f=json_object_get(j,"format"),n=json_object_get(j,"name"),st=json_object_get(j,"state");
   if(json_is_string(f)&&std::string(json_string_value(f))=="sonavyre-preset-v1"&&json_is_string(n)&&json_is_object(st))library.add(json_string_value(n),st);
   json_decref(j);
  }));
 }
 struct FloatSlider : ui::Slider {
  struct Q : Quantity { float* v; Q(float* p):v(p) {} void setValue(float x) override {*v=clamp(x,0.f,1.f);} float getValue() override{return *v;} float getDefaultValue() override{return .3f;} };
  FloatSlider(float* v) {quantity=new Q(v);box.size.x=180;}
  ~FloatSlider(){delete quantity;}
 };
 struct AtomicSlider : ui::Slider {
  struct Q : Quantity { std::atomic<float>* v; Q(std::atomic<float>* p):v(p){} void setValue(float x) override{v->store(clamp(x,0.f,1.f));} float getValue() override{return v->load();} float getDefaultValue() override{return .5f;} };
  AtomicSlider(std::atomic<float>* p){quantity=new Q(p);box.size.x=180;}
  ~AtomicSlider(){delete quantity;}
 };
 void appendContextMenu(Menu* menu) override {
  auto m=engine;
  if(!m) return;
  menu->addChild(new MenuSeparator);
  menu->addChild(createMenuLabel(pro ? "SONAVYRE PRO / Development" : "SONAVYRE / Development"));
  menu->addChild(createSubmenuItem("Preset library", "",[this](Menu* sm){presetMenu(sm);}));
  menu->addChild(createCheckMenuItem("Favourite current factory effect","",[=](){return library.favourite(m->prog);},[=](){library.toggleFavourite(m->prog);}));
  menu->addChild(createSubmenuItem("Effects", "",[this](Menu* sm){effectMenu(sm);}));
  menu->addChild(createSubmenuItem("Big reverbs", "",[this](Menu* sm){effectMenu(sm,7);}));
  menu->addChild(createIndexSubmenuItem("Clock CV routing",{"Sample rate","Parameter 1","Parameter 2","Parameter 3","Program","LFO restart"},[=](){return m->srrRoute;},[=](int v){m->srrRoute=v;}));
  menu->addChild(createSubmenuItem("Internal LFOs", "", [=](Menu* sm) {
   for(int l=0;l<3;l++) sm->addChild(createSubmenuItem(string::f("LFO %d",l+1),"",[=](Menu* lm){
    lm->addChild(createIndexSubmenuItem("Shape",{"Off","Triangle","Ramp","Saw","Sample and hold","Smooth random","Sine","Square"},[=](){return m->lfo[l].shape;},[=](int v){m->lfo[l].shape=v;}));
    lm->addChild(createIndexSubmenuItem("Target",{"Off","Parameter 1","Parameter 2","Parameter 3","Clock"},[=](){return m->lfo[l].target;},[=](int v){m->lfo[l].target=v;}));
    lm->addChild(createMenuLabel("Rate")); lm->addChild(new FloatSlider(&m->lfo[l].rate));
    lm->addChild(createMenuLabel("Depth")); lm->addChild(new FloatSlider(&m->lfo[l].depth));
   }));
  }));
  menu->addChild(createSubmenuItem("User presets (stored in patch)","",[=](Menu* sm){
   for(int i=0;i<10;i++) sm->addChild(createSubmenuItem(string::f("Preset %d%s",i+1,m->presets[i].used ? " *" : ""),"",[=](Menu* pm){
    pm->addChild(createMenuItem("Save", "",[=](){m->savePreset(i);}));
    pm->addChild(createMenuItem("Recall", "",[=](){m->recallPreset(i);},!m->presets[i].used));
   }));
  }));
  menu->addChild(createSubmenuItem("Processing combination", "",[=](Menu* cm){
   cm->addChild(createCheckMenuItem("Parallel routing", "",[=](){return m->chain.parallel.load();},[=](){m->chain.parallel.store(!m->chain.parallel.load());}));
   cm->addChild(createCheckMenuItem("Bypass combination", "",[=](){return m->chain.bypass.load();},[=](){m->chain.bypass.store(!m->chain.bypass.load());}));
   cm->addChild(createMenuLabel("Combination mix"));cm->addChild(new AtomicSlider(&m->chain.mix));
   for(int i=0;i<2;i++)cm->addChild(createSubmenuItem(string::f("Stage %d",i+1),"",[=](Menu* sm){
    auto st=&m->chain.stages[i];
    sm->addChild(createCheckMenuItem("Enabled", "",[=](){return st->enabled.load();},[=](){st->enabled.store(!st->enabled.load());}));
    sm->addChild(createSubmenuItem("Algorithm", "",[=](Menu* pm){for(int p=0;p<m->nSlots;p++)pm->addChild(createMenuItem(m->slotNames[p],"",[=](){st->program.store(p);}));}));
    for(int c=0;c<3;c++){sm->addChild(createMenuLabel(string::f("Parameter %d",c+1)));sm->addChild(new AtomicSlider(&st->pots[c]));}
    sm->addChild(createMenuLabel("Stage wet/dry"));sm->addChild(new AtomicSlider(&st->wet));
   }));
  }));
  menu->addChild(createMenuItem("Reset parameters", "",[=](){for(int c=0;c<3;c++)m->params[SonavyreEngine::KNOB1_PARAM+c].setValue(.5f);m->params[SonavyreEngine::DW_PARAM].setValue(.5f);m->params[SonavyreEngine::INGAIN_PARAM].setValue(.5f);m->params[SonavyreEngine::SRR_PARAM].setValue(1.f);}));
  menu->addChild(createIndexSubmenuItem("Visual sensitivity",{"Normal","Enhanced (default)","High","Very high"},
   [=](){float v=m->visualSensitivity;return v<2.f?0:(v<4.f?1:(v<8.f?2:3));},
   [=](int index){const float levels[]={1.f,2.5f,5.f,10.f};m->visualSensitivity=levels[clamp(index,0,3)];}));
  menu->addChild(createCheckMenuItem("Audio visualisation","",[=](){return m->animate;},[=](){m->animate=!m->animate;}));
 }
};
