#include <QKeyEvent>

#include "debugwindow.h"
#include "emulator/utils.h"
#include "qexception.h"
#include "ui_debugwindow.h"

DebugWindow::DebugWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::DebugWindow),
    stop_tracking(false)
{
    ui->setupUi(this);
}

DebugWindow::DebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    DebugWindow(parent)
{
    this->e = e;
    this->cpu = dynamic_cast<CPU*>(d);
    temporary_break = -1;

    this->setWindowTitle(d->name + " : " + d->type);

    QString file_name;
    if (d->type == "i8080")
        file_name = e->data_path + "i8080.dis";
    else
    if (d->type == "z80")
        file_name = e->data_path + "z80.dis";
    else
    if (d->type == "6502")
        file_name = e->data_path + "6502.dis";
    else
    if (d->type == "65c02")
        file_name = e->data_path + "65c02.dis";

    disasm = new DisAsm(this, file_name);
    ui->codeview->set_data(e, dynamic_cast<CPU*>(d), disasm, dynamic_cast<CPU*>(d)->get_pc());
    ui->codeview->set_frame(true, true, true, true, "╔═╤║ │╟─┴");
    ui->codeview->set_scroll(100, 0);

    connect(ui->codeview, SIGNAL(command_key(QKeyEvent*)), this, SLOT(command_key(QKeyEvent*)));


    ui->registers->set_frame(true, true, true, false, "╤═╤│ │╧─┴");
    ui->flags->set_frame(true, true, true, false, "╤═╗│ ║╧─╢");

    memory_devices = 0;
    device_mm = nullptr;
    for (unsigned int i=0; i < e->dm->device_count; i++){
        DeviceDescription * d = e->dm->get_device(i);
        if (d->device_type == "memory-mapper")
        {
            device_mm = dynamic_cast<AddressableDevice*>(d->device);
        }
        else if (d->device_type == "ram" || d->device_type == "rom")
        {
            device_memory[memory_devices++] = dynamic_cast<AddressableDevice*>(d->device);
        };
    }

    ui->dump->set_frame(false, true, true, true, "╔═╗║ ║╚═╝");

    if (device_mm != nullptr)
        ui->dump->set_data(e, device_mm);

    ui->dump->set_scroll(100, 0);

    update_registers();

    state_timer = new QTimer(this);
    connect(state_timer, SIGNAL(timeout()), this, SLOT(update_state()));
    state_timer->start(200);
    update_state();
}

DebugWindow::~DebugWindow()
{
    delete state_timer;
    delete ui;
}

void DebugWindow::update_registers()
{
    QList<QPair<QString, QString>> r = cpu->get_registers();
    QList<QPair<QString, QString>> f = cpu->get_flags();
    ui->registers->set_data(r);
    ui->flags->set_data(f);
    emit data_changed(this);
}

GenericDbgWnd * CreateDebugWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new DebugWindow(parent, e, d);
}

void DebugWindow::on_closeButton_clicked()
{
    close();
}


void DebugWindow::on_stepButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        cpu->debug = DEBUG_STEP;
        QTimer::singleShot(100, this, SLOT(on_toPCButton_clicked()));
    }
}


void DebugWindow::on_toPCButton_clicked()
{
    ui->codeview->go_to(cpu->get_pc());
    update_registers();
}


void DebugWindow::on_addBRButton_clicked()
{
    cpu->add_breakpoint(ui->codeview->get_address_at_cursor());
    ui->codeview->update();
}


void DebugWindow::on_removeBRButton_clicked()
{
    cpu->remove_breakpoint(ui->codeview->get_address_at_cursor());
    ui->codeview->update();

}


void DebugWindow::on_runButton_clicked()
{
    cpu->debug = DEBUG_OFF;
    stop_tracking = true;
}

void DebugWindow::track()
{
    ui->codeview->update();
    update_registers();
    if ((cpu->debug != DEBUG_STOPPED) && ~stop_tracking)
    {
        QTimer::singleShot(200, this, SLOT(track()));
    } else {
        if (temporary_break >= 0) cpu->remove_breakpoint(temporary_break);
        on_toPCButton_clicked();
    }

    stop_tracking = false;
}

void DebugWindow::on_stopTrackingButton_clicked()
{
    cpu->debug = DEBUG_STOPPED;
    stop_tracking = true;
    on_toPCButton_clicked();
}


void DebugWindow::on_toolButton_clicked()
{
    try {
        unsigned int v = parse_numeric_value("$" + ui->valueEdit->text());
        cpu->set_context_value("PC", v);
        on_toPCButton_clicked();
    } catch (QException &e) {
        QMessageBox::critical(0, DebugWindow::tr("Error"), DebugWindow::tr("Incorrect address value"));
    }

}


void DebugWindow::on_stepOverButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        if (temporary_break >= 0) cpu->remove_breakpoint(temporary_break);

        unsigned int a = cpu->get_pc();
        unsigned int command = cpu->get_command();

        bool can_over = false;
        for (unsigned int c : cpu->over_commands)
            if (c==command) can_over = true;

        if (can_over)
        {
            uint8_t bytes[15];
            for (unsigned int i = 0; i < disasm->max_command_length; i++)
                bytes[i] = cpu->read_mem(a+i);

            QString tmp;
            unsigned int len = disasm->disassemle(&bytes, a, sizeof(bytes), &tmp);

            temporary_break = a+len;
            cpu->add_breakpoint(temporary_break);

            cpu->debug = DEBUG_BRAKES;
            QTimer::singleShot(200, this, SLOT(track()));
            stop_tracking = false;
        } else {
            cpu->debug = DEBUG_STEP;
            QTimer::singleShot(100, this, SLOT(on_toPCButton_clicked()));
        }
    }

}


void DebugWindow::on_runUntilButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        if (temporary_break >= 0) cpu->remove_breakpoint(temporary_break);

        temporary_break = ui->codeview->get_address_at_cursor();
        cpu->add_breakpoint(temporary_break);

        cpu->debug = DEBUG_BRAKES;
        QTimer::singleShot(200, this, SLOT(track()));
        stop_tracking = false;
    }
}


void DebugWindow::on_gotoButton_clicked()
{
    try {
        unsigned int v = parse_numeric_value("$" + ui->valueEdit->text());
        ui->codeview->go_to(v);
        update_registers();
    } catch (QException &e) {
        QMessageBox::critical(0, DebugWindow::tr("Error"), DebugWindow::tr("Incorrect address value"));
    }
}


void DebugWindow::on_runDebuggedButton_clicked()
{
    if (cpu->debug == DEBUG_STOPPED)
    {
        cpu->debug = DEBUG_BRAKES;
        QTimer::singleShot(200, this, SLOT(track()));
    }
}

void DebugWindow::update_state()
{
    switch (cpu->debug) {
    case DEBUG_STOPPED:
        ui->state->setPixmap(QPixmap(":/icons/pause"));
        break;
    case DEBUG_OFF:
        ui->state->setPixmap(QPixmap(":/icons/play"));
        break;
    case DEBUG_BRAKES:
        ui->state->setPixmap(QPixmap(":/icons/ondebug"));
        break;
    }
}

void DebugWindow::resizeEvent(QResizeEvent*)
{
    //qDebug() << "Resized!";
}

void DebugWindow::keyPressEvent(QKeyEvent *event)
{
    //qDebug() << event->key();

    switch (event->key()) {
    case Qt::Key_F4:
        on_runUntilButton_clicked();
        break;
    case Qt::Key_F7:
        on_stepButton_clicked();
        break;
    case Qt::Key_F8:
        on_stepOverButton_clicked();
        break;
    case Qt::Key_F9:
        on_runDebuggedButton_clicked();
        break;
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Home:
        ui->codeview->keyPressEvent(event);
        break;
    default:
        QDialog::keyPressEvent(event);
        break;
    }
}

void DebugWindow::command_key(QKeyEvent *event)
{
    keyPressEvent(event);
}

void DebugWindow::on_dumpGotoButton_clicked()
{
    try {
        unsigned int v = parse_numeric_value("$" + ui->dumpAddr->text());
        ui->dump->reset_buffer();
        ui->dump->go_to(v);
    } catch (QException &e) {
        QMessageBox::critical(0, DebugWindow::tr("Error"), DebugWindow::tr("Incorrect address value"));
    }
}


void DebugWindow::on_dumpPgDownBotton_clicked()
{
    ui->dump->page_down();
}


void DebugWindow::on_dumpPgUpBotton_clicked()
{
    ui->dump->page_up();
}

void DebugWindow::update_view()
{
    ui->dump->update_view();
}
