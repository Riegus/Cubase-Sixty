# Makes Cubase-Sixty's app icon: the tray icon's design (cubase/bridge/src/tray/TrayApp.cpp, makeIcon) as a .ico in
# its "live" green, 16-256 px, plus a preview PNG. Needs Pillow.
#   python cubase/tools/make-app-icon.py cubase/bridge/src/tray/CubaseSixty.ico preview.png
import sys
from PIL import Image
def in_round(x,y,x0,y0,x1,y1,r):
    if x<x0 or x>x1 or y<y0 or y>y1: return False
    cx = x0+r if x<x0+r else (x1-r if x>x1-r else x)
    cy = y0+r if y<y0+r else (y1-r if y>y1-r else y)
    return (x-cx)**2+(y-cy)**2 <= r*r
def render(size, rgb):
    panel=(rgb[0]/255,rgb[1]/255,rgb[2]/255,1); rim=(panel[0]*.5,panel[1]*.5,panel[2]*.5,1)
    slot=(.12,.12,.12,1); cap=(1,1,1,1)
    sx_=[4.5,8.0,11.5]; cy_=[9.5,5.0,7.5]
    def sample(u,v):
        c=(0,0,0,0)
        if in_round(u,v,.5,1,15.5,15,2.5): c=rim
        if in_round(u,v,1.5,2,14.5,14,1.8): c=panel
        for k in range(3):
            if in_round(u,v,sx_[k]-.6,3.5,sx_[k]+.6,12.5,.6): c=slot
            if in_round(u,v,sx_[k]-1.6,cy_[k]-1.1,sx_[k]+1.6,cy_[k]+1.1,.5): c=cap
        return c
    ss=4 if size<=64 else 2
    im=Image.new('RGBA',(size,size))
    px=im.load()
    for y in range(size):
        for x in range(size):
            r=g=b=a=0
            for sy in range(ss):
                for sx in range(ss):
                    c=sample((x+(sx+.5)/ss)*16/size,(y+(sy+.5)/ss)*16/size)
                    r+=c[0]*c[3]; g+=c[1]*c[3]; b+=c[2]*c[3]; a+=c[3]
            n=ss*ss
            if a>0: px[x,y]=(round(r/a*255),round(g/a*255),round(b/a*255),round(a/n*255))
    return im
rgb=(60,190,80)
sizes=[16,20,24,32,40,48,64,256]
imgs=[render(s,rgb) for s in sizes]
imgs[-1].save(sys.argv[1], format='ICO', sizes=[(s,s) for s in sizes], append_images=imgs[:-1])
# preview: 256, 48, 32, 16 side by side on light and dark
pv=Image.new('RGBA',(256+48+32+16+50, 260*2),(255,255,255,255))
dark=Image.new('RGBA',(pv.width,260),(32,32,32,255)); pv.paste(dark,(0,260))
for row in range(2):
    x=5
    for s in [256,48,32,16]:
        im=imgs[sizes.index(s)]; pv.alpha_composite(im,(x,row*260+2)); x+=s+10
pv.save(sys.argv[2])
