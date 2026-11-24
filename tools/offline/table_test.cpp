// loads every .wav under a folder the way the plugin does and renders it at 3 positions
#include "engine.h"
#include "wav.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace tb;
static void walk(const std::string&d,std::vector<std::string>&o){DIR*x=opendir(d.c_str());if(!x)return;while(dirent*e=readdir(x)){if(e->d_name[0]=='.')continue;std::string f=d+"/"+e->d_name;struct stat st;stat(f.c_str(),&st);if(S_ISDIR(st.st_mode))walk(f,o);else if(strstr(e->d_name,".wav"))o.push_back(f);}closedir(x);}
int main(int argc,char**argv){
  std::vector<std::string> files; walk(argv[1],files); std::sort(files.begin(),files.end()); int bad=0;
  for(auto&f:files){
    WavData w; if(!wavLoad(f.c_str(),w)){printf("LOAD FAIL %s\n",f.c_str());bad++;continue;}
    int fs=wavFrameSizeFromName(f.c_str()); if(!fs){printf("NO FRAME SIZE %s\n",f.c_str());bad++;continue;}
    int nf=(int)w.samples.size()/fs; std::vector<float> s=w.samples; float pk=0; for(float v:s)pk=std::max(pk,std::fabs(v)); for(float&v:s)v*=0.9f/pk;
    auto wt=std::make_shared<Wavetable>(); if(!wtBuild(*wt,s.data(),nf,fs,44100.f,(float)w.sampleRate)){printf("BUILD FAIL %s\n",f.c_str());bad++;continue;}
    float worst=0, minrms=1, cent[3]={0,0,0};
    for(int pi=0;pi<3;pi++){
      Engine e; e.setTable(0,wt); static int idx=-1; for(int i=0;i<TB_PARAM_COUNT;i++){ if(!strcmp(tb_params[i].key,"wt1_pos"))e.pots[i]=pi*63.5f; if(!strcmp(tb_params[i].key,"vca_a"))e.pots[i]=0;if(!strcmp(tb_params[i].key,"vca_s"))e.pots[i]=127;}
      uint8_t m[3]={0x90,(uint8_t)(pi==0?36:pi==1?48:72),100}; e.onMidi(m,3);
      std::vector<int16_t> b(128*2*200); for(int i=0;i<200;i++)e.renderBlock(b.data()+i*256,128);
      double sq=0; float pkk=0; bool fin=true; for(size_t i=20000;i<b.size();i+=2){float v=b[i]/32768.f;sq+=v*v;pkk=std::max(pkk,std::fabs(v));}
      float rms=std::sqrt(sq/((b.size()-20000)/2)); worst=std::max(worst,pkk); minrms=std::min(minrms,rms);
    }
    printf("%-52s frames=%d peak=%.2f minRMS=%.3f %s\n", f.substr(f.find("NorthWest/")+10).c_str(), nf, worst, minrms, (minrms<0.01f||worst>=0.999f)?"<-- CHECK":"");
    if(minrms<0.01f||worst>=0.999f)bad++;
  }
  printf("%zu files, %d problems\n",files.size(),bad); return bad?1:0; }
