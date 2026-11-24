import numpy as np,subprocess,sys
def render(p,name="t.raw"):
    r=subprocess.run(["./render_wt",name,p],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    a=np.frombuffer(open(name,'rb').read(),dtype='<i2').astype(np.float32)/32768
    return a[0::2],a[1::2]
def stats(p):
    l,r=render(p); x=l[:44100*2]
    ok=np.isfinite(l).all() and np.isfinite(r).all()
    spec=np.abs(np.fft.rfft(x[22050:22050+16384]*np.hanning(16384)))
    f=np.fft.rfftfreq(16384,1/44100)
    cent=(spec*f).sum()/max(spec.sum(),1e-9)
    return dict(finite=ok,peak=float(np.abs(l).max()),rms=float(np.sqrt((x**2).mean())),centroid=float(cent))
base="wt1_level=100,flt_freq=127,vca_a=0,voices=1"
tests={
 "default":base,
 "FM amt 0":base+",wt1_fx_mode=1,wt1_fx1=0",
 "FM amt 60":base+",wt1_fx_mode=1,wt1_fx1=60,wt1_fx2=32",
 "FM amt 110":base+",wt1_fx_mode=1,wt1_fx1=110,wt1_fx2=64",
 "Classic sync 80":base+",wt1_fx_mode=2,wt1_fx1=80",
 "Classic width 100":base+",wt1_fx_mode=2,wt1_fx2=100",
 "Modern asym 100":base+",wt1_fx_mode=3,wt1_fx1=100",
 "Modern fold 100":base+",wt1_fx_mode=3,wt1_fx2=100",
 "uni classic 4":base+",wt1_uni=4,wt1_detune=60",
 "uni shimmer":base+",wt1_uni=4,wt1_detune=60,wt1_uni_mode=1",
 "uni noise":base+",wt1_uni=4,wt1_detune=100,wt1_uni_mode=2",
 "uni phasesync":base+",wt1_uni=4,wt1_detune=60,wt1_uni_mode=3",
 "uni posspread":base+",wt1_uni=4,wt1_detune=100,wt1_uni_mode=4,wt1_pos=64",
 "uni random 6":base+",wt1_uni=6,wt1_detune=100,wt1_uni_mode=5",
 "flt2 off LP":base+",flt_freq=60,flt_type=1",
 "flt2 serial HP":base+",flt_freq=90,flt_type=1,flt2_type=3,flt2_freq=70,flt_route=0",
 "flt2 parallel":base+",flt_freq=60,flt_type=1,flt2_type=3,flt2_freq=100,flt_route=1",
 "flt2 split":base+",wt2_level=100,flt_freq=60,flt2_type=3,flt2_freq=100,flt_route=2",
 "lfo1->pitch":base+",mm1_src=3,mm1_dst=18,mm1_amt=70,lfo1_rate=70",
 "lfo1->wt pos":base+",mm1_src=3,mm1_dst=1,mm1_amt=127,lfo1_rate=60",
 "lfo sync->cutoff":base+",flt_freq=50,mm1_src=3,mm1_dst=13,mm1_amt=100,lfo1_sync=1,lfo1_rate=60,lfo1_shape=2",
 "modwheel(0)->pitch":base+",mm1_src=7,mm1_dst=18,mm1_amt=127",
 "vel->level":base+",mm1_src=5,mm1_dst=3,mm1_amt=127",
 "key->cutoff":base+",flt_freq=60,mm1_src=6,mm1_dst=13,mm1_amt=100",
 "all8 voice stress":"wt1_uni=6,wt2_uni=6,wt2_level=100,wt1_fx_mode=1,wt1_fx1=90,flt2_type=2,flt_route=1,lfo1_rate=90,mm1_src=3,mm1_dst=13,mm1_amt=90,me1_dst=19,me1_amt=100",
}
for k,p in tests.items(): print(f"{k:22s}",stats(p))
