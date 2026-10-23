#include "../../src/sonavyre/presets/Library.hpp"
#include <cstdio>
Plugin* pluginInstance=nullptr;
int main(int argc,char** argv){
 if(argc!=2)return 10;asset::userDir=argv[1];
 SonavyreLibrary a;auto state=json_object();json_object_set_new(state,"test",json_integer(42));
 if(a.add("Test sound",state)!=0){std::puts(a.error.c_str());return 1;}json_decref(state);
 SonavyreLibrary b;if(b.name(0)!="Test sound"||json_integer_value(json_object_get(b.state(0),"test"))!=42)return 2;
 b.rename(0,"Renamed");b.toggleFavourite(3);SonavyreLibrary c;if(c.name(0)!="Renamed"||!c.favourite(3))return 3;
 auto duplicate=json_deep_copy(c.state(0));c.add("Copy",duplicate);json_decref(duplicate);c.remove(0);
 SonavyreLibrary d;if(json_array_size(d.presets())!=1||d.name(0)!="Copy")return 4;
 std::puts("PASS: disk-backed preset create, reload in new instance, rename, favourites, duplicate and delete.");
}
