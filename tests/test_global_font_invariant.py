from pathlib import Path
src=Path(__file__).resolve().parents[1]/"src"/"plugin.cpp"
lines=src.read_text(encoding="utf-8").splitlines()
viol=[]
for i,l in enumerate(lines):
    if "g_origBuild(" in l or "g_origBuildRotated(" in l:
        w="\n".join(lines[max(0,i-10):i])
        if not any(k in w for k in ("ApplyPageFont","g_fontPages.Clear","ForceFontWithUniverse")):
            viol.append((i+1,l.strip()))
print("violations:",len(viol))
for n,l in viol: print(n,l)
raise SystemExit(1 if viol else 0)
