#pragma once
#include "../../plugin.hpp"
#include <cstdio>

// UI-thread-only persistent library. Factory data never enters the editable array.
struct SonavyreLibrary {
 json_t* root=nullptr;
 std::string error;
 std::string directory(){return asset::user("halfagiraf/sonavyre");}
 std::string path(){return directory()+"/library.json";}
 SonavyreLibrary(){reload();}
 ~SonavyreLibrary(){json_decref(root);}
 void reload(){
  json_error_t e;json_t* j=json_load_file(path().c_str(),0,&e);
  if(j && (!json_is_object(j)||!json_is_array(json_object_get(j,"presets")))){json_decref(j);j=nullptr;error="Invalid preset library; original file retained";}
  if(!j){if(root)return;j=json_object();json_object_set_new(j,"schema",json_integer(1));json_object_set_new(j,"presets",json_array());json_object_set_new(j,"favourites",json_array());}
  json_decref(root);root=j;
 }
 json_t* presets(){return json_object_get(root,"presets");}
 bool save(){
  if(!error.empty())return false;
  system::createDirectories(directory());auto tmp=path()+".tmp";
  if(json_dump_file(root,tmp.c_str(),JSON_INDENT(2))!=0||!system::rename(tmp,path())){error="Could not save preset library";return false;}return true;
 }
 int add(const std::string& name,json_t* state){
  reload();if(!error.empty())return -1;
  auto p=json_object();json_object_set_new(p,"name",json_string(name.c_str()));json_object_set(p,"state",state);json_array_append_new(presets(),p);save();return int(json_array_size(presets()))-1;
 }
 std::string name(size_t i){auto v=json_object_get(json_array_get(presets(),i),"name");return json_is_string(v)?json_string_value(v):"Untitled";}
 json_t* state(size_t i){return json_object_get(json_array_get(presets(),i),"state");}
 void rename(size_t i,const std::string& name){reload();auto p=json_array_get(presets(),i);if(p){json_object_set_new(p,"name",json_string(name.c_str()));save();}}
 void remove(size_t i){reload();json_array_remove(presets(),i);save();}
 bool favourite(int p){auto a=json_object_get(root,"favourites");for(size_t i=0;i<json_array_size(a);i++)if(json_integer_value(json_array_get(a,i))==p)return true;return false;}
 void toggleFavourite(int p){reload();auto a=json_object_get(root,"favourites");if(!json_is_array(a)){a=json_array();json_object_set_new(root,"favourites",a);}for(size_t i=0;i<json_array_size(a);i++)if(json_integer_value(json_array_get(a,i))==p){json_array_remove(a,i);save();return;}json_array_append_new(a,json_integer(p));save();}
};
