# sbs.py out.png width label1=img1 label2=img2 ... : side by side, labeled, scaled to total width
import sys

from PIL import Image, ImageDraw, ImageFont

out, W = sys.argv[1], int(sys.argv[2])
items = [a.split("=", 1) for a in sys.argv[3:]]
w = W // len(items)
ims = []
for lab, p in items:
    im = Image.open(p).convert("RGB")
    im = im.resize((w, int(im.height * w / im.width)), Image.LANCZOS)
    ims.append((lab, im))
h = max(i.height for _, i in ims)
c = Image.new("RGB", (w * len(ims), h + 34), (20, 20, 20))
d = ImageDraw.Draw(c)
try:
    f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20)
except Exception:
    f = None
for k, (lab, im) in enumerate(ims):
    c.paste(im, (k * w, 34))
    d.text((k * w + 8, 6), lab, fill=(240, 240, 240), font=f)
c.save(out)
