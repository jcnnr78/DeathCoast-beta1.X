#!/bin/bash
# old engine (fase1, forced to Mono) vs new NorthWest v1.1 engine: must be bit-identical
B="wt1_level=100"
cases=(
"default|||0"
"unison+filter+sub|wt1_uni=4,wt1_detune=60,wt1_spread=80,flt_freq=70,flt_res=60,sub_level=60,flt_type=5,wt2_level=100,wt2_uni=3,wt2_uni_mode=2||0"
"fm mode|wt1_fx_mode=1,wt1_fx1=70,wt1_fx2=48,vca_d=90,vca_s=0||0"
"sync + portamento|wt1_fx_mode=2,wt1_fx1=40,wt1_fx2=30,glide_mode=2,glide=60||0"
"sync + portamento legato line|wt1_fx_mode=2,wt1_fx1=40,glide_mode=2,glide=60||1"
"fold + split filter|wt2_level=90,wt2_fx_mode=3,wt2_fx1=30,wt2_fx2=25,flt_route=2,flt2_type=4,flt2_freq=85,flt_env=110,flt_d=55,flt_s=0||0"
"bend+formant -> warp|wt1_bend=127,wt1_formant=127|wt1_fx_mode=4,wt1_fx1=127,wt1_fx2=127|0"
"bend only -> warp|wt2_level=80,wt2_bend=127|wt2_level=80,wt2_fx_mode=4,wt2_fx1=127|0"
"matrix lfo->cutoff (dst 13->9)|mm1_src=3,mm1_dst=13,mm1_amt=100,lfo1_rate=90,flt_freq=60|mm1_src=3,mm1_dst=9,mm1_amt=100,lfo1_rate=90,flt_freq=60|0"
"EG1 own dst -> matrix|wt1_fx_mode=1,wt1_fx1=40,me1_dst=19,me1_amt=110,me1_d=50|wt1_fx_mode=1,wt1_fx1=40,mm1_src=1,mm1_dst=15,mm1_amt=110,me1_d=50|0"
"legato switch -> glide LEGATO|legato=1,glide_mode=2,glide=60|glide_mode=3,glide=60|1"
"glissando line|glide_mode=1,glide=80||1"
"lfo sync s&h + filter2 parallel|lfo1_shape=5,lfo1_sync=1,mm1_src=3,mm1_dst=1,mm1_amt=127,flt2_type=2,flt_route=1||0"
)
fail=0
for c in "${cases[@]}"; do
  IFS='|' read -r name po pn seq <<< "$c"
  [ -z "$pn" ] && pn="$po"
  ./seq_orig a.raw "voice_mode=1${po:+,$po}" "$seq" || { echo "ORIG ERR $name"; fail=1; continue; }
  ./seq_new  b.raw "$pn" "$seq" || { echo "NEW ERR $name"; fail=1; continue; }
  if cmp -s a.raw b.raw; then r=IDENTICAL; else r=DIFFERENT; fail=1; fi
  printf "%-36s %s  (rms %s)\n" "$name" "$r" "$(python3 -I -c "
import array,math;a=array.array('h',open('b.raw','rb').read());print(round(math.sqrt(sum(x*x for x in a)/len(a))/32768,4))")"
done
exit $fail
