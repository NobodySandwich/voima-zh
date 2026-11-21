from pathlib import Path
TSV=Path(r"E:\Github\VOIMA_ZH\translations.tsv")
lines=TSV.read_text(encoding="utf-8-sig").splitlines()
cards={}
for line in lines:
    p=line.split("\t")
    if len(p)>=3 and p[0].startswith("CARD:characters/"):
        name=p[0].split("/")[-1].strip()
        if name:
            cards[name.lower()]=p[2]
rows=[]; added=0
for name,zh in sorted(cards.items()):
    for src in (f"you were killed by a {name}.", f"you were killed by {name}."):
        row=f"DEATH:card\t{src}\t你被 {zh} 杀死了"
        if row not in lines:
            rows.append(row); lines.append(row); added+=1
TSV.write_text("\n".join(lines)+"\n",encoding="utf-8")
print("creatures",len(cards),"added",added)
for r in rows[:20]: print(r)
