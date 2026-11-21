from pathlib import Path
TSV=Path(r"E:\Github\VOIMA_ZH\translations.tsv")
lines=TSV.read_text(encoding="utf-8-sig").splitlines()
cards={}
for line in lines:
    p=line.split("\t")
    if len(p)>=3 and p[0].startswith("CARD:characters/"):
        cards[p[0].split("/")[-1].strip().lower()]=p[2]
def cls(zh):
    if "熊" in zh or "蜜蜂" in zh: return "一只"
    if "树" in zh: return "一棵"
    if "骨头" in zh or "骨" in zh: return "一根"
    if "巢" in zh: return "一个"
    return "一个"
out=[]
for line in lines:
    p=line.split("\t")
    if len(p)>=3 and p[0]=="DEATH:card" and p[1].startswith("you were killed by "):
        src=p[1]
        for n,zh in cards.items():
            if src in (f"you were killed by a {n}.", f"you were killed by {n}."):
                sep=" " if zh and ord(zh[-1])<128 and zh[-1].isalnum() else ""
                line="DEATH:card\t"+p[1]+"\t你被"+cls(zh)+zh+sep+"杀死了"
                break
    out.append(line)
TSV.write_text("\n".join(out)+"\n",encoding="utf-8")
print("cards",len(cards))
