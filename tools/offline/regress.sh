set -e
P=(
""
"wt2_level=100,wt1_uni=4,wt1_detune=60,wt1_spread=80,flt_freq=70,flt_res=60,me1_dst=1,me1_amt=110,wt1_bend=90,wt2_formant=30,sub_level=60,flt_type=5"
"wt1_uni=3,wt2_level=90,wt2_uni=2,wt2_detune=100,vca_a=0,flt_type=2,flt_freq=40,flt_env=120,me2_dst=13,me2_amt=100,me2_a=30,voice_mode=1,glide_mode=2,glide=60"
"wt1_pos=64,wt2_pos=100,wt2_level=80,wt1_formant=100,wt2_bend=20,flt_type=7,flt_freq=90,flt_key=100,flt_vel=80,voices=3"
)
i=0; fail=0
for p in "${P[@]}"; do
  ./render_orig a.raw "$p"; ./render_wt b.raw "$p"
  if cmp -s a.raw b.raw; then echo "patch $i: IDENTICAL"; else echo "patch $i: DIFFERENT"; fail=1; fi
  i=$((i+1))
done
exit $fail
