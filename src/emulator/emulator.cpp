#include <QFileInfo>
#include <QThread>
#include <QRandomGenerator>
#include <QKeyEvent>

#include "core.h"
#include "emulator.h"
#include "emulator/config.h"
#include "emulator/utils.h"

#include "emulator/devices/cpu/i8080.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/devices/common/speaker.h"
#include "emulator/devices/common/tape.h"
#include "emulator/devices/common/scankeyboard.h"
#include "emulator/devices/specific/o128display.h"
#include "emulator/devices/common/wd1793.h"
#include "emulator/devices/common/fdd.h"
#include "emulator/devices/common/i8257.h"
#include "emulator/devices/common/i8275.h"
#include "emulator/devices/common/i8275display.h"
#include "emulator/devices/common/i8253.h"
#include "emulator/devices/common/register.h"

Emulator::Emulator(QString work_path, QString data_path, QString software_path, QString ini_file):
    work_path(work_path),
    data_path(data_path),
    software_path(software_path),
    loaded(false),
    busy(false),
    local_counter(0),
    clock_counter(0)
{
    qDebug() << "INI path: " + ini_file;
    settings = new QSettings (ini_file, QSettings::IniFormat);
    use_threads = (read_setup("Startup", "threads", "1") == "1");

    connect(this, &Emulator::finished, this, &Emulator::stop_emulation, Qt::DirectConnection);
}

QString Emulator::read_setup(QString section, QString ident, QString def_val)
{
    QString value = settings->value(section + "/" + ident, def_val).toString();
    return value;
}

void Emulator::write_setup(QString section, QString ident, QString new_val)
{
    settings->setValue(section + "/" + ident, new_val);
}


void Emulator::load_config(QString file_name)
{
    if (loaded)
    {
        //Delete loaded machine
        delete dm;
        delete im;
        loaded = false;
    }

    dm = new DeviceManager();
    im = new InterfaceManager(dm);

    register_devices();

    EmulatorConfig config(file_name);

    EmulatorConfigDevice * system = config.get_device("system");
    QFileInfo fi(file_name);
    sd.system_file = file_name;
    sd.system_path = fi.absolutePath() + "/";
    sd.system_type = system->get_parameter("type").value;
    sd.system_name = system->get_parameter("name").value;
    sd.system_version = system->get_parameter("version", false).value;
    sd.system_charmap = system->get_parameter("charmap", false).value;
    sd.software_path = software_path;
    sd.mapper_cache = parse_numeric_value(read_setup("Core", "mapper_cache", "8"));

    sd.allowed_files = system->get_parameter("files", false).value;

    load_charmap();

    for (unsigned int i=0; i<config.devices_count; i++)
    {
        EmulatorConfigDevice * d = config.get_device(i);
        if (!d->type.isEmpty())
        {
            dm->add_device(im, d);
        }
    }
    dm->load_devices_config(&sd);
    loaded = true;
}

void Emulator::load_charmap()
{
    //TODO: delete old charmap objects

    if (!sd.system_charmap.isEmpty())
    {
        QFile f(data_path + sd.system_charmap + ".chr");
        f.open(QFile::ReadOnly);
        QString s = QString::fromUtf8(f.readAll());

        unsigned int count = 0;
        for (unsigned int i = 0; i < s.length(); i++)
        {
            QChar c = s.at(i);
            if (c != '\x0D' && c != '\x0A') charmap[count++] = new QChar(c);
        }
    } else
        for (unsigned int i = 0; i < 256; i++) charmap[i] = new QChar('.');
}

QChar * Emulator::translate_char(unsigned int char_code)
{
    return charmap[char_code];
}

void Emulator::reset(bool cold)
{
    dm->reset_devices(cold);
}

void Emulator::run()
{
    if (loaded)
    {
        //qDebug() << "Emulator thread id: " << QThread::currentThreadId();

        cpu = dynamic_cast<CPU*>(dm->get_device_by_name("cpu"));
        mm = dynamic_cast<MemoryMapper*>(dm->get_device_by_name("mapper"));
        display = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display"));
        keyboard = dynamic_cast<Keyboard*>(dm->get_device_by_name("keyboard"));

        reset(true);

        clock_freq = this->cpu->clock;
        timer_res = parse_numeric_value(read_setup("Core", "TimerResolution", "1"));
        timer_delay = parse_numeric_value(read_setup("Core", "TimerDelay", "20"));
        time_ticks = this->clock_freq * timer_delay / 1000;

        local_counter = 0;
        clock_counter = 0;

        //TODO: Unify calling timers
        timer = new QTimer();
        connect(timer, &QTimer::timeout, this, &Emulator::timer_proc);
        timer->start(timer_delay);

        render_timer = new QTimer();
        connect(render_timer, &QTimer::timeout, this, &Emulator::render_screen);
        render_timer->start(1000 / 50);

        if (use_threads) exec();
    }
}

void Emulator::stop_emulation()
{
    if (this->loaded)
    {
        timer->stop();
        render_timer->stop();
        //TODO: Other stopping stuff
    }
}

void Emulator::timer_proc()
{
    //TODO: Other timer stuff?
    if (!busy)
    {
        busy = true;

        while (local_counter < time_ticks) {

            unsigned int counter = cpu->execute();
            local_counter += counter;
            clock_counter += counter;
            dm->clock(counter);
        }
        mm->sort_cache();
        local_counter -= time_ticks;
        busy = false;
    }
}


void Emulator::init_video(void *p)
{
    //SDLWindowRef = SDL_CreateWindowFrom(p);
    SDLWindowRef = SDL_CreateWindow("Screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1600, 1200, SDL_WINDOW_SHOWN | SDL_WINDOW_SKIP_TASKBAR);
    SDLRendererRef = SDL_CreateRenderer(SDLWindowRef, -1, SDL_RENDERER_ACCELERATED);
    //TODO: create setup parameters
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    GenericDisplay * d = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display"));

    d->get_screen_constraints(&screen_sx, &screen_sy);

    screen_scale = 3;
    pixel_scale = 1; //(4.0 / 3.0) / ((double)sx / (double)sy);

    render_rect.w = screen_sx * screen_scale * pixel_scale;
    render_rect.h = screen_sy * screen_scale;

    //window_surface = SDL_GetWindowSurface(SDLWindowRef);
    device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_sx, screen_sy, 32, SDL_PIXELFORMAT_RGBA8888);
    d->set_surface(device_surface);
}

void Emulator::stop_video()
{
    SDL_FreeSurface(device_surface);
    //SDL_FreeSurface(window_surface);
    SDL_DestroyRenderer(SDLRendererRef);
    SDL_DestroyWindow(SDLWindowRef);
}

void Emulator::render_screen()
{
    unsigned int current_sx, current_sy;
    display->get_screen_constraints(&current_sx, &current_sy);
    if ((current_sx != screen_sx) || (current_sy != screen_sy))
    {
        screen_sx = current_sx;
        screen_sy = current_sy;
        SDL_FreeSurface(device_surface);

        render_rect.w = screen_sx * screen_scale * pixel_scale;
        render_rect.h = screen_sy * screen_scale;

        device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_sx, screen_sy, 32, SDL_PIXELFORMAT_RGBA8888);
        display->set_surface(device_surface);
    }

    display->validate();

    if (display->was_updated)
    {
        int rx, ry;
        SDL_GetRendererOutputSize(SDLRendererRef, &rx, &ry);
        render_rect.x = (rx - render_rect.w) / 2;
        render_rect.y = (ry - render_rect.h) / 2;

        SDLTexture = SDL_CreateTextureFromSurface(SDLRendererRef, device_surface);

        SDL_RenderCopy(SDLRendererRef, SDLTexture, NULL, &render_rect);
        SDL_RenderPresent(SDLRendererRef);

        SDL_DestroyTexture(SDLTexture);
        display->was_updated = false;
    }
}

void Emulator::resize_screen()
{
    display->validate(true);
}

void Emulator::key_event(QKeyEvent *event, bool press)
{
    keyboard->key_event(event, press);
    if (event->key() == Qt::Key_F12) display->validate(true);
}

void Emulator::set_volume(int value)
{
    Sound * sound = dynamic_cast<Sound*>(dm->get_device_by_name("sound", false));
    if (sound != nullptr) sound->set_volume(value);
}

void Emulator::set_muted(bool muted)
{
    Sound * sound = dynamic_cast<Sound*>(dm->get_device_by_name("sound", false));
    if (sound != nullptr) sound->set_muted(muted);
}

Emulator::~Emulator()
{
    delete im;
    delete dm;
}

SDL_Surface *  Emulator::get_surface()
{
    return device_surface;
}

void Emulator::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    display->get_screen_constraints(sx, sy);
}

void Emulator::register_devices()
{
    dm->register_device("ram", create_ram);
    dm->register_device("rom", create_rom);
    dm->register_device("memory_mapper", create_memory_mapper);
    dm->register_device("port", create_port);
    dm->register_device("port-address", create_port_address);
    dm->register_device("speaker", create_speaker);
    dm->register_device("taperecorder", create_tape_recorder);
    dm->register_device("scan-keyboard", create_scankeyboard);
    dm->register_device("i8080", create_i8080);
    dm->register_device("i8255", create_i8255);
    dm->register_device("wd1793", create_WD1793);
    dm->register_device("fdd", create_FDD);
    dm->register_device("orion-128-display", create_o128display);
    dm->register_device("i8257", create_i8257);
    dm->register_device("i8275", create_i8275);
    dm->register_device("i8275-display", create_i8275display);
    dm->register_device("i8253", create_i8253);
    dm->register_device("register", create_register);
}
