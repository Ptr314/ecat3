// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: I/O ports window (output of serial lines and printers), source

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "serialwindow.h"
#include "dialogs/numfmt.h"
#include "emulator/utils.h"
#include "charmaps.h"

//Окно опрашивает порты так часто, чтобы вывод шел за программой без рывков
#define POLL_MS         50
//Строк в поле: больше QPlainTextEdit заметно тормозит
#define MAX_LINES       10000
//Сколько принятого держит вкладка для перерисовки
#define DATA_LIMIT      (256 * 1024)
//Сколько принятого идет в поле за раз: остальное только в файл и в буфер
#define RENDER_LIMIT    (64 * 1024)
//Длина строки поля без перевода строки в потоке: байтов и знаков
#define BYTES_PER_LINE  16
#define TEXT_LINE_MAX   1024

static const char * const MODE_NAMES[] = {"original", "recoded", "bytes"};

//Кодировки dsk_tools (init_charmap), какие имеют смысл для потока символов:
//onix - та же КОИ-8 с разметкой текстового редактора
struct Charset {
    const char * id;
    const char * title;
};
static const Charset CHARSETS[] = {
    {"koi7_n0_n1", QT_TRANSLATE_NOOP("SerialWindow", "KOI-7 N0/N1 (SO/SI)")},
    {"koi7_n2",    QT_TRANSLATE_NOOP("SerialWindow", "KOI-7 N2")},
    {"koi8_r",     QT_TRANSLATE_NOOP("SerialWindow", "KOI-8-R")},
    {"koi8_m",     QT_TRANSLATE_NOOP("SerialWindow", "KOI-8-M")},
    {"cp866",      QT_TRANSLATE_NOOP("SerialWindow", "CP866")},
    {"cp1251",     QT_TRANSLATE_NOOP("SerialWindow", "Windows-1251")},
    {"iso8859_5",  QT_TRANSLATE_NOOP("SerialWindow", "ISO 8859-5")},
    {"ascii",      QT_TRANSLATE_NOOP("SerialWindow", "ASCII")},
    {"agat",       QT_TRANSLATE_NOOP("SerialWindow", "Agat")},
    {"apple2",     QT_TRANSLATE_NOOP("SerialWindow", "Apple II")},
    {"apple2c",    QT_TRANSLATE_NOOP("SerialWindow", "Apple IIc")},
};
static const int CHARSET_COUNT = sizeof(CHARSETS) / sizeof(CHARSETS[0]);
//Данные пункта списка для charmap машины
#define CHARSET_MACHINE -1

//Устройства с отводом байтов, в порядке конфигурации
static std::vector<ComputerDevice*> find_ports(Emulator * e)
{
    std::vector<ComputerDevice*> result;
    for (unsigned int i = 0; i < e->dm->device_count; i++) {
        ComputerDevice * d = e->dm->get_device(i)->device.get();
        if (dynamic_cast<ByteTap*>(d) != nullptr) result.push_back(d);
    }
    return result;
}

bool SerialWindow::machine_has_ports(Emulator * e)
{
    return e != nullptr && e->loaded && !find_ports(e).empty();
}

SerialWindow::SerialWindow(QWidget *parent, Emulator * e)
    : GenericDbgWnd(parent)
    , e(e)
    , poll_timer(this)
{
    setWindowTitle(tr("I/O ports"));
    resize(720, 420);

    const std::string mode_setting = e->read_setup("Serial", "mode", "recoded");
    for (int m = 0; m < MODE_COUNT; m++)
        if (mode_setting == MODE_NAMES[m]) mode = m;

    //Одно меню на все вкладки: папка и режим общие
    settings_menu = new QMenu(this);
    settings_menu->addAction(tr("Choose folder..."), this, &SerialWindow::choose_folder);
    settings_menu->addSeparator();
    QActionGroup * group = new QActionGroup(this);
    const QString mode_titles[MODE_COUNT] = {tr("Original text"), tr("Recoded text"), tr("Bytes")};
    for (int m = 0; m < MODE_COUNT; m++) {
        mode_actions[m] = settings_menu->addAction(mode_titles[m]);
        mode_actions[m]->setCheckable(true);
        mode_actions[m]->setChecked(m == mode);
        mode_actions[m]->setActionGroup(group);
        connect(mode_actions[m], &QAction::triggered, this, [this, m]{ set_mode(m); });
    }

    tabs = new QTabWidget(this);
    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(tabs);

    const std::vector<ComputerDevice*> devs = find_ports(e);
    if (!devs.empty()) base = view_base(devs[0]);
    ports.resize(devs.size());
    for (size_t i = 0; i < devs.size(); i++) {
        Port & p = ports[i];
        p.dev = devs[i];
        p.tap = dynamic_cast<ByteTap*>(devs[i]);
        //На вкладке - название порта из конфигурации (title), в подсказке -
        //имя устройства, по которому к нему обращаются сценарии и MCP
        const std::string title = str_trim(devs[i]->config_parameter("title"));
        p.name = QString::fromStdString(title.empty() ? devs[i]->name : title);
        const int tab = tabs->addTab(create_page(i), p.name);
        tabs->setTabToolTip(tab, QString::fromStdString(devs[i]->name + " : " + devs[i]->type));
    }

    connect(&poll_timer, &QTimer::timeout, this, &SerialWindow::poll);
    poll_timer.start(POLL_MS);
}

SerialWindow::~SerialWindow()
{
    //Устройства здесь могут быть уже удалены: отвод снят в closeEvent, а
    //файлы закрываем без обращения к ним
    for (auto & p : ports) close_file(p);
}

QWidget * SerialWindow::create_page(size_t i)
{
    Port & p = ports[i];
    QWidget * page = new QWidget(tabs);
    QVBoxLayout * layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QToolBar * bar = new QToolBar(page);
    bar->setIconSize(QSize(24, 24));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    p.log_action = bar->addAction(QIcon(":/icons/log"), tr("Logging"));
    p.log_action->setCheckable(true);
    p.log_action->setToolTip(tr("Show the output of the port"));

    p.file_action = bar->addAction(QIcon(":/icons/file_save"), tr("To file"));
    p.file_action->setCheckable(true);
    p.file_action->setToolTip(tr("Write the output of the port to a file while logging"));
    p.file_action->setChecked(e->read_setup("Serial", file_key(p), "0") == "1");

    QAction * clear_action = bar->addAction(QIcon(":/icons/clear"), tr("Clear"));
    clear_action->setToolTip(tr("Clear the window"));

    QWidget * spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    //Сначала charmap машины, если он у нее есть, потом таблицы dsk_tools
    p.charset = new QComboBox(bar);
    p.charset->setToolTip(tr("Encoding of the recoded text"));
    const std::string machine_charmap = e->get_system_data()->system_charmap;
    if (!machine_charmap.empty())
        p.charset->addItem(tr("Machine (%1)").arg(QString::fromStdString(machine_charmap)), CHARSET_MACHINE);
    for (int c = 0; c < CHARSET_COUNT; c++)
        p.charset->addItem(tr(CHARSETS[c].title), c);
    //Выбор запоминается для каждого порта каждой машины
    const QString saved = QString::fromStdString(e->read_setup("Serial", charset_key(p), ""));
    for (int c = 0; c < CHARSET_COUNT; c++)
        if (saved == CHARSETS[c].id) p.charset->setCurrentIndex(p.charset->findData(c));
    p.charset->setEnabled(mode == MODE_RECODED);
    bar->addWidget(p.charset);

    QToolButton * settings = new QToolButton(bar);
    settings->setIcon(QIcon(":/icons/kcmsystem"));
    settings->setToolTip(tr("Settings"));
    settings->setToolButtonStyle(Qt::ToolButtonIconOnly);
    settings->setPopupMode(QToolButton::InstantPopup);
    settings->setMenu(settings_menu);
    bar->addWidget(settings);

    p.text = new QPlainTextEdit(page);
    p.text->setReadOnly(true);
    p.text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    p.text->setMaximumBlockCount(MAX_LINES);
    p.text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    layout->addWidget(bar);
    layout->addWidget(p.text);

    //Только применить: сохраненное и так в ini, а каждая запись переписывает файл целиком
    set_charset(i, p.charset->currentIndex(), false);

    connect(p.log_action, &QAction::toggled, this, [this, i](bool on){ set_logging(i, on); });
    connect(p.file_action, &QAction::toggled, this, [this, i](bool on){ set_to_file(i, on); });
    connect(clear_action, &QAction::triggered, this, [this, i]{
        ports[i].data.clear();
        refill(ports[i]);
    });
    connect(p.charset, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this, i](int index){ set_charset(i, index); });
    return page;
}

void SerialWindow::select_device(ComputerDevice * d)
{
    for (size_t i = 0; i < ports.size(); i++)
        if (ports[i].dev == d) tabs->setCurrentIndex((int)i);
}

std::string SerialWindow::file_key(const Port & p)
{
    return "to_file_" + e->settings_key() + "_" + p.dev->name;
}

std::string SerialWindow::charset_key(const Port & p)
{
    return "charset_" + e->settings_key() + "_" + p.dev->name;
}

//Новая кодировка перерисовывает все поле из принятого
void SerialWindow::set_charset(size_t i, int index, bool save)
{
    Port & p = ports[i];
    const int c = p.charset->itemData(index).toInt();
    const bool known = c >= 0 && c < CHARSET_COUNT;
    p.table = nullptr;
    p.koi7 = false;
    if (known) {
        const dsk_tools::CharmapInfo info = dsk_tools::init_charmap(CHARSETS[c].id);
        p.table = info.charmap;
        p.koi7 = info.is_koi7;
    }
    if (save) e->write_setup("Serial", charset_key(p), known ? CHARSETS[c].id : "machine");
    build_glyphs(p);
    refill(p);
}

//Поле заново из буфера: SO/SI и начало строки считаются с его начала
void SerialWindow::refill(Port & p)
{
    p.text->clear();
    p.koi7_high = 0;
    p.line_start = true;
    p.line_len = 0;
    append_text(p, render(p, reinterpret_cast<const uint8_t*>(p.data.constData()), (size_t)p.data.size()));
}

//----------------------------- Логирование ---------------------------------//

void SerialWindow::set_logging(size_t i, bool on)
{
    Port & p = ports[i];
    if (on) {
        p.tap->set_tap(true);
        if (p.file_action->isChecked()) open_file(p);
    } else {
        drain(p);
        p.tap->set_tap(false);
        close_file(p);
    }
}

void SerialWindow::set_to_file(size_t i, bool on)
{
    Port & p = ports[i];
    e->write_setup("Serial", file_key(p), on ? "1" : "0");
    if (!p.log_action->isChecked()) return;
    //Принятое до нажатия идет еще по-старому
    drain(p);
    if (on)
        open_file(p);
    else
        close_file(p);
}

//Каждое начало записи - новый файл: имя порта и время
bool SerialWindow::open_file(Port & p)
{
    close_file(p);
    const QString dir = folder();
    const QString stem = dir + "/" + QString::fromStdString(p.dev->name) + "_"
                         + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QString path = stem + ".log";
    for (int n = 2; QFile::exists(path); n++)
        path = stem + "-" + QString::number(n) + ".log";

    QFile * f = new QFile(path);
    if (!QDir().mkpath(dir) || !f->open(QIODevice::WriteOnly)) {
        delete f;
        QSignalBlocker block(p.file_action);
        p.file_action->setChecked(false);
        QMessageBox::warning(this, tr("I/O ports"),
                             tr("Cannot create file %1").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    p.file = f;
    return true;
}

void SerialWindow::close_file(Port & p)
{
    if (p.file == nullptr) return;
    p.file->close();
    delete p.file;
    p.file = nullptr;
}

//Забрать снятое портом: в файл как есть, в поле - в выбранном виде
void SerialWindow::drain(Port & p)
{
    p.tap->take_tap(chunk);
    if (chunk.empty()) return;

    if (p.file != nullptr) {
        p.file->write(reinterpret_cast<const char*>(chunk.data()), (qint64)chunk.size());
        p.file->flush();
    }
    p.data.append(reinterpret_cast<const char*>(chunk.data()), (int)chunk.size());
    if (p.data.size() > DATA_LIMIT) p.data.remove(0, p.data.size() - DATA_LIMIT);
    append_text(p, render(p, chunk.data(), chunk.size()));
}

void SerialWindow::poll()
{
    for (auto & p : ports)
        if (p.log_action->isChecked()) drain(p);
}

void SerialWindow::stop_all()
{
    poll_timer.stop();
    for (auto & p : ports) {
        if (!p.log_action->isChecked()) continue;
        drain(p);
        p.tap->set_tap(false);
        close_file(p);
    }
}

bool SerialWindow::writing_to_file() const
{
    for (const auto & p : ports)
        if (p.file != nullptr) return true;
    return false;
}

bool SerialWindow::confirm_close()
{
    QStringList writing;
    for (const auto & p : ports)
        if (p.file != nullptr) writing << p.name;
    if (writing.isEmpty()) return true;
    return QMessageBox::question(this, tr("I/O ports"),
                                 tr("Logging to a file is in progress (%1). Close the window and stop it?")
                                    .arg(writing.join(", ")),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

//Крестик и Esc закрывают разными путями: Esc идет через reject(), и
//closeEvent() на нем не приходит (Qt 5 и 6), поэтому отвод снимают оба.
//Спрашивают один раз, повторный stop_all() ничего не делает
bool SerialWindow::finish_close()
{
    if (!confirmed && !confirm_close()) return false;
    confirmed = true;
    stop_all();
    return true;
}

void SerialWindow::closeEvent(QCloseEvent *event)
{
    if (!finish_close()) {
        event->ignore();
        return;
    }
    GenericDbgWnd::closeEvent(event);
}

void SerialWindow::reject()
{
    if (!finish_close()) return;
    GenericDbgWnd::reject();
}

//------------------------------ Отображение --------------------------------//

//Текст: перевод строки - LF, CR пропускается (CR LF), прочие управляющие
//коды не видны. Байты - числа в системе машины через пробел, строка
//кончается на байте LF, чтобы вывод не сливался в один абзац.
//
//Вывод без LF (графика принтера, двоичный поток, строки по CR) рос одним
//абзацем, и каждая вставка раскладывала его заново в потоке GUI: строка
//переносится сама через BYTES_PER_LINE байтов или TEXT_LINE_MAX знаков. И
//за раз в поле идет не больше RENDER_LIMIT байтов, последних: пропущенные
//только переключают SO/SI
QString SerialWindow::render(Port & p, const uint8_t * bytes, size_t count)
{
    QString s;
    if (count > RENDER_LIMIT) {
        const size_t skip = count - RENDER_LIMIT;
        if (p.koi7)
            for (size_t i = 0; i < skip; i++) {
                if ((bytes[i] & 0x7F) == 0x0E) p.koi7_high = 0x80;
                if ((bytes[i] & 0x7F) == 0x0F) p.koi7_high = 0;
            }
        bytes += skip;
        count = RENDER_LIMIT;
        if (!p.line_start) s += '\n';
        s += QStringLiteral("...\n");
        p.line_start = true;
        p.line_len = 0;
    }
    s.reserve((int)(count * ((mode == MODE_BYTES) ? 4 : 1)) + 16);
    const int digits = cell_digits(base, false);

    for (size_t i = 0; i < count; i++) {
        const uint8_t b = bytes[i];
        if (p.koi7 && (b & 0x7F) == 0x0E) p.koi7_high = 0x80;
        if (p.koi7 && (b & 0x7F) == 0x0F) p.koi7_high = 0;

        if (mode == MODE_BYTES) {
            if (!p.line_start) s += ' ';
            s += fmt_num(b, base, digits);
            p.line_start = (b == 0x0A || ++p.line_len >= BYTES_PER_LINE);
            if (p.line_start) {
                s += '\n';
                p.line_len = 0;
            }
            continue;
        }
        if (b == 0x0A) {
            s += '\n';
            p.line_start = true;
            p.line_len = 0;
            continue;
        }
        if (p.koi7 && ((b & 0x7F) == 0x0E || (b & 0x7F) == 0x0F)) continue;
        if (b == 0x09) s += '\t';
        else if (b < 0x20 || b == 0x7F) continue;
        else if (mode == MODE_ORIGINAL) s += QChar((ushort)b);
        else s += p.glyphs[p.koi7 ? ((b & 0x7F) | p.koi7_high) : b];
        p.line_start = false;
        if (++p.line_len >= TEXT_LINE_MAX) {
            s += '\n';
            p.line_start = true;
            p.line_len = 0;
        }
    }
    return s;
}

//Знаки кодировки готовыми строками: по строке std::string на каждый
//принятый байт выходило по выделению памяти
void SerialWindow::build_glyphs(Port & p)
{
    for (unsigned i = 0; i < 256; i++)
        p.glyphs[i] = QString::fromStdString((p.table != nullptr) ? (*p.table)[i]
                                                                 : e->translate_char(i));
}

void SerialWindow::append_text(Port & p, const QString & s)
{
    if (s.isEmpty()) return;
    QScrollBar * sb = p.text->verticalScrollBar();
    const bool at_bottom = sb->value() == sb->maximum();
    QTextCursor c(p.text->document());
    c.movePosition(QTextCursor::End);
    c.insertText(s);
    if (at_bottom) sb->setValue(sb->maximum());
}

//Режим, как и кодировка, перерисовывает поле из принятого
void SerialWindow::set_mode(int new_mode)
{
    mode = new_mode;
    e->write_setup("Serial", "mode", MODE_NAMES[mode]);
    for (auto & p : ports) {
        p.charset->setEnabled(mode == MODE_RECODED);
        refill(p);
    }
}

//------------------------------- Папка -------------------------------------//

QString SerialWindow::folder()
{
    QString dir = QString::fromStdString(e->read_setup("Serial", "folder", ""));
    if (dir.isEmpty()) dir = QString::fromStdString(e->work_path) + "/serial";
    return QDir::cleanPath(dir);
}

void SerialWindow::choose_folder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder for logs"), folder());
    if (!dir.isEmpty()) e->write_setup("Serial", "folder", QDir::toNativeSeparators(dir).toStdString());
}
