// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder window, source

#include <QFileDialog>
#include <QProxyStyle>
#include <QMovie>
#include <qevent.h>

#include "taperecorder.h"
#include "ui_taperecorder.h"

class ToolButtonProxy : public QProxyStyle {
public:
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override {
        int ret = 0;
        switch (metric) {
        case QStyle::PM_ButtonShiftHorizontal:
        case QStyle::PM_ButtonShiftVertical:
            ret = 0;
            break;
        default:
            ret = QProxyStyle::pixelMetric(metric, option, widget);
            break;
        }
        return ret;
    }
};

TapeRecorderWindow::TapeRecorderWindow(QWidget *parent)
    : GenericDbgWnd(parent)
    , ui(new Ui::TapeRecorderWindow)
    , is_playing(false)
    , is_paused(false)
    , update_timer(this)
{
    setAttribute(Qt::WA_TranslucentBackground);

    #if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
        setWindowFlag(Qt::FramelessWindowHint, true);
    #else
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    #endif

    ui->setupUi(this);
}

TapeRecorderWindow::TapeRecorderWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
    : TapeRecorderWindow(parent)
{
    this->e = e;
    this->d = dynamic_cast<TapeRecorder*>(d);
    setWindowTitle(d->name + " : " + d->type);

    QIcon icon;
    icon.addFile(QString::fromUtf8(":/icons/sound2"), QSize(), QIcon::Normal, QIcon::Off);
    icon.addFile(QString::fromUtf8(":/icons/nosound2"), QSize(), QIcon::Normal, QIcon::On);

    ToolButtonProxy * tbp = new ToolButtonProxy();
    ui->buttonRec->setStyle( tbp );
    ui->buttonRewind->setStyle( tbp );
    ui->buttonForward->setStyle( tbp );
    ui->buttonPlay->setStyle( tbp );
    ui->buttonPause->setStyle( tbp );
    ui->buttonEject->setStyle( tbp );
    ui->buttonMute->setStyle( tbp );

    ui->buttonPlay->setChecked(false);
    ui->buttonPause->setChecked(false);

    btnIconOff.addFile(QString::fromUtf8(":/icons/tape_flat_off"));
    btnIconOn.addFile(QString::fromUtf8(":/icons/tape_flat_on"));
    btnIconEjectOff.addFile(QString::fromUtf8(":/icons/tape_eject_off"));
    btnIconEjectOn.addFile(QString::fromUtf8(":/icons/tape_eject_on"));

    ui->name_mask->setVisible(false);
    ui->textLabel->setVisible(false);

    QMovie *movie_left = new QMovie(":/icons/roller_left", QByteArray(), this);
    QMovie *movie_right = new QMovie(":/icons/roller_right", QByteArray(), this);

    ui->left_roller->setMovie(movie_left);
    ui->right_roller->setMovie(movie_right);
    ui->left_roller->hide();
    ui->right_roller->hide();

    this->d->volume(10);

    update_timer.setInterval(1000);
    connect(&update_timer, &QTimer::timeout, this, &TapeRecorderWindow::update_counter);
}

void TapeRecorderWindow::set_mute(bool muted)
{
    //TODO: implement
}

TapeRecorderWindow::~TapeRecorderWindow()
{
    delete ui;
}

void TapeRecorderWindow::on_buttonRewind_pressed()
{
    ui->buttonRewind->setIcon(btnIconOn);
}

void TapeRecorderWindow::on_buttonRewind_released()
{
    ui->buttonRewind->setIcon(btnIconOff);
}


void TapeRecorderWindow::on_buttonForward_pressed()
{
    ui->buttonForward->setIcon(btnIconOn);
}


void TapeRecorderWindow::on_buttonForward_released()
{
    ui->buttonForward->setIcon(btnIconOff);
}


void TapeRecorderWindow::on_buttonEject_pressed()
{
    ui->buttonEject->setIcon(btnIconEjectOn);

    if (is_playing) {
        is_playing = false;
        ui->buttonPlay->setChecked(false);
        play_pause();
    } else {
        QString path = e->read_setup("Startup", "last_path", e->work_path);
        SystemData * sd = e->get_system_data();
        QString file_name = QFileDialog::getOpenFileName(this, Emulator::tr("Load a file"), path, d->files);


        if (!file_name.isEmpty()) {
            QFileInfo fi(file_name);
            QString ext = fi.suffix().toLower();
            QString fmt = e->read_setup("TapeFiles", ext, "");

            if (fmt.length() > 0) {
                e->write_setup("Startup", "last_path", fi.absolutePath());

                ui->name_mask->setVisible(true);
                ui->textLabel->setVisible(true);

                loaded_file = fi.fileName();

                d->load_file(file_name, fmt);

                update_counter();

            } else {
                QMessageBox::warning(0, TapeRecorderWindow::tr("Error"), TapeRecorderWindow::tr("Unknown tape file format!"));
            }
        }
        ui->buttonEject->setIcon(btnIconEjectOff);
    }
}

void TapeRecorderWindow::on_buttonEject_released()
{
    ui->buttonEject->setIcon(btnIconEjectOff);
}

void TapeRecorderWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        dragPosition = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}

void TapeRecorderWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        move(event->globalPosition().toPoint() - dragPosition);
#else
        move(event->globalPos() - dragPosition);
#endif
        event->accept();
    }
}

void TapeRecorderWindow::on_buttonPlay_clicked()
{
    if (ui->buttonPlay->isChecked()) {
        is_playing = true;
        play_pause();
    }
}

void TapeRecorderWindow::on_buttonStop_clicked()
{
}

void TapeRecorderWindow::play_pause()
{
    if (is_playing && !is_paused) {
        ui->left_roller->movie()->start();
        ui->right_roller->movie()->start();
        ui->left_roller->movie()->setSpeed(50);
        ui->right_roller->movie()->setSpeed(100);
        d->play();
        ui->left_roller->show();
        ui->right_roller->show();
        update_timer.start();
    } else {
        ui->left_roller->movie()->stop();
        ui->right_roller->movie()->stop();
        ui->left_roller->hide();
        ui->right_roller->hide();
        d->stop();
        update_timer.stop();
        update_counter();
    }
}

void TapeRecorderWindow::on_toolButton_clicked()
{
    close();
}

void TapeRecorderWindow::on_buttonPause_clicked()
{
    is_paused = ui->buttonPause->isChecked();
    if (is_playing) play_pause();
}

void TapeRecorderWindow::on_buttonMute_clicked()
{
    d->mute(ui->buttonMute->isChecked());
}

void TapeRecorderWindow::on_buttonRewind_clicked()
{
    if (is_playing) {
        is_playing = false;
        play_pause();
        ui->buttonPlay->setChecked(false);
    }
    d->rewind();
}

void TapeRecorderWindow::on_buttonRec_clicked()
{
    if (ui->buttonRec->isChecked()) {
        is_recording = true;
        // play_pause();
    } else {
        is_recording = false;
    }
    d->set_recording(is_recording);
    if (!is_recording && d->get_record_size() != 0) {
        const QString path = e->read_setup("Startup", "last_path", e->work_path);
        const QString file_name = QFileDialog::getSaveFileName(this, Emulator::tr("Save recorded data"), path, "Binary files (*.bin);;All files (*.*)");
        if (!file_name.isEmpty()) {
            const QFileInfo fi(file_name);
            e->write_setup("Startup", "last_path", fi.absolutePath());
            QFile file(file_name);
            if (file.open(QIODevice::WriteOnly)) {
                const std::vector<uint8_t> * data = d->get_record_data();
                file.write(reinterpret_cast<const char*>(data->data()), static_cast<qint64>(data->size()));
                file.close();
            } else {
                QMessageBox::warning(this, TapeRecorderWindow::tr("Error"), TapeRecorderWindow::tr("Unable to save file!"));
            }
        }
    }
}

void TapeRecorderWindow::closeEvent(QCloseEvent *event)
{
    update_timer.stop();
    d->stop();
    GenericDbgWnd::closeEvent(event);
}

void TapeRecorderWindow::update_counter()
{
    if (is_playing){
        if (d->get_mode() != TAPE_STOPPED) {
            int position = d->get_position();
            int total = d->get_total();
            ui->textLabel->setText(
                loaded_file
                + " ("
                + QString::number(position / 60) + ":" + QString("%1").arg(position % 60, 2, 10, QChar('0'))
                + "/"
                + QString::number(total / 60) + ":" + QString("%1").arg(total % 60, 2, 10, QChar('0'))
                + ")"
            );
        } else {
            if (!is_paused) {
                is_playing = false;
                play_pause();
                ui->buttonPlay->setChecked(false);
                d->rewind();
            }
        }
    } else {
        int total = d->get_total();
        ui->textLabel->setText(
            loaded_file
            + " ("
            + QString::number(total / 60) + ":" + QString("%1").arg(total % 60, 2, 10, QChar('0'))
            + ")"
            );
    }
}

GenericDbgWnd * CreateTapeWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new TapeRecorderWindow(parent, e, d);
}



