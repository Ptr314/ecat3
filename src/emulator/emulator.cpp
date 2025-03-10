#include <QFileInfo>
#include <QThread>
#include <QRandomGenerator>
#include <QKeyEvent>

#include "globals.h"

#include "core.h"
#include "emulator.h"
#include "emulator/config.h"
#include "emulator/utils.h"

#include "emulator/devices/cpu/i8080.h"
#include "emulator/devices/common/i8255.h"
#include "emulator/devices/common/sound.h"
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
#include "emulator/devices/cpu/z80.h"
#include "emulator/devices/common/page_mapper.h"
#include "emulator/devices/common/generator.h"
#include "emulator/devices/cpu/6502.h"
#include "emulator/devices/specific/agat_fdc140.h"
#include "emulator/devices/specific/agat_display.h"
#include "emulator/devices/common/mapkeyboard.h"

Emulator::Emulator(QString work_path, QString data_path, QString software_path, QString ini_file):
    work_path(work_path),
    data_path(data_path),
    software_path(software_path),
    loaded(false),
    busy(false),
    local_counter(0),
    clock_counter(0),
    logger(nullptr)
{
    qDebug() << "INI path: " + ini_file;
    settings = new QSettings (ini_file, QSettings::IniFormat);
    use_threads = (read_setup("Startup", "threads", "1") == "1");

    connect(this, &Emulator::finished, this, &Emulator::stop_emulation, Qt::DirectConnection);

#ifdef LOGGER
    logger = new Logger(LOG_NAME);
#endif
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

    dm = new DeviceManager(logger);
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
    sd.data_path = data_path;
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

    // SDL_Event ev;

    if (!busy)
    {
        busy = true;

        // while (SDL_PollEvent(&ev))
        // {
        //     qDebug() << ev.type;
        // }

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
#ifdef SDL_SEPARATE_WINDOW
    SDLWindowRef = SDL_CreateWindow("Screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_SKIP_TASKBAR);
#else
    SDLWindowRef = SDL_CreateWindowFrom(p);
#endif

    SDLRendererRef = SDL_CreateRenderer(SDLWindowRef, -1, SDL_RENDERER_ACCELERATED);

    GenericDisplay * d = dynamic_cast<GenericDisplay*>(dm->get_device_by_name("display"));

    d->get_screen_constraints(&screen_sx, &screen_sy);

    screen_scale = read_setup("Video", "scale", "2").toDouble();
    screen_ratio = read_setup("Video", "ratio", QString::number(SCREEN_RATIO_43)).toInt();
    screen_filtering = read_setup("Video", "filtering", QString::number(SCREEN_FILTERING_NONE)).toInt();

    if (screen_ratio == SCREEN_RATIO_SQ)
        pixel_scale = 1;
    else
    if (screen_ratio == SCREEN_RATIO_43)
        pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
    else
        pixel_scale = ((double)screen_sy / (double)screen_sx);


#ifdef USE_SDL
    std::string s = std::to_string(screen_filtering);
    char const *pchar = s.c_str();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);
#endif

    render_rect.w = screen_sx * screen_scale * pixel_scale;
    render_rect.h = screen_sy * screen_scale;

#ifdef USE_SDL
    //window_surface = SDL_GetWindowSurface(SDLWindowRef);
    device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_sx, screen_sy, 32, SDL_PIXELFORMAT_RGBA8888);
    d->set_surface(device_surface);

    black_box = SDL_CreateTexture(SDLRendererRef, SDL_PIXELFORMAT_RGBA8888,
                                               SDL_TEXTUREACCESS_STREAMING, 100, 100);
    unsigned char* black_bytes = nullptr;
    int pitch = 0;
    SDL_LockTexture(black_box, nullptr, reinterpret_cast<void**>(&black_bytes), &pitch);
    memset(black_bytes, 0, 100*100*4);
    SDL_UnlockTexture(black_box);
#else
    device_surface = new QImage(screen_sx, screen_sy, QImage::Format_RGB32);
    black_box = new QImage(100, 100, QImage::Format_RGB32);
    black_box->fill(0);
#endif
}

void Emulator::stop_video()
{
#ifdef USE_SDL
    SDL_DestroyTexture(black_box);
    SDL_FreeSurface(device_surface);
    //SDL_FreeSurface(window_surface);
    SDL_DestroyRenderer(SDLRendererRef);
    SDL_DestroyWindow(SDLWindowRef);
#else
    delete black_box;
    delete device_surface;
#endif
}

void Emulator::render_screen()
{
    unsigned int current_sx, current_sy;
    display->get_screen_constraints(&current_sx, &current_sy);
    if ((current_sx != screen_sx) || (current_sy != screen_sy))
    {
        screen_sx = current_sx;
        screen_sy = current_sy;

        #ifdef USE_SDL
            SDL_FreeSurface(device_surface);
        #else
            delete device_surface;
        #endif

        render_rect.w = screen_sx * screen_scale * pixel_scale;
        render_rect.h = screen_sy * screen_scale;

        #ifdef USE_SDL
            device_surface = SDL_CreateRGBSurfaceWithFormat(0, screen_sx, screen_sy, 32, SDL_PIXELFORMAT_RGBA8888);
        #else
            device_surface = new QImage(screen_sx, screen_sy, QImage::Format_RGB32);
        #endif

        display->set_surface(device_surface);

        #ifdef USE_SDL
            // We need to blank old screen contents
            SDL_RenderCopy(SDLRendererRef, black_box, NULL, NULL);
        #endif
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
    GenericSound * sound = dynamic_cast<GenericSound*>(dm->get_device_by_name("sound", false));
    if (sound != nullptr) sound->set_volume(value);
}

void Emulator::set_muted(bool muted)
{
    GenericSound * sound = dynamic_cast<GenericSound*>(dm->get_device_by_name("sound", false));
    if (sound != nullptr) sound->set_muted(muted);
}

Emulator::~Emulator()
{
    delete im;
    delete dm;

#ifdef LOGGER
    delete logger;
#endif

}

SDL_Surface *  Emulator::get_surface()
{
    return device_surface;
}

void Emulator::get_screen_constraints(unsigned int * sx, unsigned int * sy)
{
    display->get_screen_constraints(sx, sy);
}

#ifdef LOGGER
void Emulator::logs(ComputerDevice * d, QString s)
{
    if (logger != nullptr) logger->logs(d->name + ": " + s);
}
#endif

SystemData * Emulator::get_system_data()
{
    return &sd;
}

void Emulator::set_scale(int scale)
{
    screen_scale = scale;
    screen_sx = 0;

    write_setup("Video", "scale", QString::number(screen_scale) );
}

void Emulator::set_ratio(int ratio)
{
    screen_ratio = ratio;
    if (ratio == SCREEN_RATIO_SQ)
        pixel_scale = 1;
    else
    if (ratio == SCREEN_RATIO_43)
        pixel_scale = (4.0 / 3.0) / ((double)screen_sx / (double)screen_sy);
    else
        pixel_scale = ((double)screen_sy / (double)screen_sx);

    screen_sx = 0;

    write_setup("Video", "ratio", QString::number(screen_ratio) );
}

void Emulator::set_filtering(int filtering)
{
#ifdef USE_SDL
    screen_filtering = filtering;
    std::string s = std::to_string(filtering);
    char const *pchar = s.c_str();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, pchar);

    screen_sx = 0;

    write_setup("Video", "filtering", QString::number(filtering) );
#endif
}

int Emulator::get_scale()
{
    return round(screen_scale);
}

int Emulator::get_ratio()
{
    return screen_ratio;
}

int Emulator::get_filtering()
{
    return screen_filtering;
}


void Emulator::register_devices()
{
    dm->register_device("ram", create_ram);
    dm->register_device("rom", create_rom);
    dm->register_device("memory-mapper", create_memory_mapper);
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
    dm->register_device("z80", create_z80);
    dm->register_device("page-mapper", create_page_mapper);
    dm->register_device("generator", create_generator);
    dm->register_device("6502", create_mos6502);
    dm->register_device("65c02", create_wdc65c02);
    dm->register_device("agat-fdc140", create_agat_fdc140);
    dm->register_device("agat-display", create_agat_display);
    dm->register_device("map-keyboard", create_mapkeyboard);
}
