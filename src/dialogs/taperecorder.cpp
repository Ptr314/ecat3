#include <QFileDialog>

#include "taperecorder.h"
#include "ui_taperecorder.h"

TapeRecorderWindow::TapeRecorderWindow(QWidget *parent)
    : GenericDbgWnd(parent)
    , ui(new Ui::TapeRecorderWindow)
{
    ui->setupUi(this);
}

TapeRecorderWindow::TapeRecorderWindow(QWidget *parent, Emulator * e, ComputerDevice * d):
    TapeRecorderWindow(parent)
{
    this->e = e;
    this->d = dynamic_cast<TapeRecorder*>(d);
    setWindowTitle(d->name + " : " + d->type);

    QIcon * icon = new QIcon();
    icon->addFile(QString::fromUtf8(":/icons/sound2"), QSize(), QIcon::Normal, QIcon::Off);
    icon->addFile(QString::fromUtf8(":/icons/nosound2"), QSize(), QIcon::Normal, QIcon::On);

    ui->muteButton->setCheckable(true);
    ui->muteButton->setIcon(*icon);

    connect(ui->muteButton, SIGNAL(toggled(bool)), this, SLOT(set_mute(bool)));

    ui->playButton->setEnabled(false);

    // timer = new QTimer();
    // connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    // timer->start(100);
}

void TapeRecorderWindow::set_mute(bool muted)
{
    //TODO: implement
}

TapeRecorderWindow::~TapeRecorderWindow()
{
    delete ui;
}

void TapeRecorderWindow::on_openButton_clicked()
{
    QString path = e->read_setup("Startup", "last_path", e->work_path);
    SystemData * sd = e->get_system_data();
    QString file_name = QFileDialog::getOpenFileName(this, Emulator::tr("Load a file"), path, sd->allowed_files);


    if (!file_name.isEmpty()) {
        QFileInfo fi(file_name);
        e->write_setup("Startup", "last_path", fi.absolutePath());

        //HandleExternalFile(e, file_name);
        ui->playButton->setEnabled(true);
        ui->FileNameEdit->setText(fi.fileName());
    }
}

