import UnityPy
from pathlib import Path
p=Path(r"G:\VOIMA 2.2x2 Windows - Release\VOIMA_Data\data.unity3d")
env=UnityPy.load(str(p))
print("objects",len(env.objects))
counts={}
for obj in env.objects:
    t=obj.type.name
    counts[t]=counts.get(t,0)+1
print(counts)
for obj in env.objects:
    try:
        d=obj.read_typetree()
    except Exception:
        continue
    if not isinstance(d,dict): continue
    if any(k in d for k in ("damage","heal_amount","push_damage")) and "path" in d:
        print("FOUND",obj.type.name,obj.path_id,d.get("path"),d.get("name"),d.get("damage"),d.get("heal_amount"),d.get("push_damage"))
