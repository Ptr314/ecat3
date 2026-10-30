# Электроника МС 0585: скрипты вики-пакета и образов дисков

Скрипты, которыми сделан пакет статей `docs-external/E85/wiki` (перепечатка документов со сканов) и разобраны образы `docs-external/E85/DSK`. Python: `C:\DEV\venv\Scripts\python.exe` (нужен Pillow).

Рабочий каталог (страницы, задания, куски) — `tools/e85-wiki/work` или `E85_WORK`. В репозиторий он не входит; готовые статьи лежат в `docs-external/E85/wiki`.

| Скрипт | Что делает |
|---|---|
| `render_src.py` | djvu/pdf в серые PNG (`work/wiki_src/<ключ>/pNNN[a,b].png`, разворот делится на a/b), слабый слой OCR. Нужны DjVuLibre и poppler (пути в начале файла) |
| `make_jobs.py` | задания для переписчика по кускам (`work/wiki_work/jobs`, `manifest.json`) |
| `STYLE.md` | правила перепечатки; кладётся в `work/wiki_work/` |
| `assemble.py` | склейка кусков `g*.wiki`, вырезка рисунков (таблица `ROT` — повороты), проверки, запись статей в `wiki/` |
| `make_sheets.py` | статьи-галереи листов схем |
| `make_readme.py` | `wiki/README.md` |
| `find_verify_pages.py` | выбор страниц с цифрами для сверки |
| `ods1_list.py` | каталог Files-11 ODS-1 и RT-11 с образа `.dsk` (физический порядок, чередование 2:1 со сдвигом 2 на дорожку) |
| `dsk_probe.py` | разведка формата образа |

Куски `wiki_work/<ключ>/g*.wiki` во временном каталоге не сохранились: правки статей делать прямо в `docs-external/E85/wiki/*.wiki`.
