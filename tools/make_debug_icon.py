from pathlib import Path
from PIL import Image, ImageOps
src=Path(r"E:\Github\VOIMA_ZH\assets\voima.ico")
dst=Path(r"E:\Github\VOIMA_ZH\assets\voima_debug.ico")
im=Image.open(src).convert("RGBA")
r,g,b,a=im.split()
rgb=ImageOps.invert(Image.merge("RGB",(r,g,b)))
out=Image.merge("RGBA",(*rgb.split(),a))
out.save(dst,format="ICO",sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
print(dst, dst.stat().st_size)
