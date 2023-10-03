#include "i8255window.h"
#include "ui_i8255window.h"

#include "emulator/devices/common/i8255.h"

I8255Window::I8255Window(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::I8255Window)
{
    ui->setupUi(this);
}

I8255Window::I8255Window(QWidget *parent, Emulator * e, ComputerDevice * d):
    I8255Window(parent)
{
    this->e = e;
    this->d = dynamic_cast<I8255*>(d);
    setWindowTitle(d->name + " : " + d->type);

    timer = new QTimer();
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(100);
}


I8255Window::~I8255Window()
{
    delete ui;
}

QDialog * CreateI8255Window(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new I8255Window(parent, e, d);
}

void I8255Window::update()
{
    unsigned int va = d->get_value(0);
    unsigned int vb = d->get_value(1);
    unsigned int vc = d->get_value(2);
    unsigned int vd = d->get_value(3);
    ui->binA->setText(QString("%1").arg(va, 8, 2, QChar('0')));
    ui->hexA->setText(QString("%1").arg(va, 2, 16, QChar('0')).toUpper());
    ui->binB->setText(QString("%1").arg(vb, 8, 2, QChar('0')));
    ui->hexB->setText(QString("%1").arg(vb, 2, 16, QChar('0')).toUpper());
    ui->binCH->setText(QString("%1").arg(vc >> 4, 4, 2, QChar('0')));
    ui->binCL->setText(QString("%1").arg(vc & 0x0F, 4, 2, QChar('0')));
    ui->hexC->setText(QString("%1").arg(vc, 2, 16, QChar('0')).toUpper());
    ui->binD->setText(QString("%1").arg(vd, 8, 2, QChar('0')));
    ui->hexD->setText(QString("%1").arg(vd, 2, 16, QChar('0')).toUpper());

    ui->modeA->setText(((vd &  0x10) != 0)?I8255Window::tr("Input"):I8255Window::tr("Output"));
    ui->modeB->setText(((vd &  0x02) != 0)?I8255Window::tr("Input"):I8255Window::tr("Output"));
    ui->modeCH->setText(((vd & 0x08) != 0)?I8255Window::tr("Input"):I8255Window::tr("Output"));
    ui->modeCL->setText(((vd & 0x01) != 0)?I8255Window::tr("Input"):I8255Window::tr("Output"));

}
