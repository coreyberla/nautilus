import os, random, sys, time
random.seed(7)
d = sys.argv[1]; n = int(sys.argv[2]); os.makedirs(d, exist_ok=True)
words = ["report","holiday","photo","invoice","notes","backup","project","draft","final","scan","meeting","budget",
         "summary","contract","family","trip","recipe","lecture","assignment","screenshot","export","archive",
         "تقرير","صورة","ملاحظات","مشروع","写真","プロジェクト","résumé","données","фото","отчёт","Übersicht"]
exts = [".txt",".pdf",".png",".jpg",".odt",".ods",".mp3",".mp4",".zip",".tar.gz",".c",".py",".html",".svg",".md",".docx"]
now = time.time()
for i in range(n):
    name = "_".join(random.choice(words) for _ in range(random.randint(1,5))) + f"_{random.randint(0,10**9)}_{i}" + random.choice(exts)
    p = os.path.join(d, name)
    with open(p, "wb") as f:
        f.truncate(int(10 ** random.uniform(1, 9.5)))
    t = now - random.random() * 86400 * 1500
    os.utime(p, (t, t))
