from pathlib import Path
root=Path(r"G:\VOIMA 2.2x2 Windows - Release")
mod=root/"VOIMA_ZH"
gamefont=root/"VOIMA_Data"/"StreamingAssets"/"Font"
chars=set()
for line in (mod/"translations.tsv").read_text(encoding="utf-8").splitlines():
    p=line.split("\t")
    if len(p)>=3 and p[0].startswith("LOAD:"):
        chars.update(c for c in p[2] if ord(c)>=128)
chars=sorted(chars)
cands=[]
for cp in range(0x0400,0x0500):
    name=chr(cp)+".png"
    if (gamefont/name).exists(): cands.append(name)
for cp in range(0x00C0,0x0180):
    name=chr(cp)+".png"
    if (gamefont/name).exists(): cands.append(name)
print("chars",len(chars),"cands",len(cands))
if len(cands)<len(chars): raise SystemExit("not enough reserved glyphs")
out=[]
for ch,name in zip(chars,cands):
    src=mod/"Font"/(ch+".png")
    if not src.exists(): raise SystemExit("missing src "+str(src))
    out.append(name+"\t"+ch+".png\t"+ch)
(mod/"Font"/"runtime_font_manifest.tsv").write_text("\n".join(out)+"\n",encoding="utf-8",newline="")
print("\n".join(out[:5])); print("wrote",len(out))
