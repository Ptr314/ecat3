import os, re, json, glob
_REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
E85 = os.path.join(_REPO, "docs-external", "E85")
WIKI = os.path.join(E85, "wiki")
WORKDIR = os.environ.get("E85_WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))
S = WORKDIR
W = WIKI
man = json.load(open(os.path.join(S, "wiki_work", "manifest.json"), encoding="utf-8"))
SRCFILE = {
 "po_to": "Main/PO_tehnicheskoe_opisanir_2.791.026___.djvu (сверка: Main/MC0585_TO.djvu)",
 "sk": "Main/Sistema_komand___tehnicheskoe_opisanie_2.791.026___1.djvu",
 "ns1_to": "Main/NS1_TO.djvu", "nv1_to": "Video/MC_0585_NV1_TO.pdf", "nv2_pasport": "Video/NV2_pasport.pdf",
 "kngmd_to4": "Discs/KNMD_TO4.djvu (по содержанию это ТО контроллера НГМД)",
 "nmd_to5": "Discs/NMD_TO.djvu (копия: Discs/MC0585_NMD_TO.djvu)",
 "fo": "Docs/formulyar_2.791.026___.djvu (копия: Docs/MC0585_FO.djvu)",
 "ie": "Docs/instruktchiya_po_ekspluatatchii_2.791.026.djvu (копия: Docs/MC0585_IE.djvu)",
 "pamyatki": "Docs/k_svedeniyu_potrbitala_pamyatki.djvu", "zip_fodos": "Docs/Vedomost_ZIP_OS_FODOS_3.djvu",
 "tpo": "Soft/Tesovie_programmi_rukovodstvo_po_tehnicheskomu_obsluzhivaniyu.00066_01_46_01.djvu",
 "pvk_opis": "Soft/PO_PVK_Opisanir_primeneniya_.00065_01_31_01.djvu", "basic": "Soft/Basic_.00065_01_35_01.djvu",
 "rtd": "Soft/PO_PVK_redakror_RTD_rukovodstvo_operatora_.00065_01_34_04.djvu",
 "kbd1": "KBD/Keyboard_MC7004.djvu (те же материалы: KBD/Keyboard_MC7004_2.djvu)",
 "mc9005": "БП/0585_MC9005.pdf, БП/shematic_0585_MC9005.pdf",
 "mc1702_to": "MC1702/MC1702_TO.djvu (tis.kz, архив MC1702.rar)",
 "mc1702_ps": "MC1702/MC1702_PS.djvu (tis.kz, архив MC1702.rar)",
}
rows = []; nrzb_lines = []
for slug, meta in man.items():
    fn = os.path.join(W, meta["title"] + ".wiki")
    if not os.path.exists(fn): continue
    t = open(fn, encoding="utf-8").read()
    n_nrzb = t.count("[нрзб.]"); n_q = t.count("[?]")
    pages = len(re.findall(r"<!-- с\. \S+ \[[^\]]+\] -->", t))
    figs = len(re.findall(r"\[\[Файл:", t))
    rows.append((meta["title"], SRCFILE.get(slug, ""), pages, n_nrzb, n_q, figs))
    # where are [нрзб.]
    cur = "?"
    for line in t.split("\n"):
        m = re.search(r"<!-- с\. (\S+) \[([^\]]+)\] -->", line)
        if m: cur = f"с. {m.group(1)} ({m.group(2)})"
        if "[нрзб.]" in line and not line.startswith("'''"):
            snippet = re.sub(r"\s+", " ", line)[:140]
            nrzb_lines.append(f"- {meta['title']}, {cur}: {snippet}")
out = []
out.append("# Электроника МС 0585: пакет статей для emuverse\n")
out.append("Перепечатки документов комплекса «Электроника МС 0585» (Электроника-85) в формате MediaWiki для публикации в энциклопедии emuverse.ru. "
           "Один файл `.wiki` на документ; название файла = название статьи. Картинки лежат в `images/` и подключены как `[[Файл:…]]`.\n")
out.append("## Соглашения\n")
out.append("- Исходные документы напечатаны заглавными буквами (АЦПУ); в статьях текст приведён к обычному регистру, обозначения регистров и сигналов оставлены как в оригинале.\n"
           "- Нумерация разделов, таблиц и рисунков исходная. Конец каждой печатной страницы отмечен комментарием `<!-- с. N [файл] -->`: N — печатный номер, в скобках имя файла страницы из папки обработки.\n"
           "- `[нрзб.]` — не прочитано; `[?]` — сомнительное чтение; `<!-- так в оригинале -->` — явная опечатка оригинала сохранена.\n"
           "- Таблицы набраны как `wikitable`; рисунки из символов перепечатаны в `<pre>`; остальные рисунки и листы схем — картинки (`images/E85_*.png`).\n"
           "- Скан АЦПУ путает буквы Д/А/Я, П/Я, Ш/Ю/Ф/М, Э/З: аббревиатуры восстановлены по смыслу, при сомнении стоит `[?]`.\n"
           "- Названия статей начинаются с «Электроника МС 0585», кроме клавиатуры и блока питания (МС 7004, МС 9005). На всех статьях стоит категория `Категория:Электроника МС 0585`.\n"
           "- Плашка об авторских правах (как в статье «УКНЦ Техническое описание прототипа») в статьи не вставлена: шаблон на emuverse нужно подставить при публикации.\n")
out.append("## Статьи-перепечатки\n")
out.append("| Статья | Исходный файл (папка docs-external/E85) | Страниц | [нрзб.] | [?] | Картинок |\n|---|---|---|---|---|---|")
for r in rows:
    out.append(f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} |")
out.append("")
out.append("## Статьи со схемами и обзорная\n")
out.append("- `Электроника МС 0585.wiki` — обзорная статья машины (состав, процессор, карта адресов, векторы, ПЗУ с контрольными суммами, автотест, документация, эмуляторы, ссылки).\n"
           "- `… Схема электрическая принципиальная.wiki` — листы схем НС1, НВ1, НВ2, КНГМД, КНМД, НП2 (изображения; перечни элементов НС1, НВ1, НВ2, КНГМД, КНМД перепечатаны в соответствующих технических описаниях).\n"
           "- `Электроника МС 0585 НП2 Перечень элементов.wiki` — перечни элементов НП2 (3 листа).\n"
           "- `Электроника МС 0585 Схемы электрические соединений.wiki` — схемы соединений исполнений МС0585 … МС0585.08 и кабелей.\n"
           "- `Электроника МС 1702 Схема электрическая принципиальная.wiki` — схема Э3 (3 листа) и сборочный чертёж СБ (2 листа) сопроцессора МС 1702.\n")
out.append("## Дополнительные материалы (tis.kz, тема «Документация», forum=31&topic=3)\n")
out.append("- Архивы Andrey_Ak (2012): `mc0585.rar`, `hc1-np2.rar`, `ngmd-nmd.rar`, `hb1-hb2.rar`, `ps-kb.rar` совпадают с материалами папки E85; новое — `MC1702.rar` (ТО и паспорт сопроцессора МС 1702, папка `MC1702`), `MC1702-SH.rar` (схема и сборочный чертёж МС 1702, `MC1702/MC1702-E3`), второй экземпляр сканов ТО НВ1 и НВ2 (из него взят лист 16 НВ2, `Video/scans_tis`).\n"
           "- Каталог документации Andrey_Ak (`Docs/tis_Documentation_catalog.xls`) показывает документы, которых нет в сети (например, 2.791.026 Э6, «ПРОС. Руководство оператора»).\n"
           "- Образы дискет tis.kz (темы 001–007) — в папке `DSK`.\n")
out.append("## Что не вошло\n")
out.append("- Документы DEC (папка `DEC`): EK-PC300-V1/V2, ek-pc380-ps-003, MP01922 — большие сканы без текстового слоя, доступны на bitsavers.org.\n"
           "- Дубли и худшие копии: `Main/MC0585_TO`, `Docs/MC0585_FO`, `Docs/MC0585_IE`, `Discs/MC0585_NMD_TO`, `Discs/MC0585_NMD_E3` (кроме сборочного чертежа и кабелей КНМД), `Main/MS0585_Sch`, `RAM/MC0585_NP2`, `KBD/Keyboard_MC7004_2` — использованы только для сверки и недостающих листов.\n"
           "- `Video/scan.rar` не открывался.\n"
           "- Страницы-обложки и пустые листы отмечены маркерами без текста.\n")
out.append("## Особенности нумерации и сканов\n")
out.append("- КНМД (ТО5): в скане нет печатной страницы 13 (таблица регистра ошибок РОШПК продолжается на с. 14 без части разрядов 09-14).\n"
           "- НВ2 (ТО6): в основном скане (Video/NV2_pasport.pdf) нет печатного листа 16, он дополнен из другого экземпляра скана с tis.kz (Video/scans_tis/HB2_TO_Scanitto_09_sheets16-17.png); файл p-16 основного скана — это лист 17.\n"
           "- МС 9005 (паспорт): после с. 26 идёт с. 29; в скане нет страниц 27-28.\n"
           "- Системный модуль НС1: правый край ряда страниц закрыт тенью скана, оборванные слова дочитаны по смыслу с пометкой `[?]` или `[нрзб.]`.\n")
out.append("## Места с [нрзб.]\n")
out.extend(nrzb_lines if nrzb_lines else ["Нет."])
open(os.path.join(W, "README.md"), "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print(len(rows), "articles;", len(nrzb_lines), "нрзб")
