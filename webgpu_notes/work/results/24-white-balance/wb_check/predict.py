from model import *
def s2l(c): c=c/255; return c/12.92 if c<=0.04045 else ((c+0.055)/1.055)**2.4
def l2s(v): v=max(0,min(1,v)); return round(255*(12.92*v if v<=0.0031308 else 1.055*v**(1/2.4)-0.055))
card=np.array([s2l(187.6)]*3)  # 6500 K measured card, in linear
patch=np.array([1.0,0.5,0.25])
for T,t in [(6500,0),(3200,0),(10000,0),(6500,0.5),(6500,-0.5)]:
    m=M(T,t)
    print(T,t,'card',[l2s(v) for v in m@card],'patch',[l2s(v) for v in m@patch])
