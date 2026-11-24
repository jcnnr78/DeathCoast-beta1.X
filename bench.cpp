#include "engine.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace tb;
static void set(Engine&e,const char*k,float v){for(int i=0;i<TB_PARAM_COUNT;i++)if(!strcmp(k,tb_params[i].key))e.pots[i]=v;}
int main(int argc,char**argv){
  const int heavy=argc>1&&argv[1][0]=='h';
  Engine e; if(heavy){ set(e,"wt1_uni",6);set(e,"wt2_uni",6);set(e,"wt2_level",100);set(e,"wt1_fx_mode",1);set(e,"wt1_fx1",90);set(e,"wt2_fx_mode",2);set(e,"wt2_fx1",60);set(e,"flt2_type",2);set(e,"flt_route",1);set(e,"mm1_src",3);set(e,"mm1_dst",9);set(e,"mm1_amt",90);set(e,"sub_level",50);set(e,"noise_level",30);}
  else { set(e,"wt2_level",100); set(e,"wt1_uni",2); set(e,"wt2_uni",2);}
  set(e,"vca_s",127); set(e,"vca_a",0);
  for(int n=40;n<41;n++){uint8_t m[3]={0x90,(uint8_t)n,100};e.onMidi(m,3);}
  std::vector<int16_t> b(128*2); int blocks=44100*4/128;
  auto t0=std::chrono::steady_clock::now();
  for(int i=0;i<blocks;i++) e.renderBlock(b.data(),128);
  double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  printf("%s: 1 mono voice, 4 s audio in %.3f s -> %.1f%% of one x86 core\n",heavy?"heavy":"typical",sec,sec/4*100);
}
