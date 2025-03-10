#include "portwindow.h"
#include "ui_portwindow.h"

PortWindow::PortWindow(QWidget *parent) :
    GenericDbgWnd(parent),
    ui(new Ui::PortWindow)
{
    ui->setupUi(this);
}

PortWindow::PortWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    PortWindow(parent)
{
    this->e = e;
    this->d = dynamic_cast<Port*>(d);
    setWindowTitle(d->name + " : " + d->type);

    timer = new QTimer();
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(100);
}

PortWindow::~PortWindow()
{
    delete ui;
}

void PortWindow::update()
{
    unsigned int v = d->get_direct();
    ui->binaryEdit->setText(QString("%1").arg(v, 8, 2, QChar('0')));
    ui->hexEdit->setText(QString("%1").arg(v, 2, 16, QChar('0')).toUpper());
}

GenericDbgWnd * CreatePortWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new PortWindow(parent, e, d);
}
