import numpy as np
def planck_uv(T):
    T2=T*T
    u=(0.860117757+1.54118254e-4*T+1.28641212e-7*T2)/(1+8.42420235e-4*T+7.08145163e-7*T2)
    v=(0.317398726+4.22806245e-5*T+4.20481691e-8*T2)/(1-2.89741816e-5*T+1.61456053e-7*T2)
    return np.array([u,v])
def white_uv(T,tint):
    uv=planck_uv(T)
    d=planck_uv(T+1)-planck_uv(T-1)
    n=np.array([d[1],-d[0]]); n/=np.linalg.norm(n)   # points to +v (green)
    return uv + n*tint*0.05
def uv_to_xyz(uv):
    u,v=uv; d=2*u-8*v+4; x=3*u/d; y=2*v/d
    return np.array([x/y,1.0,(1-x-y)/y])
SRGB_TO_XYZ=np.array([[0.4124564,0.3575761,0.1804375],[0.2126729,0.7151522,0.0721750],[0.0193339,0.1191920,0.9503041]])
BRAD=np.array([[0.8951,0.2664,-0.1614],[-0.7502,1.7135,0.0367],[0.0389,-0.0685,1.0296]])
RGB2LMS=BRAD@SRGB_TO_XYZ
LMS2RGB=np.linalg.inv(RGB2LMS)
def gains(T,tint):
    ref=BRAD@uv_to_xyz(white_uv(6500,0)); src=BRAD@uv_to_xyz(white_uv(T,tint))
    return ref/src
def M(T,tint): return LMS2RGB@np.diag(gains(T,tint))@RGB2LMS
np.set_printoptions(precision=9,suppress=True)
print("RGB2LMS\n",RGB2LMS); print("LMS2RGB\n",LMS2RGB)
for T,t in [(6500,0),(3200,0),(10000,0),(6500,0.5),(6500,-0.5),(2000,0),(15000,0)]:
    m=M(T,t); print(T,t,"gains",gains(T,t),"grey(0.18)->",m@np.array([.18,.18,.18]))
# light at T under WB T vs light at 6500 under default
def light_rgb(T):
    xyz=uv_to_xyz(planck_uv(T)); rgb=np.linalg.inv(SRGB_TO_XYZ)@xyz; rgb/=rgb.max(); return np.clip(rgb,0,None)
for T in [3200,10000]:
    a=M(T,0)@light_rgb(T); b=light_rgb(6500); print("light",T,"WB",T,a/a.max(),"vs 6500",b/b.max())
