// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the БК screen

#include "bkscreenwindow.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "dialogs/numfmt.h"
#include "emulator/devices/specific/a16m.h"
#include "emulator/devices/specific/bk_display.h"
#include "emulator/devices/specific/smk512.h"

static const unsigned int LINE_BYTES = 64;                  //a line of the screen
static const unsigned int LINES = 256;
static const unsigned int SCREEN_BYTES = LINE_BYTES * LINES;    //040000
static const unsigned int CPU_SPACE = 0x10000;
static const int CPU_SCREEN_PAGE = 1;                       //040000, where the БК0010 screen is
static const unsigned int BOARD_WINDOW = 0100000;           //where the СМК-512 and А16М show their memory
static const unsigned int BOARD_SEGMENT = 010000;           //they do that by 4 KB

//The picture always takes the proportions of the screen, 512 x 256
static const int VIEW_W = 512;
static const int VIEW_H = 256;

//Sources that are not a RAM device by name
static const char * const SRC_DISPLAY = "@display";
static const char * const SRC_CPU = "@cpu";

//------------------------------- BKScreenPicture -------------------------------//

BKScreenPicture::BKScreenPicture(QWidget *parent):
    QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(VIEW_W / 2, VIEW_H / 2);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BKScreenPicture::set_image(const QImage &image)
{
    m_image = image;
    update();
}

//Fitted into the widget; a whole number of times once it is larger, so that
//the dots stay even
QRect BKScreenPicture::target() const
{
    double scale = std::min((double)width() / VIEW_W, (double)height() / VIEW_H);
    if (scale >= 1) scale = (int)scale;
    const int w = (int)(VIEW_W * scale);
    const int h = (int)(VIEW_H * scale);
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void BKScreenPicture::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Window));
    if (!m_image.isNull()) p.drawImage(target(), m_image);
}

void BKScreenPicture::mouseMoveEvent(QMouseEvent *event)
{
    const QRect t = target();
    const QPoint pos = event->pos();
    if (m_image.isNull() || !t.contains(pos)) {
        emit hovered(-1, -1);
        return;
    }
    emit hovered((pos.x() - t.left()) * m_image.width() / t.width(),
                 (pos.y() - t.top()) * m_image.height() / t.height());
}

void BKScreenPicture::leaveEvent(QEvent *)
{
    emit hovered(-1, -1);
}

//------------------------------- BKScreenWindow -------------------------------//

BKScreenWindow::BKScreenWindow(QWidget *parent, Emulator * e, BKDisplay * d):
    GenericDbgWnd(parent),
    e(e),
    d(d),
    m_start(0),
    m_bytes(SCREEN_BYTES, 0),
    m_have(0),
    m_shown_mode(BK_MODE_RGB),
    m_hover_x(-1),
    m_hover_y(-1)
{
    m_cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu", false));
    m_base = view_base(d);

    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(560, 400);

    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout * top = new QHBoxLayout();
    top->addWidget(new QLabel(tr("Source:"), this));
    m_source = new QComboBox(this);
    m_source->addItem(tr("As the display"), SRC_DISPLAY);
    if (m_cpu != nullptr) m_source->addItem(tr("Processor"), SRC_CPU);
    for (unsigned int i = 0; i < e->dm->device_count; i++)
        if (RAM * ram = dynamic_cast<RAM*>(e->dm->get_device(i)->get()))
            m_source->addItem(QString::fromStdString(ram->name), QString::fromStdString(ram->name));
    top->addWidget(m_source);

    top->addWidget(new QLabel(tr("Page:"), this));
    m_page = new QSpinBox(this);
    m_page->setWrapping(true);
    top->addWidget(m_page);

    //Where the page starts, or for the СМК-512 and А16М memory where the processor sees it
    m_where = new QLabel(this);
    m_where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    //A page seen by parts makes a long text: it does not widen the window
    m_where->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(m_where, 1);
    layout->addLayout(top);

    QHBoxLayout * second = new QHBoxLayout();
    second->addWidget(new QLabel(tr("Mode:"), this));
    m_mode = new QComboBox(this);
    m_mode->addItem(tr("As the display"), -1);
    m_mode->addItem(tr("Colour"), BK_MODE_RGB);
    m_mode->addItem(tr("Black and white"), BK_MODE_BW);
    m_mode->addItem(tr("Grey"), BK_MODE_MONO);
    second->addWidget(m_mode);

    second->addWidget(new QLabel(tr("Palette:"), this));
    m_palette = new QComboBox(this);
    m_palette->addItem(tr("As the display"), -1);
    for (int p = 0; p < 16; p++) m_palette->addItem(QString::number(p), p);
    second->addWidget(m_palette);

    m_info = new QLabel(this);
    second->addWidget(m_info);
    second->addStretch(1);
    layout->addLayout(second);

    m_picture = new BKScreenPicture(this);
    layout->addWidget(m_picture, 1);

    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    load_choice();

    connect(m_source, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        //The processor's screen is at 040000, a RAM device's at its start
        set_page_range();
        set_page((source_key() == SRC_CPU) ? CPU_SCREEN_PAGE : 0);
    });
    connect(m_page, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int page) {
        set_page(page);
    });
    connect(m_mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        save_choice();
        update_view();
    });
    connect(m_palette, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        save_choice();
        update_view();
    });
    connect(m_picture, &BKScreenPicture::hovered, this, [this](int x, int y) {
        m_hover_x = x;
        m_hover_y = y;
        show_hover();
    });

    update_view();

    //The machine runs meanwhile: what it draws shows up
    QTimer * timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &BKScreenWindow::update_view);
    timer->start(200);
}

QString BKScreenWindow::source_key() const
{
    return m_source->currentData().toString();
}

//The bytes the source has: the processor's space, or the RAM device's size.
//"As the display" is the video RAM at the moment, which may be any of two
unsigned int BKScreenWindow::source_size() const
{
    const QString key = source_key();
    if (key == SRC_CPU) return CPU_SPACE;
    std::string name = key.toStdString();
    if (key == SRC_DISPLAY) {
        DeviceFieldValue v;
        BKDisplay * disp = d;
        e->invoke([disp, &v]() { disp->get_field("vram", 0, 0, v); });
        name = v.text;
    }
    RAM * ram = dynamic_cast<RAM*>(e->dm->get_device_by_name(name, false));
    return (ram != nullptr) ? ram->get_size() : 0;
}

//As many pages as the source has, the last one possibly short
void BKScreenWindow::set_page_range()
{
    const unsigned int size = source_size();
    const int pages = (int)((size + SCREEN_BYTES - 1) / SCREEN_BYTES);
    const QSignalBlocker block(m_page);
    m_page->setRange(0, std::max(pages, 1) - 1);
}

void BKScreenWindow::set_page(int page)
{
    if (m_page->value() != page) {
        const QSignalBlocker block(m_page);
        m_page->setValue(page);
    }
    m_start = (unsigned int)m_page->value() * SCREEN_BYTES;
    save_choice();
    update_view();
}

//Where the page starts in the source; a page of the СМК-512 or А16М memory
//says only where the processor sees it: whole at one address, by parts
//(the processor addresses of the parts), or not at all
void BKScreenWindow::show_where(bool board, const std::vector<std::pair<unsigned int, unsigned int>> &mapped)
{
    QString text;
    if (board) {
        const unsigned int parts = SCREEN_BYTES / BOARD_SEGMENT;
        bool whole = mapped.size() == parts;
        for (unsigned int i = 0; whole && i < parts; i++)
            whole = mapped[i].first == i && mapped[i].second == mapped[0].second + i;
        const int cpu_digits = addr_digits(CPU_SPACE - 1, m_base);
        if (mapped.empty())
            text = tr("not mapped");
        else if (whole)
            text = tr("mapped at %1").arg(fmt_num(BOARD_WINDOW + mapped[0].second * BOARD_SEGMENT, m_base, cpu_digits));
        else {
            QStringList list;
            for (const auto & m : mapped)
                list << fmt_num(BOARD_WINDOW + m.second * BOARD_SEGMENT, m_base, cpu_digits);
            text = tr("partly mapped: %1").arg(list.join(", "));
        }
    } else {
        const bool cpu = source_key() == SRC_CPU;
        const unsigned int size = cpu ? CPU_SPACE : source_size();
        text = tr("starts at %1").arg(fmt_num(m_start, m_base, addr_digits((size > 0) ? size - 1 : 0, m_base)));
    }
    m_where->setText(text);
    m_where->setToolTip(text);
}

//The byte under the mouse: its address in the source and its value
void BKScreenWindow::show_hover()
{
    if (m_hover_x < 0 || m_hover_y < 0) {
        m_status->clear();
        return;
    }
    const unsigned int dots = BKDisplay::wide_mode(m_shown_mode) ? 8 : 4;
    const unsigned int index = (unsigned int)m_hover_y * LINE_BYTES + (unsigned int)m_hover_x / dots;
    if (index >= SCREEN_BYTES) return;
    unsigned int address = m_start + index;
    const bool cpu = source_key() == SRC_CPU;
    if (cpu) address %= CPU_SPACE;
    const QString at = fmt_num(address, m_base, addr_digits(cpu ? CPU_SPACE - 1 : address, m_base));
    const QString value = (index < m_have) ? fmt_num(m_bytes[index], m_base, cell_digits(m_base, false)) : "-";
    m_status->setText(tr("%1: %2   line %3, byte %4")
                      .arg(at, value)
                      .arg(m_hover_y).arg(index % LINE_BYTES));
}

//The view is kept between runs in [BKScreen] of the ini
void BKScreenWindow::load_choice()
{
    const QString source = QString::fromStdString(e->read_setup("BKScreen", "source", SRC_DISPLAY));
    const int s = m_source->findData(source);
    m_source->setCurrentIndex((s >= 0) ? s : 0);

    set_page_range();
    bool ok = false;
    const int page = QString::fromStdString(e->read_setup("BKScreen", "page", "")).toInt(&ok);
    const QSignalBlocker block(m_page);
    m_page->setValue(ok ? page : ((source_key() == SRC_CPU) ? CPU_SCREEN_PAGE : 0));
    m_start = (unsigned int)m_page->value() * SCREEN_BYTES;

    const int mode = m_mode->findData(QString::fromStdString(e->read_setup("BKScreen", "mode", "-1")).toInt());
    m_mode->setCurrentIndex((mode >= 0) ? mode : 0);
    const int palette = m_palette->findData(QString::fromStdString(e->read_setup("BKScreen", "palette", "-1")).toInt());
    m_palette->setCurrentIndex((palette >= 0) ? palette : 0);
}

void BKScreenWindow::save_choice()
{
    e->write_setup("BKScreen", "source", source_key().toStdString());
    e->write_setup("BKScreen", "page", std::to_string(m_page->value()));
    e->write_setup("BKScreen", "mode", std::to_string(m_mode->currentData().toInt()));
    e->write_setup("BKScreen", "palette", std::to_string(m_palette->currentData().toInt()));
}

//The 16 KB and the display's own mode and palette are taken in one pass on the
//emulation thread, so a frame is not torn by a write in the middle of a copy;
//the picture is drawn from that copy here
void BKScreenWindow::update_view()
{
    const QString key = source_key();
    const bool cpu = key == SRC_CPU;
    const std::string name = key.toStdString();
    const unsigned int start = m_start;
    BKDisplay * disp = d;
    CPU * proc = m_cpu;
    DeviceManager * dm = e->dm;
    std::vector<uint8_t> & bytes = m_bytes;
    unsigned int have = 0;
    unsigned int disp_mode = BK_MODE_RGB;
    unsigned int disp_palette = 0;
    std::string vram;
    bool board = false;
    std::vector<std::pair<unsigned int, unsigned int>> mapped;    //4 KB of the page, the window segment
    e->invoke([&]() {
        DeviceFieldValue v;
        if (disp->get_field("vram", 0, 0, v)) vram = v.text;
        DeviceFieldValue m;
        if (disp->get_field("mode", 0, 0, m) && !m.values.empty()) disp_mode = m.values[0];
        DeviceFieldValue p;
        if (disp->get_field("palette", 0, 0, p) && !p.values.empty()) disp_palette = p.values[0];

        if (cpu) {
            for (unsigned int i = 0; i < SCREEN_BYTES; i++)
                bytes[i] = proc->peek_mem((start + i) % CPU_SPACE) & 0xFF;
            have = SCREEN_BYTES;
            return;
        }
        RAM * ram = dynamic_cast<RAM*>(dm->get_device_by_name((key == SRC_DISPLAY) ? vram : name, false));
        const unsigned int size = (ram != nullptr) ? ram->get_size() : 0;

        //The memory of a СМК-512 or an А16М: which of its 4 KB segments the
        //board shows to the processor now, as it reads them
        for (unsigned int i = 0; ram != nullptr && i < dm->device_count; i++) {
            ComputerDevice * dev = dm->get_device(i)->get();
            SMK512 * smk512 = dynamic_cast<SMK512*>(dev);
            A16M * a16m = dynamic_cast<A16M*>(dev);
            if (!(smk512 != nullptr && smk512->board_ram() == ram) && !(a16m != nullptr && a16m->board_ram() == ram))
                continue;
            board = true;
            const unsigned int value = (smk512 != nullptr) ? smk512->latched() : 0;
            const unsigned int first = start / BOARD_SEGMENT;
            for (unsigned int w = 0; w < CPU_SPACE - BOARD_WINDOW; w += BOARD_SEGMENT) {
                int src;
                unsigned int segment;
                if (smk512 != nullptr) {
                    src = SMK512::layout_source(value, w);
                    segment = SMK512::page_of(value) * 8 + (unsigned int)src;
                } else {
                    src = a16m->layout_source(w);
                    segment = (unsigned int)src;
                }
                if (src < 0 || src == SMK512::SRC_ROM || src == A16M::SRC_ROM) continue;
                if (segment >= first && segment < first + SCREEN_BYTES / BOARD_SEGMENT)
                    mapped.push_back(std::make_pair(segment - first, w / BOARD_SEGMENT));
            }
            std::sort(mapped.begin(), mapped.end());
            break;
        }

        for (unsigned int i = 0; i < SCREEN_BYTES; i++) {
            if (start + i < size) {
                bytes[i] = ram->get_direct(start + i) & 0xFF;
                have = i + 1;
            } else bytes[i] = 0;
        }
    });
    m_have = have;
    show_where(board, mapped);

    const int chosen_mode = m_mode->currentData().toInt();
    const int chosen_palette = m_palette->currentData().toInt();
    const unsigned int mode = (chosen_mode >= 0) ? (unsigned int)chosen_mode : disp_mode;
    const unsigned int palette = (chosen_palette >= 0) ? (unsigned int)chosen_palette : disp_palette;
    m_shown_mode = mode;
    m_info->setText(tr("display: %1, palette %2").arg(QString::fromStdString(vram)).arg(disp_palette));

    const bool wide = BKDisplay::wide_mode(mode);
    const unsigned int dots = wide ? 8 : 4;
    QImage image((int)(LINE_BYTES * dots), (int)LINES, QImage::Format_RGB32);
    uint32_t colors[4];
    for (unsigned int c = 0; c < 4; c++) colors[c] = 0xFF000000u | BKDisplay::dot_rgb(mode, palette, c);
    for (unsigned int y = 0; y < LINES; y++) {
        uint32_t * out = reinterpret_cast<uint32_t*>(image.scanLine((int)y));
        const uint8_t * line = &m_bytes[y * LINE_BYTES];
        for (unsigned int i = 0; i < LINE_BYTES; i++) {
            const uint8_t b = line[i];
            //The lowest bits are the leftmost dot, as on the screen
            if (wide) {
                for (unsigned int k = 0; k < 8; k++) out[i * 8 + k] = colors[(b >> k) & 1];
            } else {
                for (unsigned int k = 0; k < 4; k++) out[i * 4 + k] = colors[(b >> (k * 2)) & 3];
            }
        }
    }
    m_picture->set_image(image);
    show_hover();
}

GenericDbgWnd * CreateBKScreenWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    if (BKDisplay * disp = dynamic_cast<BKDisplay*>(d)) return new BKScreenWindow(parent, e, disp);
    return nullptr;
}
