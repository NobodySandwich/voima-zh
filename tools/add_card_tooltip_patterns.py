from pathlib import Path
import re
ROOT=Path(r"E:\Github\VOIMA_ZH")
tsv=ROOT/"translations.tsv"; report=ROOT/"catalog"/"card_tooltip_actual_report.tsv"
lines=tsv.read_text(encoding="utf-8-sig").splitlines()
cards={}
for line in lines:
    p=line.split("\t")
    if len(p)>=3 and p[0].startswith("CARD:"):
        cards[p[0].split("/")[-1].strip().lower()]=p[2]
bs=chr(92); nl=bs+"n"; rows=[]
for line in report.read_text(encoding="utf-8").splitlines()[1:]:
    p=line.split("\t")
    if len(p)<5 or p[0]!="EXACT": continue
    stat,item,key,source=p[1],p[2],p[3],p[4].replace("\\n"," ")
    name=cards.get(item)
    if not name: continue
    pat=re.sub(r"\d+", "{0}", key, count=1)
    src=re.sub(r"\d+", "{0}", source, count=1)
    if stat=="damage": tr=name+nl+"伤害 = {0}"
    elif stat=="heals": tr=name+nl+"治疗 {0}%"
    elif stat=="heals others": tr=name+nl+"治疗他人 = {0}"
    else: continue
    row=pat+"\t"+src+"\t"+tr
    if row not in lines: rows.append(row)
if rows:
    lines.extend(rows); tsv.write_text("\n".join(lines)+"\n",encoding="utf-8")
print("added",len(rows))
for r in rows: print(r)
