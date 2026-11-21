from pathlib import Path
p=Path(r"E:\Github\VOIMA_ZH\src\plugin.cpp")
s=p.read_text(encoding="utf-8")
tags=["bannerprobe","chestdebug","tipdebug","hudbind","renderzh","speechfirst","renderempty","unidump","carddump","fps"]
for t in tags:
    s=s.replace(f'Logger::Instance().Log("{t}"', f'Logger::Instance().Debug("{t}"')
p.write_text(s,encoding="utf-8")
print("gated",len(tags))
