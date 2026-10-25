// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the СМК-512 memory board

#include "smkwindow.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "dialogs/numfmt.h"
#include "dialogs/debugger/memorywindow.h"
#include "dialogs/debugger/memoryview.h"
#include "emulator/devices/specific/smk512.h"

static const unsigned int WINDOW = 0100000;     //the board's window, 0100000-0177777
static const unsigned int PAGE_SIZE = 0100000;  //32 KB
static const unsigned int HIGH = 077000;        //0177000: the top of segment 7

//The modes in the order of their codes, bits 4-6 of the register
static const char * const MODE_NAMES[8] = { "Hlt11", "All", "RAM11", "Std10", "Hlt10", "RAM10", "Std11", "SYS" };

//The register bits of a page: 10, 2, 3 and 0, see SMK512::base_of()
static unsigned int page_bits(unsigned int page)
{
    unsigned int v = 0;
    if (page & 1) v |= 02000;
    if (page & 2) v |= 4;
    if (page & 4) v |= 010;
    if (page & 8) v |= 1;
    return v;
}

//The byte of the board's memory a segment of the window shows
static unsigned int board_address(unsigned int value, int src, unsigned int offset)
{
    return ((SMK512::page_of(value) * 8 + (unsigned int)src) << 12) + (offset & 07777);
}

SMKWindow::SMKWindow(QWidget *parent, Emulator * e, SMK512 * d):
    GenericDbgWnd(parent),
    e(e),
    d(d),
    m_panel(nullptr),
    m_latched(0),
    m_read_off(false),
    m_sel_mode(7),
    m_sel_page(0)
{
    m_cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu", false));
    m_base = view_base(d);

    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(960, 520);

    QHBoxLayout * layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);

    QWidget * left = new QWidget(this);
    left->setFixedWidth(300);
    QVBoxLayout * left_box = new QVBoxLayout(left);
    left_box->setContentsMargins(4, 4, 4, 4);
    QFormLayout * form = new QFormLayout();

    m_view = new QComboBox(left);
    m_view->addItem(tr("Window now"));
    m_view->addItem(tr("Window in a mode"));
    m_view->addItem(tr("Page of the board"));
    form->addRow(tr("View:"), m_view);

    m_mode = new QComboBox(left);
    for (unsigned int m = 0; m < 8; m++)
        m_mode->addItem(fmt_num(m << 4, m_base, 3) + " " + MODE_NAMES[m], m << 4);
    form->addRow(tr("Mode:"), m_mode);

    m_page = new QSpinBox(left);
    m_page->setRange(0, 15);
    m_page->setDisplayIntegerBase(m_base);
    form->addRow(tr("Page:"), m_page);
    left_box->addLayout(form);

    m_register = new QLabel(left);
    left_box->addWidget(m_register);

    m_layout = new QTreeWidget(left);
    m_layout->setColumnCount(2);
    m_layout->setHeaderLabels(QStringList() << tr("Address") << tr("Contents"));
    m_layout->setRootIsDecorated(false);
    m_layout->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    //A segment clicked or chosen with Enter is brought to the top of the dump
    auto show_segment = [this](QTreeWidgetItem * item) {
        if (item != nullptr && m_panel != nullptr) m_panel->view()->go_to(item->data(0, Qt::UserRole).toUInt());
    };
    connect(m_layout, &QTreeWidget::itemClicked, this, show_segment);
    connect(m_layout, &QTreeWidget::itemActivated, this, show_segment);
    left_box->addWidget(m_layout, 1);
    layout->addWidget(left);

    m_dump_box = new QVBoxLayout();
    layout->addLayout(m_dump_box, 1);

    load_choice();
    read_board();
    show_selection();
    show_register();
    rebuild();

    connect(m_view, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        save_choice();
        rebuild();
    });
    connect(m_mode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_sel_mode = index;
        save_choice();
        rebuild();
    });
    connect(m_page, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int page) {
        m_sel_page = page;
        save_choice();
        rebuild();
    });

    //The machine runs meanwhile: what it writes and how it switches show up
    QTimer * timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SMKWindow::update_view);
    timer->start(500);
}

//The register the selectors make: mode and page
unsigned int SMKWindow::selected_value() const
{
    return m_mode->currentData().toUInt() | page_bits((unsigned int)m_page->value());
}

//What the processor reads at an offset of the window with the register set so
QString SMKWindow::describe(unsigned int value, unsigned int offset) const
{
    const int src = SMK512::layout_source(value, offset);
    if (src == SMK512::SRC_ROM) return tr("ROM of the board");
    if (src == SMK512::SRC_NONE) {
        const unsigned int segment = (offset >= HIGH) ? 7 : offset >> 12;
        return d->machine_disabled(value, segment) ? tr("no answer") : tr("memory of the machine");
    }
    return tr("page %1, segment %2").arg(SMK512::page_of(value), 0, m_base).arg(src);
}

//The cell of the board's memory behind an offset of the window, for the status line
QString SMKWindow::board_text(unsigned int value, unsigned int offset) const
{
    const int src = SMK512::layout_source(value, offset);
    if (src == SMK512::SRC_ROM) {
        const unsigned int a = offset & 07777;
        return QString::fromStdString(d->board_rom()->name) + " + " + fmt_num(a, m_base, addr_digits(a, m_base));
    }
    if (src == SMK512::SRC_NONE) return describe(value, offset);
    const unsigned int a = board_address(value, src, offset);
    return QString::fromStdString(d->board_ram()->name) + " + " + fmt_num(a, m_base, addr_digits(a, m_base))
           + " (" + describe(value, offset) + ")";
}

//What the processor sees: the board, the memory of the machine and its registers
MemorySource SMKWindow::source_now()
{
    MemorySource s;
    s.title = QString::fromStdString(d->name);
    s.size = PAGE_SIZE;
    s.base = m_base;
    s.origin = WINDOW;
    if (m_cpu == nullptr) {
        s.read = [](unsigned int) { return 0xFFu; };
        return s;
    }
    const MemorySource c = memory_of_cpu(m_cpu);
    s.read = [c](unsigned int a) { return c.read(WINDOW + a); };
    s.write = [c](unsigned int a, unsigned int v) { c.write(WINDOW + a, v); };
    s.writable = [c](unsigned int a) { return c.writable(WINDOW + a); };
    const unsigned int value = m_latched;
    MemoryMapper * mm = m_cpu->mm;
    const int base = m_base;
    s.owner = [this, value, mm, base](unsigned int a) -> QString {
        if (SMK512::layout_source(value, a) != SMK512::SRC_NONE || mm == nullptr) return board_text(value, a);
        unsigned int offset = 0;
        AddressableDevice * owner = mm->peek_owner(WINDOW + a, offset);
        if (owner == nullptr) return tr("nothing");
        return QString::fromStdString(owner->name) + " + " + fmt_num(offset, base, addr_digits(offset, base));
    };
    return s;
}

//The board's part of the window in another mode and page. The memory of the
//machine under the board is not reached there: the mapper answers for the mode
//set now, so those cells read FF
MemorySource SMKWindow::source_mode(unsigned int value)
{
    MemorySource s;
    s.title = QString::fromStdString(d->name);
    s.size = PAGE_SIZE;
    s.base = m_base;
    s.origin = WINDOW;
    Memory * ram = d->board_ram();
    Memory * rom = d->board_rom();
    s.read = [value, ram, rom](unsigned int a) -> unsigned int {
        const int src = SMK512::layout_source(value, a);
        if (src == SMK512::SRC_NONE) return 0xFF;
        if (src == SMK512::SRC_ROM) return rom->get_direct(a & 07777) & 0xFF;
        return ram->get_direct(board_address(value, src, a)) & 0xFF;
    };
    //Only the board's RAM is edited, at the cell the dump shows
    s.writable = [value](unsigned int a) {
        const int src = SMK512::layout_source(value, a);
        return src != SMK512::SRC_NONE && src != SMK512::SRC_ROM;
    };
    s.write = [value, ram](unsigned int a, unsigned int v) {
        const int src = SMK512::layout_source(value, a);
        if (src != SMK512::SRC_NONE && src != SMK512::SRC_ROM)
            ram->set_value(board_address(value, src, a), v & 0xFF, true);
    };
    s.owner = [this, value](unsigned int a) -> QString { return board_text(value, a); };
    return s;
}

//One 32 KB page of the board's RAM
MemorySource SMKWindow::source_page(unsigned int page)
{
    MemorySource s;
    s.title = QString::fromStdString(d->name);
    s.size = PAGE_SIZE;
    s.base = m_base;
    Memory * ram = d->board_ram();
    const unsigned int from = page * PAGE_SIZE;
    s.read = [ram, from](unsigned int a) { return ram->get_direct(from + a) & 0xFF; };
    s.writable = [](unsigned int) { return true; };
    s.write = [ram, from](unsigned int a, unsigned int v) { ram->set_value(from + a, v & 0xFF, true); };
    const int base = m_base;
    const QString name = QString::fromStdString(ram->name);
    s.owner = [name, from, base, page](unsigned int a) -> QString {
        return name + " + " + fmt_num(from + a, base, addr_digits(from + a, base)) + " ("
               + tr("page %1, segment %2").arg(page, 0, base).arg(a >> 12) + ")";
    };
    return s;
}

//The register as the board latched it, read where the board lives
void SMKWindow::read_board()
{
    SMK512 * smk = d;
    unsigned int value = 0;
    bool off = false;
    e->invoke([smk, &value, &off]() {
        value = smk->latched();
        off = smk->read_off();
    });
    m_latched = value;
    m_read_off = off;
}

void SMKWindow::show_register()
{
    QString s = tr("0177130: %1").arg(fmt_num(m_latched, m_base, (m_base == 8) ? 6 : 4));
    if (m_read_off) s += "  " + tr("reading off");
    m_register->setText(s);
}

//The selectors follow the board in the view of the window now
void SMKWindow::show_selection()
{
    const bool now = m_view->currentIndex() == VIEW_NOW;
    m_mode->setEnabled(m_view->currentIndex() == VIEW_MODE);
    m_page->setEnabled(!now);
    m_mode->blockSignals(true);
    m_page->blockSignals(true);
    m_mode->setCurrentIndex(now ? (int)((m_latched >> 4) & 7) : m_sel_mode);
    m_page->setValue(now ? (int)SMK512::page_of(m_latched) : m_sel_page);
    m_mode->blockSignals(false);
    m_page->blockSignals(false);
}

//The view, mode and page are kept between runs in [SMK512] of the ini
static int setup_int(Emulator * e, const char * key, int def, int max)
{
    bool ok = false;
    const int v = QString::fromStdString(e->read_setup("SMK512", key, "")).toInt(&ok);
    return (ok && v >= 0 && v <= max) ? v : def;
}

void SMKWindow::load_choice()
{
    m_sel_mode = setup_int(e, "mode", 7, 7);
    m_sel_page = setup_int(e, "page", 0, 15);
    m_view->blockSignals(true);
    m_view->setCurrentIndex(setup_int(e, "view", VIEW_NOW, VIEW_PAGE));
    m_view->blockSignals(false);
}

void SMKWindow::save_choice()
{
    e->write_setup("SMK512", "view", std::to_string(m_view->currentIndex()));
    e->write_setup("SMK512", "mode", std::to_string(m_sel_mode));
    e->write_setup("SMK512", "page", std::to_string(m_sel_page));
}

//The layout and the dump for the selection; the cursor stays where it was
void SMKWindow::rebuild()
{
    show_selection();
    const int view = m_view->currentIndex();
    const unsigned int value = (view == VIEW_NOW) ? m_latched : selected_value();

    m_layout->clear();
    if (view == VIEW_PAGE) {
        const unsigned int page = (unsigned int)m_page->value();
        for (unsigned int s = 0; s < 8; s++) {
            const unsigned int a = page * PAGE_SIZE + (s << 12);
            QTreeWidgetItem * item = new QTreeWidgetItem(m_layout);
            item->setText(0, fmt_num(s << 12, m_base, addr_digits(0, m_base)));
            item->setText(1, QString::fromStdString(d->board_ram()->name) + " + " + fmt_num(a, m_base, addr_digits(a, m_base)));
            item->setData(0, Qt::UserRole, s << 12);
        }
    } else {
        for (unsigned int s = 0; s < 9; s++) {
            const unsigned int offset = (s < 8) ? (s << 12) : HIGH;
            QTreeWidgetItem * item = new QTreeWidgetItem(m_layout);
            item->setText(0, fmt_num(WINDOW + offset, m_base, addr_digits(0, m_base)));
            item->setText(1, describe(value, offset));
            item->setData(0, Qt::UserRole, offset);
        }
    }

    unsigned int cursor = 0;
    if (m_panel != nullptr) {
        cursor = m_panel->view()->cursor();
        delete m_panel;
    }
    MemorySource src;
    if (view == VIEW_NOW) src = source_now();
    else if (view == VIEW_MODE) src = source_mode(value);
    else src = source_page((unsigned int)m_page->value());
    m_panel = new MemoryPanel(e, src, this);
    m_dump_box->addWidget(m_panel);
    connect(m_panel->view(), &MemoryView::memory_changed, this, [this]() { emit data_changed(this); });
    m_panel->view()->go_to(cursor);
}

void SMKWindow::update_view()
{
    const unsigned int was = m_latched;
    read_board();
    show_register();
    if (m_view->currentIndex() == VIEW_NOW && m_latched != was) rebuild();
    else if (m_panel != nullptr) m_panel->refresh();
}

GenericDbgWnd * CreateSMKWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    if (SMK512 * smk = dynamic_cast<SMK512*>(d)) return new SMKWindow(parent, e, smk);
    return CreateMemoryWindow(parent, e, d);
}
