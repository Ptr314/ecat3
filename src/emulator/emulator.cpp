#include <QFileInfo>
#include <QThread>
#include <QRandomGenerator>

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

Emulator::Emulator(QString work_path, QString data_path, QString ini_file):
    work_path(work_path),
    data_path(data_path),
    loaded(false),
    busy(false),
    local_counter(0),
    clock_counter(0)
{
    qDebug() << "INI path: " + ini_file;
    this->settings = new QSettings (ini_file, QSettings::IniFormat);
}

QString Emulator::read_setup(QString section, QString ident, QString def_val)
{
    QString value = this->settings->value(section + "/" + ident, def_val).toString();
    return value;
}

void Emulator::load_config(QString file_name)
{
    if (this->loaded)
    {
        //Delete loaded machine
        delete this->dm;
        this->loaded = false;
    }

    this->dm = new DeviceManager();
    this->im = new InterfaceManager(this->dm);

    this->register_devices();

    EmulatorConfig * config = new EmulatorConfig(work_path + file_name);

    EmulatorConfigDevice * system = config->get_device("system");
    QFileInfo fi(work_path + file_name);
    sd.system_file = file_name;
    sd.system_path = fi.absolutePath() + "/";
    sd.system_type = system->get_parameter("type").value;
    sd.system_name = system->get_parameter("name").value;
    sd.system_version = system->get_parameter("version", false).value;
    sd.system_charmap = system->get_parameter("charmap", false).value;
    sd.software_path = work_path;
    sd.mapper_cache = parse_numeric_value(this->read_setup("Core", "mapper_cache", "8"));

    sd.allowed_files = system->get_parameter("files", false).value;

    this->load_charmap();

    for (unsigned int i=0; i<config->devices_count; i++)
    {
        EmulatorConfigDevice *d = config->get_device(i);
        if (!d->type.isEmpty())
        {
            this->dm->add_device(this->im, d);
        }
    }
    this->dm->load_devices_config(&sd);
    delete config;
    this->loaded = true;
}

void Emulator::load_charmap()
{
    //TODO: delete old charmap objects

    if (!sd.system_charmap.isEmpty())
    {
        QFile f(this->data_path + this->sd.system_charmap + ".chr");
        f.open(QFile::ReadOnly);
        QString s = QString::fromUtf8(f.readAll());

        unsigned int count = 0;
        for (unsigned int i = 0; i < s.length(); i++)
        {
            QChar c = s.at(i);
            if (c != '\x0D' && c != '\x0A') this->charmap[count++] = new QChar(c);
        }
    } else
        for (unsigned int i = 0; i < 256; i++) this->charmap[i] = new QChar('.');
}

QChar * Emulator::translate_char(unsigned int char_code)
{
    return this->charmap[char_code];
}

void Emulator::reset(bool cold)
{
    //TODO: understand the difference
    this->dm->reset_devices(cold);
}

void Emulator::start()
{
    if (this->loaded)
    {
        this->cpu = (CPU*)this->dm->get_device_by_name("cpu");
        this->mm = (MemoryMapper*)this->dm->get_device_by_name("mapper");
        this->display = (Display*)this->dm->get_device_by_name("display");
        this->keyboard = (Keyboard*)this->dm->get_device_by_name("keyboard");

        this->reset(true);

        this->clock_freq = this->cpu->clock;
        this->timer_res = parse_numeric_value(this->read_setup("Core", "TimerResolution", "1"));
        this->timer_delay = parse_numeric_value(this->read_setup("Core", "TimerDelay", "20"));
        this->time_ticks = this->clock_freq * this->timer_delay / 1000;

        this->local_counter = 0;
        this->clock_counter = 0;

        //TODO: Unify calling timers
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, QOverload<>::of(&Emulator::timer_proc));
        timer->start(this->timer_delay);

        this->render_timer = new QTimer(this);
        connect(this->render_timer, SIGNAL(timeout()), this, SLOT(render_screen()));
        this->render_timer->start(1000 / 50);
    }
}

void Emulator::stop()
{
    if (this->loaded)
    {
        this->timer->stop();
        //TODO: Other stopping stuff
    }
}

unsigned int tmp = 0;

void Emulator::timer_proc()
{
    //TODO: Other timer stuff?
    if (!this->busy)
    {
        this->busy = true;

        process_events();

        display->start_rendering();
        while (this->local_counter < this->time_ticks) {

            unsigned int counter = this->cpu->execute();
            this->local_counter += counter;
            this->clock_counter += counter;
            this->dm->clock(counter);
        }
        this->mm->sort_cache();

        this->local_counter -= this->time_ticks;
        display->stop_rendering();


        //TODO: Cleanup
//        //Debug block {
//        QRandomGenerator *rg = QRandomGenerator::global();

//        Memory * m1 = (Memory*)(dm->get_device_by_name("ram0"));
//        Memory * m2 = (Memory*)(dm->get_device_by_name("ram1"));
//        m1->set_value(0xC000 + tmp/2, rg->bounded(256));
//        m2->set_value(0xC000 + tmp/2, rg->bounded(256));
//        tmp++;
//        //} Debig block

        this->busy = false;
    }
}


void Emulator::init_video(void *p)
{
    SDLWindowRef = SDL_CreateWindowFrom(p);
    SDLRendererRef = SDL_CreateRenderer(SDLWindowRef, -1, SDL_RENDERER_ACCELERATED);

    Display * d = (Display*)dm->get_device_by_name("display");
    unsigned int sx, sy;
    d->get_screen_constraints(&sx, &sy);

    //TODO: create setup parameters
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    double screen_scale = 3;
    double pixel_scale = 1; //(4.0 / 3.0) / ((double)sx / (double)sy);
    int rx, ry;

    SDL_GetRendererOutputSize(SDLRendererRef, &rx, &ry);
    //SDL_GetWindowSize(SDLWindowRef, &rx, &ry);
    //dynamic_cast<QWidget*>(p)->
    render_rect.w = sx * screen_scale * pixel_scale;
    render_rect.h = sy * screen_scale;

    SDLTexture = SDL_CreateTexture(SDLRendererRef, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, sx, sy);

    d->set_texture(SDLTexture);


    //TODO: cleanup
//    SDL_RendererInfo info;
//    SDL_GetRendererInfo( SDLRendererRef, &info );
//    qDebug() << "Renderer name: " << info.name;
//    qDebug() << "Texture formats: ";
//    for( Uint32 i = 0; i < info.num_texture_formats; i++ )
//    {
//        qDebug() << SDL_GetPixelFormatName( info.texture_formats[i] );
//    }
}

void Emulator::stop_video()
{
    SDL_DestroyRenderer(SDLRendererRef);
    SDL_DestroyWindow(SDLWindowRef);
}

void Emulator::render_screen()
{
    //TODO: cleanup
//    void *pixels;
//    Uint8 *base;
//    int pitch;

//    SDL_LockTexture(SDLTexture, NULL, &pixels, &pitch);

//    QRandomGenerator *rg = QRandomGenerator::global();

//    int x = rg->bounded(384);
//    int y = rg->bounded(256);
//    base = ((Uint8 *)pixels) + (4 * (y * 384 + x));

//    base[0] = rg->bounded(256);
//    base[1] = rg->bounded(256);
//    base[2] = rg->bounded(256);
//    //base[3] = 0;

//    SDL_UnlockTexture(SDLTexture);
    display->validate();

    //TODO: move this code to resize event
    int rx, ry;
    SDL_GetRendererOutputSize(SDLRendererRef, &rx, &ry);
    render_rect.x = (rx - render_rect.w) / 2;
    render_rect.y = (ry - render_rect.h) / 2;

    SDL_RenderCopy(SDLRendererRef, SDLTexture, NULL, &render_rect);
    SDL_RenderPresent(SDLRendererRef);
}

void Emulator::process_events()
{
//    SDL_Event event;
//    while (SDL_PollEvent(&event) == 1) {
//        if (event.type == SDL_QUIT) {
//            //TODO: Maybe we need to to something here
//            return;
//        } else if (event.type == SDL_KEYDOWN) {
//            qDebug() << "Down: " << event.key.keysym.sym;
//        } else if (event.type == SDL_KEYUP) {
//            qDebug() << "Up: " << event.key.keysym.sym;
//        }
//    }
}

void Emulator::key_event(QKeyEvent *event, bool press)
{
    keyboard->key_event(event, press);
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
    dm->register_device("orion-128-display", create_o128display);
}
