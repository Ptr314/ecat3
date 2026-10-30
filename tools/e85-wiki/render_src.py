import subprocess, sys, os, io
_REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
E85 = os.path.join(_REPO, "docs-external", "E85")
WIKI = os.path.join(E85, "wiki")
WORKDIR = os.environ.get("E85_WORK", os.path.join(os.path.dirname(os.path.abspath(__file__)), "work"))
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

DJ = r"C:\Program Files (x86)\DjVuLibre"
E = E85
OUT = os.path.join(WORKDIR, "wiki_src")
PDFTOPPM = r"C:\DEV\poppler-25.12.0\Library\bin\pdftoppm.exe"

# key -> (file relative to E85, kind) ; kind: spread (96 dpi two-page scans), page, sheet (schematics)
DOCS = {
 "po_to":      ("Main/PO_tehnicheskoe_opisanir_2.791.026___.djvu", "spread"),
 "sk":         ("Main/Sistema_komand___tehnicheskoe_opisanie_2.791.026___1.djvu", "spread"),
 "fo":         ("Docs/formulyar_2.791.026___.djvu", "spread"),
 "ie":         ("Docs/instruktchiya_po_ekspluatatchii_2.791.026.djvu", "spread"),
 "pamyatki":   ("Docs/k_svedeniyu_potrbitala_pamyatki.djvu", "spread"),
 "zip_fodos":  ("Docs/Vedomost_ZIP_OS_FODOS_3.djvu", "spread"),
 "tpo":        ("Soft/Tesovie_programmi_rukovodstvo_po_tehnicheskomu_obsluzhivaniyu.00066_01_46_01.djvu", "spread"),
 "pvk_opis":   ("Soft/PO_PVK_Opisanir_primeneniya_.00065_01_31_01.djvu", "spread"),
 "basic":      ("Soft/Basic_.00065_01_35_01.djvu", "spread"),
 "rtd":        ("Soft/PO_PVK_redakror_RTD_rukovodstvo_operatora_.00065_01_34_04.djvu", "spread"),
 "ns1_to":     ("Main/NS1_TO.djvu", "page"),
 "mc0585_to":  ("Main/MC0585_TO.djvu", "page"),
 "kngmd_to4":  ("Discs/KNMD_TO4.djvu", "page"),
 "nmd_to5":    ("Discs/NMD_TO.djvu", "page"),
 "nmd_mc":     ("Discs/MC0585_NMD_TO.djvu", "page"),
 "fo2":        ("Docs/MC0585_FO.djvu", "page"),
 "ie2":        ("Docs/MC0585_IE.djvu", "page"),
 "kbd1":       ("KBD/Keyboard_MC7004.djvu", "page"),
 "kbd2":       ("KBD/Keyboard_MC7004_2.djvu", "page"),
 "np2":        ("RAM/MC0585_NP2.djvu", "page"),
 "cec":        ("Main/MC0585_CEC.djvu", "sheet"),
 "mc1702_to":  ("MC1702/MC1702_TO.djvu", "page"),
 "mc1702_ps":  ("MC1702/MC1702_PS.djvu", "page"),
 "ns1_sch":    ("Main/NS1_sch.djvu", "sheet"),
 "ms0585_sch": ("Main/MS0585_Sch.djvu", "sheet"),
 "kngmd_sch":  ("Discs/KNGMD_schematic_3.858.231.djvu", "sheet"),
 "knmd_sch":   ("Discs/KNMD_sch.djvu", "sheet"),
 "nmd_e3":     ("Discs/MC0585_NMD_E3.djvu", "sheet"),
 "np2_sch":    ("RAM/NP2_sch.djvu", "sheet"),
 "nv1_sch":    ("Video/NV1_sch.djvu", "sheet"),
 "nv2_sch":    ("Video/NV2_sch.djvu", "sheet"),
}
PDFS = {
 "nv1_to":      ("Video/MC_0585_NV1_TO.pdf", 250),
 "nv2_pasport": ("Video/NV2_pasport.pdf", 250),
 "mc9005":      ("\u0411\u041f/0585_MC9005.pdf", 220),
 "mc9005_sch":  ("\u0411\u041f/shematic_0585_MC9005.pdf", 250),
}

def npages(path):
    out = subprocess.run([os.path.join(DJ, "djvused.exe"), path, "-e", "n"], capture_output=True, text=True).stdout
    return int(out.strip())

def render_page(key, path, kind, p):
    d = os.path.join(OUT, key); os.makedirs(d, exist_ok=True)
    r = subprocess.run([os.path.join(DJ, "ddjvu.exe"), "-format=pgm", f"-page={p}", path, "-"], capture_output=True)
    im = Image.open(io.BytesIO(r.stdout)).convert("L")
    w, h = im.size
    if kind == "spread" and w > h:
        im.crop((0, 0, w // 2, h)).save(os.path.join(d, f"p{p:03d}a.png"), optimize=True)
        im.crop((w // 2, 0, w, h)).save(os.path.join(d, f"p{p:03d}b.png"), optimize=True)
    else:
        im.save(os.path.join(d, f"p{p:03d}.png"), optimize=True)
    if kind in ("spread",):
        od = os.path.join(OUT, key, "ocr"); os.makedirs(od, exist_ok=True)
        subprocess.run([os.path.join(DJ, "djvutxt.exe"), f"--page={p}", path, os.path.join(od, f"p{p:03d}.txt")], capture_output=True)

def main(keys):
    jobs = []
    for key in keys:
        if key in DOCS:
            rel, kind = DOCS[key]
            path = os.path.join(E, rel.replace("/", os.sep))
            n = npages(path)
            for p in range(1, n + 1):
                jobs.append((key, path, kind, p))
        elif key in PDFS:
            rel, dpi = PDFS[key]
            path = os.path.join(E, rel.replace("/", os.sep))
            d = os.path.join(OUT, key); os.makedirs(d, exist_ok=True)
            subprocess.run([PDFTOPPM, "-gray", "-r", str(dpi), "-png", path, os.path.join(d, "p")], capture_output=True)
    with ThreadPoolExecutor(max_workers=6) as ex:
        list(ex.map(lambda j: render_page(*j), jobs))
    print("done", len(jobs), "pages")

if __name__ == "__main__":
    main(sys.argv[1:] or list(DOCS) + list(PDFS))
