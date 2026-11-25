import os, random, time
random.seed(1)
d = "/root/testdir"
os.makedirs(d, exist_ok=True)
words = ["report","holiday","photo","invoice","notes","backup","project","draft","final","scan",
         "تقرير","صورة","ملاحظات","مشروع","نسخة","العائلة","رحلة",
         "写真","プロジェクト","résumé","Ünïcödé","données","фото","отчёт"]
exts = [".txt",".pdf",".png",".jpg",".odt",".ods",".mp3",".mp4",".zip",".tar.gz",".c",".py",".html",".svg",".deb",""]
now = time.time()
for i in range(3000):
    n = "_".join(random.choice(words) for _ in range(random.randint(1,5))) + f"_{i}" + random.choice(exts)
    p = os.path.join(d, n)
    if random.random() < 0.08:
        os.makedirs(p, exist_ok=True)
    else:
        with open(p, "wb") as f:
            f.write(os.urandom(random.choice([0, 10, 1000, 50000])))
    t = now - random.random() * 86400 * 800
    os.utime(p, (t, t))
