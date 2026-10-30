import os, re, json, sys, glob
_REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
E85 = os.path.join(_REPO, "docs-external", "E85")
WIKI = os.path.join(E85, "wiki")
WORKDIR = os.environ.get("E85_WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))
from PIL import Image

S = WORKDIR
SRC = os.path.join(S, "wiki_src")
WORK = os.path.join(S, "wiki_work")
OUT = WIKI
IMG = os.path.join(OUT, "images")
CAT = "[[Категория:Электроника МС 0585]]"

manifest = json.load(open(os.path.join(WORK, "manifest.json"), encoding="utf-8"))
ROT = {("kbd1","pril1"): -90, ("kbd1","ris2"): -90, ("kbd1","ris3"): -90, ("kngmd_to4","ris1"): 180, ("nmd_to5","ris2"): -90}
FIG = re.compile(r"<!--FIG\s+id=(\S+)\s+src=(\S+)\s+bbox=([\d.,\- ]+?)\s*-->")

def join_parts(texts):
    out = ""
    for t in texts:
        t = t.strip("\n")
        if not t.strip():
            continue
        lines = t.split("\n")
        if lines[0].strip() == "<!-- продолжение -->":
            t = "\n".join(lines[1:]).lstrip("\n")
            out = out.rstrip("\n")
            sep = "\n" if (out.endswith("|}") or out.endswith("</pre>")) else " "
            # continuation of a paragraph: join with a space; after block elements keep a newline
            out = out + sep + t if out else t
        else:
            out = out + ("\n\n" if out else "") + t
    return out

def crop_figures(slug, text):
    figs = []
    def repl(m):
        fid, src, bb = m.group(1), m.group(2), m.group(3)
        figs.append((fid, src, bb))
        return ""
    text = FIG.sub(repl, text)
    os.makedirs(IMG, exist_ok=True)
    for fid, src, bb in figs:
        path = os.path.join(SRC, src.replace("/", os.sep))
        if not os.path.exists(path):
            print("  !! no source for figure", fid, src); continue
        x0, y0, x1, y1 = [float(v) for v in bb.split(",")]
        im = Image.open(path)
        w, h = im.size
        box = (max(0, int((x0 - 0.01) * w)), max(0, int((y0 - 0.01) * h)), min(w, int((x1 + 0.01) * w)), min(h, int((y1 + 0.01) * h)))
        c = im.crop(box)
        if (slug, fid) in ROT: c = c.rotate(ROT[(slug, fid)], expand=True)
        if c.width > 1600:
            c = c.resize((1600, int(c.height * 1600 / c.width)), Image.LANCZOS)
        c.save(os.path.join(IMG, f"E85_{slug}_{fid}.png"), optimize=True)
    return text, figs

def check(slug, text):
    probs = []
    opens = sum(1 for l in text.split("\n") if l.startswith("{|"))
    closes = sum(1 for l in text.split("\n") if l.strip() == "|}")
    if opens != closes: probs.append(f"таблицы: {opens} открыто, {closes} закрыто")
    if text.count("<pre>") != text.count("</pre>"): probs.append("<pre> не парны")
    stripped = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    # arrows like ----> inside <pre> art are harmless; a leftover '<!--' means an unclosed comment
    if "<!--" in stripped: probs.append("незакрытый комментарий <!--")
    nrz = text.count("[нрзб.]")
    q = text.count("[?]")
    pages = re.findall(r"<!-- с\. (\S+) \[([^\]]+)\] -->", text)
    nums = []
    for n, f in pages:
        if n.isdigit(): nums.append(int(n))
    gaps = [(a, b) for a, b in zip(nums, nums[1:]) if b != a + 1]
    return probs, nrz, q, len(pages), gaps

def main(slugs):
    os.makedirs(OUT, exist_ok=True)
    report = []
    for slug in slugs:
        meta = manifest[slug]
        d = os.path.join(WORK, slug)
        files = sorted(glob.glob(os.path.join(d, "g*.wiki")))
        if not files:
            print(slug, "нет кусков"); continue
        # expected groups
        texts = [open(f, encoding="utf-8").read() for f in files]
        body = join_parts(texts)
        body, figs = crop_figures(slug, body)
        head = f"'''{meta['title']}''' — перепечатка документа «{meta['designation']}». Текст распознан со скана; неразборчивые места помечены «[нрзб.]», сомнительные чтения — «[?]».\n\n"
        text = head + body.strip("\n") + "\n\n" + CAT + "\n"
        text = re.sub(r"\n{3,}", "\n\n", text)
        fn = os.path.join(OUT, meta["title"] + ".wiki")
        with open(fn, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        probs, nrz, q, npages, gaps = check(slug, text)
        done = len(files)
        expected = sum(-(-len(c["files"]) // 3) for c in meta["chunks"])
        found = set(f for _, f in re.findall(r"<!-- с\. (\S+) \[([^\]]+)\] -->", text))
        allf = [f[:-4] for c in meta["chunks"] for f in c["files"]]
        missing = [f for f in allf if f not in found]
        print(f"  картинок без маркера страницы: {len(missing)} из {len(allf)}: {missing[:30]}")
        print(f"{slug}: {done}/{expected} кусков, страниц-маркеров {npages}, нрзб {nrz}, ? {q}, рисунков {len(figs)}; проблемы: {probs or 'нет'}; разрывы нумерации: {gaps[:6]}")
        report.append(dict(slug=slug, title=meta["title"], groups=done, expected=expected, nrzb=nrz, q=q, figs=len(figs), problems=probs, gaps=gaps))
    json.dump(report, open(os.path.join(WORK, "report.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)

if __name__ == "__main__":
    main(sys.argv[1:] or list(manifest))
