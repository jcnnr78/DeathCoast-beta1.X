// offline renderer: ./render out.raw "key=val,key=val" [notes]  -> writes int16 stereo raw @44100
#include "engine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
using namespace nw;
int main(int argc,char**argv){
  srand(1234);
  Engine e;
  if(argc>2){ std::string s=argv[2]; size_t p=0; while(p<s.size()){ size_t q=s.find(',',p); if(q==std::string::npos)q=s.size(); std::string kv=s.substr(p,q-p); p=q+1; size_t eq=kv.find('='); if(eq==std::string::npos)continue; std::string k=kv.substr(0,eq); float v=atof(kv.c_str()+eq+1); bool ok=false; for(int i=0;i<NW_PARAM_COUNT;i++) if(k==nw_params[i].key){e.pots[i]=v;ok=true;} if(!ok){fprintf(stderr,"unknown param %s\n",k.c_str());return 2;} } }
  int frames=44100*3; std::vector<int16_t> buf((size_t)frames*2);
  uint8_t on1[3]={0x90,48,100},on2[3]={0x90,55,100},on3[3]={0x90,60,100},off[3]={0x80,48,0},off2[3]={0x80,55,0},off3[3]={0x80,60,0};
  int pos=0; auto blk=[&](int n){ e.renderBlock(buf.data()+pos*2,n); pos+=n; };
  e.onTrackMidi(on1,3); e.onTrackMidi(on2,3); e.onTrackMidi(on3,3); blk(44100*2);
  e.onTrackMidi(off,3); e.onTrackMidi(off2,3); e.onTrackMidi(off3,3); blk(frames-pos);
  FILE*f=fopen(argc>1?argv[1]:"out.raw","wb"); fwrite(buf.data(),2,buf.size(),f); fclose(f); return 0; }
