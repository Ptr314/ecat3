// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the БК screen

#include "bkscreenwindow.h"

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "dialogs/numfmt.h"
#include "emulator/devices/specific/bk_display.h"

static const unsigned int LINE_BYTES = 64;                  //a line of the screen
static const unsigned int LINES = 256;
static const unsigned int SCREEN_BYTES = LINE_BYTES * LINES;    //040000
static const unsigned int CPU_SPACE = 0x10000;
static const unsigned int CPU_SCREEN = 040000;              //where the БК0010 screen is

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

    top->addWidget(new QLabel(tr("Address:"), this));
    m_address = new QLineEdit(this);
    m_address->setMaximumWidth(90);
    top->addWidget(m_address);

    //Steps by a line and by a whole screen
    const struct { const char * text; int delta; } steps[] = {
        {"<<", -(int)SCREEN_BYTES}, {"<", -(int)LINE_BYTES}, {">", (int)LINE_BYTES}, {">>", (int)SCREEN_BYTES}
    };
    for (const auto & s : steps) {
        QPushButton * b = new QPushButton(s.text, this);
        b->setMaximumWidth(32);
        b->setAutoDefault(false);
        const int delta = s.delta;
        connect(b, &QPushButton::clicked, this, [this, delta]() { step(delta); });
        top->addWidget(b);
    }
    top->addStretch(1);
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
    show_address();

    connect(m_source, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        //The processor's screen is at 040000, a RAM device's at its start
        set_start((source_key() == SRC_CPU) ? CPU_SCREEN : 0);
    });
    connect(m_address, &QLineEdit::returnPressed, this, [this]() {
        try {
            set_start(parse_input(m_address->text(), m_base));
        } catch (const std::exception &) {
            show_address();
        }
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

//The processor's space wraps around, a RAM device stops at its last byte
void BKScreenWindow::set_start(unsigned int start)
{
    const unsigned int size = source_size();
    if (source_key() == SRC_CPU) start %= CPU_SPACE;
    else if (size == 0) start = 0;
    else if (start >= size) start = size - 1;
    m_start = start;
    show_address();
    save_choice();
    update_view();
}

void BKScreenWindow::step(int delta)
{
    if (source_key() == SRC_CPU) {
        set_start((m_start + CPU_SPACE + (unsigned int)(delta % (int)CPU_SPACE)) % CPU_SPACE);
        return;
    }
    if (delta < 0 && m_start < (unsigned int)(-delta)) set_start(0);
    else set_start(m_start + (unsigned int)delta);
}

void BKScreenWindow::show_address()
{
    const unsigned int size = source_size();
    m_address->setText(fmt_num(m_start, m_base, addr_digits((size > 0) ? size - 1 : 0, m_base)));
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

    bool ok = false;
    const unsigned int start = QString::fromStdString(e->read_setup("BKScreen", "address", "")).toUInt(&ok);
    m_start = ok ? start : ((source_key() == SRC_CPU) ? CPU_SCREEN : 0);

    const int mode = m_mode->findData(QString::fromStdString(e->read_setup("BKScreen", "mode", "-1")).toInt());
    m_mode->setCurrentIndex((mode >= 0) ? mode : 0);
    const int palette = m_palette->findData(QString::fromStdString(e->read_setup("BKScreen", "palette", "-1")).toInt());
    m_palette->setCurrentIndex((palette >= 0) ? palette : 0);
}

void BKScreenWindow::save_choice()
{
    e->write_setup("BKScreen", "source", source_key().toStdString());
    e->write_setup("BKScreen", "address", std::to_string(m_start));
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
        for (unsigned int i = 0; i < SCREEN_BYTES; i++) {
            if (start + i < size) {
                bytes[i] = ram->get_direct(start + i) & 0xFF;
                have = i + 1;
            } else bytes[i] = 0;
        }
    });
    m_have = have;

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
