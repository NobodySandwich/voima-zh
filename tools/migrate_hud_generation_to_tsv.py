from pathlib import Path
import re
repo=Path(r"E:\Github\VOIMA_ZH")
src=(repo/"src"/"plugin.cpp").read_text(encoding="utf-8")
start=src.index('static const std::map<std::string, std::string> actions = {')
end=src.index('};',start)
pairs=re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}',src[start:end])
tsv=repo/"translations.tsv"
lines=tsv.read_text(encoding="utf-8-sig").splitlines()
existing=set()
for line in lines:
    p=line.split("\t")
    if len(p)>=3 and p[0].startswith("HUD:"): existing.add(p[0][4:])
added=[]
for k,zh in pairs:
    if k not in existing:
        lines.append(f"HUD:{k}\t{k}\t{zh}"); added.append("HUD:"+k)
extra=[
"GEN:generating the world\tgenerating the world{*}\t正在生成世界{*}",
"GEN:rendering the world\trendering the world{*}\t正在渲染世界{*}",
"GEN:cards\t{0} cards\t{0} 张卡片",
"GEN:slabs\t{0} slabs\t{0} 个板块",
"HUD:edit every\tedit every\t批量编辑",
"HUD:wall thickness\twall thickness\t墙体厚度",
]
for row in extra:
    key=row.split("\t",1)[0]
    if not any(x.split("\t",1)[0]==key for x in lines):
        lines.append(row); added.append(key)
tsv.write_text("\n".join(lines)+"\n",encoding="utf-8")
(repo/"translations"/"translations.tsv").write_text("\n".join(lines)+"\n",encoding="utf-8")
print("map",len(pairs),"added",len(added))
for a in added: print(a)
