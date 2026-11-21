from pathlib import Path
p=Path(r"E:\Github\VOIMA_ZH\translations.tsv")
lines=p.read_text(encoding="utf-8-sig").splitlines()
bs=chr(92); nl=bs+"n"
out=[]; skip_next=False
for line in lines:
    if skip_next:
        skip_next=False; continue
    if line.startswith("TIP:drill damage = 12\t"):
        continue
    if line.startswith("TIP:drill damage = {0}\tdrill") and line.count("\t")==1:
        skip_next=True; continue
    if line.startswith("TIP:mushroom big poisons {0}%\tmushroom big") and line.count("\t")==1:
        skip_next=True; continue
    out.append(line)
rows=[
"TIP:drill damage = {0}\tdrill damage = {0}\t钻头"+nl+"伤害 = {0}",
"TIP:mushroom big poisons {0}%\tmushroom big poisons {0}%\t大蘑菇"+nl+"中毒 {0}%",
]
for r in rows:
    if r not in out: out.append(r)
p.write_text("\n".join(out)+"\n",encoding="utf-8")
print("final",len(out))
