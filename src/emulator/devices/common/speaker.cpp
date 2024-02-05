#include "speaker.h"
#include "emulator/utils.h"

#define SPK_MODE_LEVEL  0
#define SPK_MODE_FLIP   1

Speaker::Speaker(InterfaceManager *im, EmulatorConfigDevice *cd):
    GenericSound(im, cd),
    mode(SPK_MODE_LEVEL),
    flip_value(0)
{
    i_input = create_interface(1, "input", MODE_R, 1);
    i_mixer = create_interface(8, "mixer", MODE_R);
}

unsigned int Speaker::calc_sound_value()
{
    unsigned int V = 0;
    if (InputWidth != 0)
        V += ((mode==SPK_MODE_FLIP)?flip_value:i_input->value) & 0x01;
    for (unsigned int i=0; i < MixerWidth; i++)
        V += (i_mixer->value >> i) & 0x01;
    return V * InputValue * volume / 100 + 127;
}

void Speaker::reset(bool cold)
{
    GenericSound::reset(cold);

    if (i_input->linked == 0)
        InputWidth = 0;
    else
        InputWidth = 1;

    if (i_mixer->linked == 0)
        MixerWidth = 0;
    else
        MixerWidth = CalcBits(i_mixer->linked_bits, 8);

    if (InputWidth + MixerWidth > 0)
        InputValue = 127 / (InputWidth + MixerWidth);
    else
        InputValue = 1;
}

void Speaker::load_config(SystemData *sd)
{
    GenericSound::load_config(sd);

    QString s = cd->get_parameter("mode", false).value.toLower();
    if (s.isEmpty() || s == "level")
        mode = SPK_MODE_LEVEL;
    else
    if (s == "flip")
        mode = SPK_MODE_FLIP;
    else
        QMessageBox::critical(0, Speaker::tr("Error"), Speaker::tr("Unknown speaker type %1").arg(s));
}

void Speaker::interface_callback(unsigned int callback_id, unsigned int new_value, unsigned int old_value)
{
    if (mode == SPK_MODE_FLIP) flip_value ^= 1;
}

ComputerDevice * create_speaker(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new Speaker(im, cd);
}
