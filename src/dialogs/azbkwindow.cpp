// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the AZBK controller's memory mapper

#include "azbkwindow.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "dialogs/numfmt.h"
#include "dialogs/debugger/debugstyle.h"
#include "dialogs/debugger/memorywindow.h"
#include "dialogs/debugger/memoryview.h"

static const unsigned int SPACE = 0200000;      //the processor's 64 KB
static const unsigned int IO_PAGE = 0177000;    //the windows end at 0176777
static const unsigned int TABLE_PAGES = 16;     //the run of AZ pages the table lists

//The БК0011М page whose shadow the AZ pages 4c..4c+3 are: c is the code of
//bits 14-12 of 0177716, as AZBK::write_716() sets the windows
static const unsigned int BK_PAGE_OF_CODE[8] = { 1, 5, 2, 3, 4, 7, 0, 6 };
static const unsigned int SMK_PAGE = 0200;      //0200-0377: the СМК-512 the board emulates
static const unsigned int FREE_PAGE = 0400;

static QString file_name(const std::string &path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

AZBKWindow::State::State():
    active(0), readonly(0), shadow(0), control(0), copy130(0), copy716(0), start(false), smk_flag(false)
{
    for (unsigned int n = 0; n < 16; n++) {
        win[n] = 0;
        rd[n] = wr[n] = ROUTE_PASS;
        ro[n] = false;
    }
}

bool AZBKWindow::State::operator==(const State &o) const
{
    for (unsigned int n = 0; n < 16; n++)
        if (win[n] != o.win[n] || rd[n] != o.rd[n] || wr[n] != o.wr[n] || ro[n] != o.ro[n] || below[n] != o.below[n])
            return false;
    return active == o.active && readonly == o.readonly && shadow == o.shadow && control == o.control
        && copy130 == o.copy130 && copy716 == o.copy716 && start == o.start && smk_flag == o.smk_flag;
}

AZBKWindow::AZBKWindow(QWidget *parent, Emulator * e, AZBK * d):
    GenericDbgWnd(parent),
    e(e),
    d(d),
    m_panel(nullptr),
    m_panel_view(-1)
{
    m_cpu = dynamic_cast<CPU*>(e->dm->get_device_by_name("cpu", false));
    m_ram = d->board_ram();
    m_base = view_base(d);
    //The configuration's: they stay while the machine is loaded
    m_slots = d->rom_slots();
    m_logo = d->logo_file();

    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(1360, 560);

    QHBoxLayout * layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    QSplitter * splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter);

    QWidget * left = new QWidget(splitter);
    QVBoxLayout * left_box = new QVBoxLayout(left);
    left_box->setContentsMargins(4, 4, 4, 4);
    QFormLayout * form = new QFormLayout();

    m_view = new QComboBox(left);
    m_view->addItem(tr("Processor addresses"));
    m_view->addItem(tr("Pages of the windows"));
    m_view->addItem(tr("AZ memory"));
    form->addRow(tr("View:"), m_view);

    m_page = new QSpinBox(left);
    m_page->setRange(0, (int)(m_ram->get_size() / AZBK::PAGE_BYTES) - 1);
    m_page->setDisplayIntegerBase(m_base);
    form->addRow(tr("Page:"), m_page);
    left_box->addLayout(form);

    m_registers = new QLabel(left);
    m_registers->setFont(dbg::font());
    m_registers->setWordWrap(true);
    m_registers->setTextInteractionFlags(Qt::TextSelectableByMouse);
    left_box->addWidget(m_registers);

    m_layout = new QTreeWidget(left);
    m_layout->setRootIsDecorated(false);
    m_layout->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    //A row clicked or chosen with Enter is brought to the top of the dump
    auto show_row = [this](QTreeWidgetItem * item) {
        if (item != nullptr && m_panel != nullptr) m_panel->view()->go_to(item->data(0, Qt::UserRole).toUInt());
    };
    connect(m_layout, &QTreeWidget::itemClicked, this, show_row);
    connect(m_layout, &QTreeWidget::itemActivated, this, show_row);
    left_box->addWidget(m_layout, 1);

    QWidget * right = new QWidget(splitter);
    m_dump_box = new QVBoxLayout(right);
    m_dump_box->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 720 << 640);

    m_save_timer = new QTimer(this);
    m_save_timer->setSingleShot(true);
    m_save_timer->setInterval(1000);
    connect(m_save_timer, &QTimer::timeout, this, &AZBKWindow::write_choice);
    load_choice();
    read_board();
    show_registers();
    rebuild();

    connect(m_view, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this]() {
        save_choice();
        rebuild();
    });
    connect(m_page, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this](int page) {
        save_choice();
        show_layout();
        if (m_panel != nullptr && m_panel_view == VIEW_MEMORY)
            m_panel->view()->go_to((unsigned int)page * AZBK::PAGE_BYTES);
    });

    //The machine runs meanwhile: what it writes and how it switches show up,
    //while there is a window to show it in
    QTimer * timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (isVisible() && !isMinimized()) update_view();
    });
    timer->start(500);
}

AZBKWindow::~AZBKWindow()
{
    if (m_save_timer->isActive()) write_choice();
}

QString AZBKWindow::page_number(unsigned int page) const
{
    return fmt_num(page, m_base, (m_base == 8) ? 5 : 4);
}

//What the AZ memory holds in a page (the map of MAXIOL's description)
QString AZBKWindow::describe_page(unsigned int page) const
{
    if (page < AZBK::LOGO_PAGE)
        return tr("BK RAM page %1, part %2").arg(BK_PAGE_OF_CODE[page >> 2]).arg(page & 3);
    if (page == AZBK::LOGO_PAGE)
        return m_logo.empty() ? tr("logo") : tr("logo: %1").arg(file_name(m_logo));
    if (page == 076) return tr("screenshot header");
    if (page == 077) return tr("BIOS variables");
    if (page < AZBK::ROM_PAGE) return tr("service");
    if (page < AZBK::ROM_PAGE + AZBK::ROM_PAGES) {
        const unsigned int slot = page - AZBK::ROM_PAGE;
        const QString r = QString("R%1").arg(slot, 2, 10, QChar('0'));
        for (const AZBK::Slot &s : m_slots) {
            if (slot < s.slot || slot >= s.slot + s.pages) continue;
            if (s.pages > 1)
                return tr("ROM %1: %2, part %3 of %4").arg(r, file_name(s.file)).arg(slot - s.slot + 1).arg(s.pages);
            return tr("ROM %1: %2").arg(r, file_name(s.file));
        }
        return tr("ROM %1, empty").arg(r);
    }
    if (page < FREE_PAGE)
        return tr("SMK-512 page %1, segment %2").arg((page - SMK_PAGE) >> 3, 0, m_base).arg((page - SMK_PAGE) & 7);
    return tr("free");
}

//A byte of the AZ memory, for the status line
QString AZBKWindow::az_text(unsigned int page, unsigned int offset) const
{
    const unsigned int a = page * AZBK::PAGE_BYTES + offset;
    return QString::fromStdString(m_ram->name) + " + " + fmt_num(a, m_base, addr_digits(m_ram->get_size() - 1, m_base))
           + " (" + tr("page %1: %2").arg(page_number(page), describe_page(page)) + ")";
}

//The windows whose register names the page, and whether they answer
QString AZBKWindow::seen_at(unsigned int page) const
{
    QStringList where;
    for (unsigned int n = 0; n < 16; n++) {
        if (m_state.win[n] != page) continue;
        QString how;
        if (m_state.rd[n] == ROUTE_ANSWER || m_state.rd[n] == ROUTE_OR) how = tr("answers");
        else if (m_state.wr[n] == ROUTE_SNOOP) how = tr("shadow");
        else how = tr("not answering");
        where << fmt_num(n << 12, m_base, addr_digits(0, m_base)) + " " + how;
    }
    return where.isEmpty() ? tr("nowhere") : where.join(", ");
}

//Who answers a read of the window: the board's route() for it
QString AZBKWindow::read_text(unsigned int n) const
{
    const QString below = QString::fromStdString(m_state.below[n]);
    switch (m_state.rd[n]) {
    case ROUTE_ANSWER:  return "AZ";
    case ROUTE_OR:      return tr("AZ OR %1").arg(below);
    case ROUTE_TIMEOUT: return tr("no answer");
    default:            return below.isEmpty() ? tr("no answer") : below;
    }
}

//Who takes a write of the window
QString AZBKWindow::write_text(unsigned int n) const
{
    const QString below = QString::fromStdString(m_state.below[n]);
    switch (m_state.wr[n]) {
    case ROUTE_ANSWER:  return "AZ";
    case ROUTE_THROUGH: return tr("AZ and %1").arg(below);
    case ROUTE_SNOOP:   return tr("%1, copy to AZ (shadow)").arg(below);
    case ROUTE_TIMEOUT: return tr("no answer");
    default: {
        const QString t = below.isEmpty() ? tr("no answer") : below;
        //An answering window that takes no write leaves it to the memory under it
        if (((m_state.active >> n) & 1) != 0 && m_state.ro[n]) return tr("%1 (read-only window)").arg(t);
        return t;
    }
    }
}

//What the processor sees: the board's pages and the memory of the machine
MemorySource AZBKWindow::source_now()
{
    MemorySource s;
    if (m_cpu == nullptr) {
        s.title = QString::fromStdString(d->name);
        s.size = SPACE;
        s.base = m_base;
        s.read = [](unsigned int) { return 0xFFu; };
        return s;
    }
    s = memory_of_cpu(m_cpu);
    s.title = QString::fromStdString(d->name);
    //The windows are taken when asked: the dump of this view stays while the
    //board switches, only the owner of a cell changes
    MemoryMapper * mm = m_cpu->mm;
    AZBK * board = d;
    const int base = m_base;
    s.owner = [this, mm, board, base](unsigned int a) -> QString {
        QString below = tr("nothing");
        if (mm != nullptr) {
            unsigned int offset = 0;
            AddressableDevice * owner = (a < IO_PAGE) ? mm->peek_owner_below(board, a, offset) : mm->peek_owner(a, offset);
            if (owner != nullptr)
                below = QString::fromStdString(owner->name) + " + " + fmt_num(offset, base, addr_digits(offset, base));
        }
        if (a >= IO_PAGE) return below;
        const unsigned int n = a >> 12;
        const QString az = az_text(m_state.win[n], a & 07777);
        switch (m_state.rd[n]) {
        case ROUTE_ANSWER:  return az;
        case ROUTE_OR:      return az + " | " + below;
        case ROUTE_TIMEOUT: return tr("no answer");
        default:
            return (m_state.wr[n] == ROUTE_SNOOP) ? below + " | " + tr("shadow: %1").arg(az) : below;
        }
    };
    return s;
}

//The AZ pages the window registers name, whether the windows answer or not:
//a shadow window shows the copy the board keeps, which should be the memory
//of the БК under it. Above 0177000 there is no window
MemorySource AZBKWindow::source_pages()
{
    MemorySource s;
    s.title = QString::fromStdString(d->name);
    s.size = SPACE;
    s.base = m_base;
    Memory * ram = m_ram;
    AZBK * board = d;
    const unsigned int size = ram->get_size();
    s.read = [this, ram, size](unsigned int a) -> unsigned int {
        if (a >= IO_PAGE) return 0xFF;
        const unsigned int at = m_state.win[a >> 12] * AZBK::PAGE_BYTES + (a & 07777);
        return (at < size) ? (ram->get_direct(at) & 0xFF) : 0xFF;
    };
    s.writable = [this, size](unsigned int a) {
        return a < IO_PAGE && m_state.win[a >> 12] * AZBK::PAGE_BYTES + (a & 07777) < size;
    };
    //The screen of the board redraws the lines of a page written to
    s.write = [this, ram, board, size](unsigned int a, unsigned int v) {
        if (a >= IO_PAGE) return;
        const unsigned int page = m_state.win[a >> 12];
        const unsigned int at = page * AZBK::PAGE_BYTES + (a & 07777);
        if (at >= size) return;
        ram->set_value(at, v & 0xFF, true);
        board->touch_page(page);
    };
    s.owner = [this](unsigned int a) -> QString {
        if (a >= IO_PAGE) return tr("no window");
        return az_text(m_state.win[a >> 12], a & 07777);
    };
    return s;
}

//All of the AZ memory
MemorySource AZBKWindow::source_memory()
{
    MemorySource s;
    s.title = QString::fromStdString(m_ram->name);
    s.size = m_ram->get_size();
    s.base = m_base;
    Memory * ram = m_ram;
    AZBK * board = d;
    s.read = [ram](unsigned int a) { return ram->get_direct(a) & 0xFF; };
    s.writable = [](unsigned int) { return true; };
    s.write = [ram, board](unsigned int a, unsigned int v) {
        ram->set_value(a, v & 0xFF, true);
        board->touch_page(a / AZBK::PAGE_BYTES);
    };
    s.owner = [this](unsigned int a) -> QString {
        const unsigned int page = a / AZBK::PAGE_BYTES;
        return tr("%1; the processor: %2").arg(az_text(page, a % AZBK::PAGE_BYTES), seen_at(page));
    };
    return s;
}

//The mapper as the board has it, read where the board lives. route() has no
//side effects (the contract of routed = 1)
void AZBKWindow::read_board()
{
    State s;
    AZBK * board = d;
    MemoryMapper * mm = (m_cpu != nullptr) ? m_cpu->mm : nullptr;
    e->invoke([&s, board, mm]() {
        for (unsigned int n = 0; n < 16; n++) {
            const unsigned int a = n << 12;
            s.win[n] = board->window_page(n);
            s.rd[n] = board->route(a, MODE_R);
            s.wr[n] = board->route(a, MODE_W);
            s.ro[n] = board->window_readonly(n);
            if (mm != nullptr) {
                unsigned int offset = 0;
                AddressableDevice * below = mm->peek_owner_below(board, a, offset);
                if (below != nullptr) s.below[n] = below->name;
            }
        }
        s.active = board->active();
        s.readonly = board->readonly();
        s.shadow = board->shadow();
        s.control = board->control();
        s.copy130 = board->copy130();
        s.copy716 = board->copy716();
        s.start = board->starting();
        s.smk_flag = board->smk_flag();
    });
    m_state = s;
}

void AZBKWindow::show_registers()
{
    const State &s = m_state;
    const int w = (m_base == 8) ? 6 : 4;
    auto reg = [this, w](unsigned int address, unsigned int value) {
        return fmt_num(address, m_base, w) + ": " + fmt_num(value, m_base, w);
    };

    QStringList bits;
    const unsigned int c = s.control;
    const unsigned int cpu = (c & AZBK::CTRL_CPU) >> 6;
    if (cpu != 0) bits << tr("CPU %1 MHz").arg(cpu);
    bits << (((c & AZBK::CTRL_50HZ) != 0) ? tr("48.8 Hz") : tr("60 Hz"));
    if (c & AZBK::CTRL_V100)    bits << tr("timer of vector %1").arg(fmt_num(0100, m_base, 3));
    if (c & AZBK::CTRL_ROM11)   bits << tr("BK11 ROM in window 1");
    if (c & AZBK::CTRL_037OFF)  bits << "037_OFF";
    if (c & AZBK::CTRL_014OFF)  bits << "014_OFF";
    if (c & AZBK::CTRL_BK11EMU) bits << tr("BK11 emulation");
    if (c & AZBK::CTRL_BK10)    bits << tr("BK10");
    if (c & AZBK::CTRL_WIN1MOD) bits << tr("window 1 modification");
    if (c & AZBK::CTRL_REV037)  bits << tr("037 control");
    if (c & AZBK::CTRL_WIN1OFF) bits << tr("window 1 to SMK");

    QString t = reg(0177340, s.active) + "  " + reg(0177342, s.readonly) + "  " + reg(0177344, s.shadow) + "\n";
    t += reg(0177346, s.control) + " - " + bits.join(", ") + "\n";
    t += reg(0177350, s.copy130) + "  " + reg(0177352, s.copy716);
    if (s.start) t += "  " + tr("start");
    if (s.smk_flag) t += "  " + tr("0177130 armed");
    m_registers->setText(t);
}

void AZBKWindow::show_selection()
{
    m_page->setEnabled(m_view->currentIndex() == VIEW_MEMORY);
}

//The view and page are kept between runs in [AZBK] of the ini
static int setup_int(Emulator * e, const char * key, int def, int max)
{
    bool ok = false;
    const int v = QString::fromStdString(e->read_setup("AZBK", key, "")).toInt(&ok);
    return (ok && v >= 0 && v <= max) ? v : def;
}

void AZBKWindow::load_choice()
{
    m_view->blockSignals(true);
    m_page->blockSignals(true);
    m_view->setCurrentIndex(setup_int(e, "view", VIEW_NOW, VIEW_MEMORY));
    m_page->setValue(setup_int(e, "page", 0, m_page->maximum()));
    m_view->blockSignals(false);
    m_page->blockSignals(false);
}

//Every ini write saves the whole file: a page stepped with the wheel would
//write it a few times a step
void AZBKWindow::save_choice()
{
    m_save_timer->start();
}

void AZBKWindow::write_choice()
{
    e->write_setup("AZBK", "view", std::to_string(m_view->currentIndex()));
    e->write_setup("AZBK", "page", std::to_string(m_page->value()));
}

//The table for the view. Rows are rewritten in place, so a change of the
//mapper while the machine runs keeps the selection and the scroll
void AZBKWindow::show_layout()
{
    show_selection();
    const int view = m_view->currentIndex();
    std::vector<QStringList> rows;
    std::vector<unsigned int> offsets;
    QStringList header;

    if (view == VIEW_MEMORY) {
        header << tr("Page") << tr("Contents") << tr("Seen at");
        const unsigned int first = (unsigned int)m_page->value();
        const unsigned int pages = m_ram->get_size() / AZBK::PAGE_BYTES;
        for (unsigned int p = first; p < first + TABLE_PAGES && p < pages; p++) {
            rows.push_back(QStringList() << page_number(p) << describe_page(p) << seen_at(p));
            offsets.push_back(p * AZBK::PAGE_BYTES);
        }
    } else {
        header << tr("Address") << tr("AZ page") << tr("Read") << tr("Write");
        const int digits = addr_digits(0, m_base);
        for (unsigned int n = 0; n < 16; n++) {
            const unsigned int page = m_state.win[n];
            rows.push_back(QStringList() << fmt_num(n << 12, m_base, digits)
                                         << page_number(page) + " " + describe_page(page)
                                         << read_text(n) << write_text(n));
            offsets.push_back(n << 12);
        }
        const QString regs = tr("registers of the machine");
        rows.push_back(QStringList() << fmt_num(IO_PAGE, m_base, digits) << "" << regs << regs);
        offsets.push_back(IO_PAGE);
    }

    if (m_layout->columnCount() != header.size() || m_layout->headerItem()->text(0) != header[0]) {
        m_layout->clear();
        m_layout->setColumnCount(header.size());
        m_layout->setHeaderLabels(header);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        QTreeWidgetItem * item = m_layout->topLevelItem((int)i);
        if (item == nullptr) item = new QTreeWidgetItem(m_layout);
        for (int c = 0; c < rows[i].size(); c++) item->setText(c, rows[i][c]);
        item->setData(0, Qt::UserRole, offsets[i]);
    }
    while (m_layout->topLevelItemCount() > (int)rows.size())
        delete m_layout->takeTopLevelItem((int)rows.size());
}

//The table and the dump for the view; the cursor stays where it was between
//the two views of the processor's addresses. Only a choice in the window comes
//here: the panel goes away under the handlers of its own view otherwise (an
//edit in the dump updates every window)
void AZBKWindow::rebuild()
{
    show_layout();
    const int view = m_view->currentIndex();

    unsigned int cursor = 0;
    if (m_panel != nullptr) {
        if (m_panel_view != VIEW_MEMORY && view != VIEW_MEMORY) cursor = m_panel->view()->cursor();
        delete m_panel;
    }
    MemorySource src;
    if (view == VIEW_NOW) src = source_now();
    else if (view == VIEW_PAGES) src = source_pages();
    else src = source_memory();
    m_panel = new MemoryPanel(e, src, this);
    m_panel_view = view;
    m_dump_box->addWidget(m_panel);
    connect(m_panel->view(), &MemoryView::memory_changed, this, [this]() { emit data_changed(this); });
    m_panel->view()->go_to((view == VIEW_MEMORY) ? (unsigned int)m_page->value() * AZBK::PAGE_BYTES : cursor);
}

void AZBKWindow::update_view()
{
    const State was = m_state;
    read_board();
    show_registers();
    if (!(m_state == was)) show_layout();
    if (m_panel != nullptr) m_panel->refresh();
}

GenericDbgWnd * CreateAZBKWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    AZBK * board = dynamic_cast<AZBK*>(d);
    if (board != nullptr && board->board_ram() != nullptr) return new AZBKWindow(parent, e, board);
    return CreateMemoryWindow(parent, e, d);
}
