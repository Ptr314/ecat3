import os, json, glob, math
_REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
E85 = os.path.join(_REPO, "docs-external", "E85")
WIKI = os.path.join(E85, "wiki")
WORKDIR = os.environ.get("E85_WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))
S = WORKDIR
SRC = os.path.join(S, "wiki_src")
WORK = os.path.join(S, "wiki_work")
JOBS = os.path.join(WORK, "jobs")
os.makedirs(JOBS, exist_ok=True)

P = "Электроника МС 0585 "
# slug: (title, designation, kind, per_chunk, extra hint)
DOCS = {
 "ns1_to":      (P+"НС1 Техническое описание", "2.791.026 ТО2 (модуль системный НС1, плата 3.858.230)", "page", 20, ""),
 "po_to":       (P+"Техническое описание", "2.791.026 ТО (комплекс вычислительный персональный «Электроника МС 0585»)", "spread", 22, ""),
 "kngmd_to4":   (P+"КНГМД Техническое описание", "2.791.026 ТО4 (контроллер НГМД, плата 3.858.231)", "page", 17, ""),
 "nmd_to5":     (P+"КНМД Техническое описание", "2.791.026 ТО5 (контроллер НМД, плата 3.858.232). Печатной страницы 13 в этом скане может не быть; если её нет, отметьте это комментарием", "page", 17, ""),
 "nv1_to":      (P+"НВ1 Техническое описание", "2.791.026 ТО3 (видеоконтроллер НВ1, плата 3.858.233)", "page", 17, ""),
 "nv2_pasport": (P+"НВ2 Техническое описание", "2.791.026 ТО6 (расширитель памяти видеоконтроллера НВ2, плата 3.858.302). В скане нет печатного листа 16: файл p-16 это лист 17, дальше сдвиг на 1 до файла p-23 = лист 24; отметьте это комментарием", "page", 17, ""),
 "sk":          (P+"Система команд", "2.791.026 ТО1 (система команд, техническое описание)", "spread", 20, "Документ большой, текст местами очень плотный: описания команд с кодами, форматами, признаками, примерами. Коды команд и примеры переписывайте особенно точно."),
 "fo":          (P+"Формуляр", "2.791.026 ФО", "spread", 18, ""),
 "ie":          (P+"Инструкция по эксплуатации", "2.791.026 ИЭ", "spread", 18, ""),
 "pamyatki":    (P+"Памятки потребителю", "К сведению потребителя (памятки)", "spread", 14, ""),
 "zip_fodos":   (P+"ФОДОС Ведомость ЗИП", "Ведомость ЗИП ОС ФОДОС 3", "spread", 20, "Это ведомость комплекта: переписывайте таблицы целиком, включая обозначения и количество."),
 "tpo":         (P+"Тестовые программы", ".00066-01 46 01 (тестовые программы, руководство по техническому обслуживанию)", "spread", 20, ""),
 "pvk_opis":    (P+"ПО ПВК Описание применения", ".00065-01 31 01 (программное обеспечение ПВК, описание применения)", "spread", 12, ""),
 "basic":       (P+"Бейсик", ".00065-01 35 01 (язык Бейсик, программное обеспечение ПВК)", "spread", 20, ""),
 "rtd":         (P+"Редактор RTD", ".00065-01 34 04 (редактор RTD, руководство оператора)", "spread", 20, ""),
 "kbd1":        ("Электроника МС 7004 Техническое описание и инструкция по эксплуатации", "Блок клавиатуры «Электроника МС 7004», техническое описание и инструкция по эксплуатации", "page", 12, ""),
 "mc1702_to":   ("Электроника МС 1702 Техническое описание и инструкция по эксплуатации", "3.098.002 ТО (сопроцессор «Электроника МС1702», техническое описание и инструкция по эксплуатации) с перечнем элементов 3.098.002 ПЭ3. Страницы 400 dpi, одиночные; печатные номера внизу листа", "page", 16, "Сопроцессор МС1702 — плата с процессором 1810ВМ86, ОЗУ и ПЗУ, работающая с комплексом МС0585 по системной магистрали. Особенно точно переписывайте регистры, адреса, биты, таблицы сигналов и перечень элементов."),
 "mc1702_ps":   ("Электроника МС 1702 Паспорт", "3.098.002 ПС (сопроцессор «Электроника МС1702», паспорт) и 3.098.002 ЭД (ведомость эксплуатационных документов)", "page", 16, ""),
 "mc9005":      ("Электроника МС 9005 Паспорт", "Источник питания «Электроника МС9005», паспорт 3.508.023 ПС", "page", 19, ""),
}

def images(slug, kind):
    d = os.path.join(SRC, slug)
    fs = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    return fs

def chunks(fs, per, kind):
    # chunks of ~per images; for spreads keep a/b together
    n = len(fs)
    k = max(1, round(n / per))
    size = math.ceil(n / k)
    if kind == "spread" and size % 2: size += 1
    out = []
    for i in range(0, n, size):
        out.append(fs[i:i + size])
    return out

manifest = {}
for slug, (title, desig, kind, per, hint) in DOCS.items():
    fs = images(slug, kind)
    ch = chunks(fs, per, kind)
    manifest[slug] = {"title": title, "designation": desig, "kind": kind, "chunks": []}
    has_ocr = os.path.isdir(os.path.join(SRC, slug, "ocr"))
    for k, c in enumerate(ch):
        base = k * 10
        groups = [c[i:i + 3] for i in range(0, len(c), 3)]
        if len(groups) > 9:
            raise SystemExit("too many groups")
        lines = []
        lines.append(f"# Задание: «{title}», кусок {k+1} из {len(ch)}")
        lines.append("")
        lines.append(f"- Документ: {desig}")
        lines.append(f"- Название статьи на вики: «{title}»")
        lines.append(f"- slug (для имён файлов рисунков и путей): `{slug}`")
        lines.append(f"- Каталог страниц (PNG, серый): `{os.path.join(SRC, slug)}`")
        if has_ocr:
            lines.append(f"- Слабый слой OCR (только подсказка, много ошибок): `{os.path.join(SRC, slug, 'ocr')}\\pNNN.txt` (NNN = номер разворота)")
        lines.append(f"- Каталог результата: `{os.path.join(WORK, slug)}` (создайте, если нет)")
        lines.append(f"- Правила: `{os.path.join(WORK, 'STYLE.md')}` (прочитайте первым, соблюдайте строго)")
        if kind == "spread":
            lines.append("- Файлы pNNNa / pNNNb — левая и правая печатные страницы одного разворота: читайте a, затем b.")
        lines.append(f"- Это кусок {k+1} из {len(ch)}: " + ("первый кусок документа (титульный лист и начало)." if k == 0 else "продолжение; текст может начинаться с середины абзаца/таблицы предыдущего куска (см. STYLE, «продолжение»)."))
        if k < len(ch) - 1:
            lines.append("- Последняя страница вашего куска может обрываться посреди абзаца: просто закончите её текстом, как есть; следующий кусок продолжит.")
        if hint:
            lines.append(f"- Особенность документа: {hint}")
        lines.append("")
        lines.append("## Куски вывода (читайте по 3 картинки за вызов, файл пишите сразу после чтения)")
        for gi, g in enumerate(groups, start=1):
            lines.append(f"- `g{base+gi:03d}.wiki` ← {', '.join(g)}")
        lines.append("")
        lines.append("Когда всё готово, ответьте коротким отчётом: сколько страниц переписано, где были неразборчивые места ([нрзб.] — перечислите страницы), где печатные номера страниц не читаются, какие рисунки помечены (id). Сам текст в ответ не вставляйте.")
        with open(os.path.join(JOBS, f"{slug}_{k+1:02d}.md"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        manifest[slug]["chunks"].append({"n": k + 1, "base": base, "files": c})
    print(slug, len(fs), "images,", len(ch), "chunks")
with open(os.path.join(WORK, "manifest.json"), "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=1)
print("total chunks", sum(len(m["chunks"]) for m in manifest.values()))
