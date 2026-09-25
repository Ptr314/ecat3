// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder window, source

#include <QFileDialog>
#include <QDir>
#include <QProxyStyle>
#include <QMovie>
#include <QTimer>
#include <qevent.h>
#include <QMessageBox>
#include <QFontMetrics>

#include "taperecorder.h"
#include "ui_taperecorder.h"
#include "emulator/script/script_parser.h"

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
    setWindowTitle(QString::fromStdString(d->name + " : " + d->type));

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

    //Обратный ход перебирает кадры назад, а назад QMovie умеет ходить только
    //по кешу: с CacheNone формат GIF прыгает лишь на нулевой кадр, и анимация
    //на первом же шаге останавливается насовсем
    movie_left->setCacheMode(QMovie::CacheAll);
    movie_right->setCacheMode(QMovie::CacheAll);

    ui->left_roller->setMovie(movie_left);
    ui->right_roller->setMovie(movie_right);
    ui->left_roller->hide();
    ui->right_roller->hide();

    //Шаг обратного хода берем у самой анимации, чтобы он не разъезжался с
    //прямым, если ролики когда-нибудь перерисуют
    movie_left->jumpToFrame(0);
    roller_delay = qMax(1, movie_left->nextFrameDelay());

    connect(&back_left, &QTimer::timeout, this, [this]() { step_back(ui->left_roller); });
    connect(&back_right, &QTimer::timeout, this, [this]() { step_back(ui->right_roller); });

    this->d->volume(10);

    update_timer.setInterval(1000);
    connect(&update_timer, &QTimer::timeout, this, &TapeRecorderWindow::update_counter);
    this->d->on_mode_changed = [this](unsigned int new_mode) {
        QTimer::singleShot(0, this, [this, new_mode]() {
            tape_mode_changed(new_mode);
        });
    };

    //Машина или сценарий могли зарядить и запустить ленту задолго до того, как
    //окно открыли: показываем то, что в лентопротяжке происходит сейчас
    sync_from_device();
}

void TapeRecorderWindow::show_movement(bool moving, bool fast, bool back)
{
    if (moving) {
        roll(ui->left_roller, back_left, fast?200:50, back);
        roll(ui->right_roller, back_right, fast?400:100, back);
        update_timer.start();
    } else {
        stop_roller(ui->left_roller, back_left);
        stop_roller(ui->right_roller, back_right);
        update_timer.stop();
    }
}

void TapeRecorderWindow::roll(QLabel * roller, QTimer & timer, int speed, bool back)
{
    QMovie * movie = roller->movie();
    if (back) {
        movie->stop();
        timer.start(qMax(1, roller_delay * 100 / speed));
    } else {
        timer.stop();
        movie->setSpeed(speed);
        movie->start();
    }
    roller->show();
}

void TapeRecorderWindow::stop_roller(QLabel * roller, QTimer & timer)
{
    timer.stop();
    roller->movie()->stop();
    roller->hide();
}

void TapeRecorderWindow::step_back(QLabel * roller)
{
    QMovie * movie = roller->movie();
    const int frames = movie->frameCount();
    if (frames <= 0) return;
    //Кадр еще не показывали - начинаем с последнего
    const int current = movie->currentFrameNumber();
    movie->jumpToFrame(((current <= 0)?frames:current) - 1);
}

void TapeRecorderWindow::sync_from_device()
{
    const int mode = d->get_mode();
    //Перемотку машина ведет сама: в окне нажимаются ее клавиши, а кнопка
    //воспроизведения остается отпущенной
    const bool forward = (mode == TAPE_FORWARD);
    const bool back = (mode == TAPE_BACK);
    const bool playing = (mode != TAPE_STOPPED) && !forward && !back;
    const bool recording = d->get_recording();

    if (!d->get_loaded_name().empty())
        loaded_file = QString::fromStdString(d->get_loaded_name());

    if (!loaded_file.isEmpty()) {
        ui->name_mask->setVisible(true);
        ui->textLabel->setVisible(true);
    }

    is_recording = recording;
    ui->buttonRec->setChecked(recording);

    ui->buttonForward->setIcon(forward?btnIconOn:btnIconOff);
    ui->buttonRewind->setIcon(back?btnIconOn:btnIconOff);

    if (playing != (is_playing && !is_paused)) {
        is_playing = playing;
        is_paused = false;
        ui->buttonPlay->setChecked(playing);
        if (!playing) ui->buttonPause->setChecked(false);
    }

    //Только картинка: саму лентопротяжку трогать нельзя, она уже в том
    //состоянии, о котором нам сообщили
    const bool moving = (mode != TAPE_STOPPED);
    const bool fast = forward || back;
    if (moving != is_moving || fast != is_fast || back != is_back) {
        is_moving = moving;
        is_fast = fast;
        is_back = back;
        show_movement(moving, fast, back);
    }
    update_counter();
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
        e->record_command(d->name, "stop", "");
    } else {
        //Кассета, на которую писала машина, живет только в устройстве: перед
        //тем как сменить ее, спрашиваем, не сохранить ли
        if (d->get_record_size() != 0
            && QMessageBox::question(this, TapeRecorderWindow::tr("Tape Recorder"),
                   TapeRecorderWindow::tr("The machine has written on this tape. Save it?"),
                   QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            save_recording();

        QString path = QString::fromStdString(e->get_last_path());
        SystemData * sd = e->get_system_data();
        QString file_name = QFileDialog::getOpenFileName(this, tr("Load a file"), path, QString::fromStdString(d->files));


        if (!file_name.isEmpty()) {
            QFileInfo fi(file_name);
            QString ext = fi.suffix().toLower();
            // A machine specific entry wins over the generic one, so the same
            // extension can mean different things on different computers
            QString fmt = QString::fromStdString(e->read_setup("TapeFiles", sd->system_type + "." + ext.toStdString(), ""));
            if (fmt.isEmpty())
                fmt = QString::fromStdString(e->read_setup("TapeFiles", ext.toStdString(), ""));

            if (fmt.length() > 0) {
                e->set_last_path(fi.absolutePath().toStdString());

                ui->name_mask->setVisible(true);
                ui->textLabel->setVisible(true);

                loaded_file = fi.fileName();

                emulator::Result res = d->load_file(file_name.toStdString(), fmt.toStdString());
                if (!res) {
                    QMessageBox::warning(this, TapeRecorderWindow::tr("Error"), translateResultMessage(res.message));
                    return;
                }

                //The format is written out explicitly, so the replay does not
                //depend on the ini file
                e->record_command(d->name, "load",
                    format_script_arg(fi.absoluteFilePath().toStdString()) + "," + format_script_arg(fmt.toStdString()));

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
        if (!is_paused) e->record_command(d->name, "play", "");
    }
}

void TapeRecorderWindow::play_pause()
{
    is_moving = is_playing && !is_paused;
    is_fast = false;
    is_back = false;
    show_movement(is_moving, false, false);
    if (is_moving) {
        d->play();
    } else {
        d->stop();
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
    if (is_playing) {
        play_pause();
        e->record_command(d->name, is_paused?"stop":"play", "");
    }
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
        e->record_command(d->name, "stop", "");
    }
    d->rewind();
    e->record_command(d->name, "rewind", "");
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
    e->record_command(d->name, "record", is_recording?"1":"0");
    if (!is_recording) save_recording();
}

void TapeRecorderWindow::save_recording()
{
    if (d->get_record_size() != 0) {
        QString path = QString::fromStdString(e->get_last_path());
        // The device knows what it has decoded, and for some formats the
        // extension decides how the file is put back on the tape
        const std::string suggested = d->get_record_name();
        if (!suggested.empty()) path = QDir(path).filePath(QString::fromStdString(suggested));
        // The mask the machine loads tapes through is the one to save under:
        // the extension is what decides how the file goes back on the tape
        QString filter = QString::fromStdString(d->files);
        if (filter.isEmpty()) filter = "Binary files (*.bin);;Text programs (*.asc)";
        if (!filter.contains("*.*")) filter += ";;All files (*.*)";
        const QString file_name = QFileDialog::getSaveFileName(this, tr("Save recorded data"), path, filter);
        if (!file_name.isEmpty()) {
            const QFileInfo fi(file_name);
            e->set_last_path(fi.absolutePath().toStdString());
            QFile file(file_name);
            if (file.open(QIODevice::WriteOnly)) {
                std::vector<uint8_t> data;
                d->get_save_data(data);
                file.write(reinterpret_cast<const char*>(data.data()), static_cast<qint64>(data.size()));
                file.close();
                e->record_command(d->name, "save", format_script_arg(fi.absoluteFilePath().toStdString()));
            } else {
                QMessageBox::warning(this, TapeRecorderWindow::tr("Error"), TapeRecorderWindow::tr("Unable to save file!"));
            }
        }
    }
}

void TapeRecorderWindow::closeEvent(QCloseEvent *event)
{
    update_timer.stop();
    //Лентопротяжка зовет окно на каждой смене режима, и этот вызов идет с
    //потока эмуляции. Окно сейчас будет удалено (WA_DeleteOnClose), так что
    //сначала снимаем обработчик, иначе машина, продолжающая крутить ленту,
    //позовет его уже по освобожденной памяти
    d->on_mode_changed = nullptr;
    //Лента, которую держит сама машина (линия двигателя поднята), закрытием
    //окна не останавливается: окно к ней отношения не имеет
    if (!d->is_machine_driven()) d->stop();
    GenericDbgWnd::closeEvent(event);
}

static QString tape_time(int seconds)
{
    return QString::number(seconds / 60) + ":" + QString("%1").arg(seconds % 60, 2, 10, QChar('0'));
}

//horizontalAdvance появился в Qt 5.11, а сборка для XP идет на 5.6; width()
//в Qt 6 убрали совсем
static int text_width(const QFontMetrics & fm, const QString & s)
{
    #if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
        return fm.width(s);
    #else
        return fm.horizontalAdvance(s);
    #endif
}

//Выедаем середину, а не конец: расширение говорит, что за лента.
//QFontMetrics::elidedText тут не годится - он меряет без межбуквенного
//интервала, который табло задает стилем, и на длинном имени промахивается
static QString elide_middle(const QFontMetrics & fm, const QString & name, int width)
{
    if (text_width(fm, name) <= width) return name;
    for (int cut = 1; cut < name.length(); cut++) {
        const int head = (name.length() - cut) / 2;
        const QString s = name.left(head) + QString::fromUtf8("…") + name.mid(head + cut);
        if (text_width(fm, s) <= width) return s;
    }
    return QString::fromUtf8("…");
}

QString TapeRecorderWindow::fit_name(const QString & time) const
{
    //Стиль табло задает межбуквенный интервал, а до первого show() его еще не
    //применили: без этого мы померяли бы куда более узкую строку
    ui->textLabel->ensurePolished();
    //Табло узкое и не растягивается, а имя - единственное, что можно ужать:
    //время должно быть видно всегда
    const QFontMetrics fm = ui->textLabel->fontMetrics();
    const int left = ui->textLabel->width() - text_width(fm, " " + time);
    return elide_middle(fm, loaded_file, qMax(left, 0)) + " " + time;
}

void TapeRecorderWindow::update_counter()
{
    if (is_playing || is_moving){
        if (d->get_mode() != TAPE_STOPPED) {
            ui->textLabel->setText(fit_name(
                "(" + tape_time(d->get_position()) + "/" + tape_time(d->get_total()) + ")"
            ));
        } else {
            //Лента кончилась сама. Перематывать ее назад можно только тогда,
            //когда ее пустили из окна: машина, которая ведет лентопротяжку
            //сама, держит головку там, где ей нужно, и перемотка из окна
            //увела бы ее из-под системы
            if (!is_paused && !d->is_machine_driven()) {
                is_playing = false;
                play_pause();
                ui->buttonPlay->setChecked(false);
                d->rewind();
            }
        }
    } else {
        ui->textLabel->setText(fit_name("(" + tape_time(d->get_total()) + ")"));
    }
}

void TapeRecorderWindow::tape_mode_changed(MAYBE_UNUSED unsigned int new_mode)
{
    sync_from_device();
}

GenericDbgWnd * CreateTapeWindow(QWidget *parent, Emulator * e, ComputerDevice * d)
{
    return new TapeRecorderWindow(parent, e, d);
}



