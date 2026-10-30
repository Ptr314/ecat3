import re, json, os
_REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
E85 = os.path.join(_REPO, "docs-external", "E85")
WIKI = os.path.join(E85, "wiki")
WORKDIR = os.environ.get("E85_WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))
W = WIKI
S = WORKDIR
man = json.load(open(os.path.join(S, "wiki_work", "manifest.json"), encoding="utf-8"))
res = {}
for slug in ["ns1_to", "po_to", "kngmd_to4", "nmd_to5", "nv1_to", "nv2_pasport", "sk"]:
    t = open(os.path.join(W, man[slug]["title"] + ".wiki"), encoding="utf-8").read()
    parts = re.split(r"(<!-- с\. \S+ \[[^\]]+\] -->)", t)
    pages = []
    pe = False
    for i in range(1, len(parts), 2):
        m = re.match(r"<!-- с\. (\S+) \[([^\]]+)\] -->", parts[i])
        body = parts[i + 1]
        if re.search(r"Перечень элементов", body, re.I) and slug != "sk" and i > 0.6 * len(parts):
            pe = True
        nums = len(re.findall(r"\b[0-7]{5,}\b", body))
        tbl = body.count("{|")
        if not pe and (tbl > 0 or nums >= 6):
            pages.append(m.group(2))
    res[slug] = pages
    print(slug, len(pages), "of", len(parts) // 2, pages[:60])
json.dump(res, open(os.path.join(S, "wiki_work", "verify_pages.json"), "w", encoding="utf-8"), ensure_ascii=False)
