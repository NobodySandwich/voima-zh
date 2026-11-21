from pathlib import Path
p=Path(r"E:\Github\VOIMA_ZH\translations.tsv")
lines=p.read_text(encoding="utf-8-sig").splitlines()
bs=chr(92); nl=bs+"n"; rows=[
"TIP:mushroom small poisons {0}%\tmushroom small poisons {0}%\t小蘑菇"+nl+"中毒 {0}%",
"TIP:goober damage = {0}\tgoober damage = {0}\t咕宝"+nl+"伤害 = {0}",
]
for r in rows:
    if r not in lines: lines.append(r)
p.write_text("\n".join(lines)+"\n",encoding="utf-8")
print("rows",len(rows))
