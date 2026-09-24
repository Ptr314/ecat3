// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder device

#include "emulator/utils.h"
#include "emulator/devices/cpu/cpu_utils.h"
#include "tape.h"
#include "tape_bk.h"
#include "dsk_tools/dsk_tools.h"
//Часть помощников dsk_tools объявлена в его внутреннем заголовке, и звать
//его надо по полному пути: короткий "utils.h" из dsk_tools.h у MSVC попадает
//в emulator/utils.h - он ищет кавычечный include и по цепочке включающих
#include "libs/dsk_tools/src/utils.h"

// Разряды линии управления лентопротяжкой у ленты-накопителя. Сверено с
// конфигурацией машины в эмуляторе b2m и с тем, как их выдает ПЗУ Юниора:
// команда ввода с ленты начинается с $10 (FC48), запись - с $18 (FC43), а
// заканчивается все импульсом $80 (FC37)
#define CTL_REC      0x08
#define CTL_PLAY     0x10
#define CTL_BACK     0x20
#define CTL_FORWARD  0x40
#define CTL_STOP     0x80

//Тишина на ленте: байты идут вплотную, по восемь битовых интервалов, так что
//пропуск двух байт - это уже пауза между записями
#define SILENCE_BITS 16

#define CB_CONTROL  10
#define CB_DATA_IN  11

TapeRecorder::TapeRecorder(InterfaceManager *im, EmulatorConfigDevice *cd)
    : ComputerDevice(im, cd)
    , baud_rate(0)
    , tape_mode(TAPE_STOPPED)
    , data_size(0)
    , data_position(0)
    , bit_shift(7)
    , ticks_counter(0)
    , i_input(this, im, 1, "input", MODE_R, 1)
    , i_output(this, im, 1, "output", MODE_W)
    , i_speaker(this, im, 1, "speaker", MODE_W)
    , i_motor(this, im, 1, "motor", MODE_R, 2)
    , i_control(this, im, 8, "control", MODE_R, CB_CONTROL)
    , i_data_out(this, im, 8, "data_out", MODE_W)
    , i_data_in(this, im, 8, "data_in", MODE_R, CB_DATA_IN)
    , i_ready(this, im, 1, "ready", MODE_W)
{
    m_clocked = true;   //clock() is overridden here
    device_class = "tape";

    //No lookup of the CPU here: a constructor must not reach for another device.
    //The clock arrives as m_system_clock from ComputerDevice::load_config()

    speaker_config = new EmulatorConfigDevice(name + "-speaker", "speaker");
    speaker_config->add_parameter("~input", "", name + ".speaker", "", "");

    speaker = new Speaker(im, speaker_config);
}

TapeRecorder::~TapeRecorder()
{
    delete speaker;
    delete speaker_config;
}

emulator::Result TapeRecorder::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    baud_rate = read_confg_value(cd, "baudrate", false, (unsigned int)1200);

    const std::string enc_str = str_tolower(cd->get_parameter("encoding", false).value);
    if (enc_str.empty() || enc_str == "msx")
        m_tape_enc = TapeEnc::MSX;
    else if (enc_str == "rk86")
        m_tape_enc = TapeEnc::RK86;
    else if (enc_str == "uknc")
        m_tape_enc = TapeEnc::UKNC;
    else if (enc_str == "bk")
        m_tape_enc = TapeEnc::BK;
    else if (enc_str == "unior")
        m_tape_enc = TapeEnc::UNIOR;
    else
        return emulator::Result::error(emulator::ErrorCode::ConfigError, "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Incorrect encoding")) + "} " + enc_str);

    //Носитель по умолчанию задает конфигурация, но последнее слово за
    //загруженным файлом: у Арго магнитофон один, и лента Спектрума приходит
    //в него тем же путем, что своя
    medium = (m_tape_enc == TapeEnc::UNIOR)?TapeMedium::Bytes:TapeMedium::Levels;

    files = cd->get_parameter("files", false).value;

    if (files.empty()) files = sd->allowed_files;

    //Перемотка идет быстрее воспроизведения во столько же раз, во сколько у
    //настоящего магнитофона
    m_fast_speed = read_confg_value(cd, "fast_speed", false, (unsigned int)15);
    if (m_fast_speed < 1) m_fast_speed = 1;

    //Длина чистой кассеты: полчаса - это сторона С-60
    const unsigned int blank_seconds = read_confg_value(cd, "blank_length", false, (unsigned int)1800);
    m_blank_units = (uint64_t)blank_seconds * baud_rate;

    if (medium == TapeMedium::Bytes)
    {
        ticks_per_bit = (baud_rate > 0)?(m_system_clock / baud_rate):1;
        if (ticks_per_bit < 1) ticks_per_bit = 1;
        m_carrier = false;
        i_ready.change(1);          // Сигнала нет: DSR неактивен
    }

    res = speaker->load_config(sd);
    if (!res) return res;

    speaker->reset(true);

    const std::string image = cd->get_parameter("image", false).value;
    if (!image.empty())
    {
        const std::string file = find_file_location(sd, image);
        if (file.empty())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Tape image file not found")) + "} " + image);
        //Формат берется по расширению, а если секция [TapeFiles] о нем
        //молчит - по кодировке машины: образ накопителя самоописателен
        const std::string fmt = format_for_extension(dsk_tools::get_file_ext(image));
        res = fmt.empty()?bytes_load_file(file):load_file(file, fmt);
        if (!res) return res;
    }

    return emulator::Result::ok();
}

void TapeRecorder::interface_callback(unsigned callback_id, unsigned new_value, MAYBE_UNUSED unsigned old_value)
{
    if (callback_id == CB_CONTROL)
    {
        const unsigned int v = new_value & 0xFF;
        //Стоп сильнее всего: ПЗУ гасит лентопротяжку импульсом $80
        if (v & CTL_STOP)           set_transport(T_STOP);
        else if (v & CTL_REC)       set_transport((v & CTL_PLAY)?T_RECORD:T_STOP);
        else if (v & CTL_PLAY)      set_transport(T_PLAY);
        else if (v & CTL_BACK)      set_transport(T_BACK);
        else if (v & CTL_FORWARD)   set_transport(T_FORWARD);
        else                        set_transport(T_STOP);
        return;
    }

    if (callback_id == CB_DATA_IN)
    {
        //Байт от передатчика ВВ51. Пока лентопротяжка в записи, он ложится на
        //ленту; в остальных положениях машина пишет в никуда, как и на живой
        if (m_transport == T_RECORD)
        {
            //Запись ложится туда, где головка была на первом байте, а не там,
            //где включили лентопротяжку: драйвер TCP/M запускает ее и ждет по
            //таймеру почти три секунды, и настоящий блок лежит на ленте именно
            //на этом расстоянии от своего маркера
            if (m_write_buf.empty()) m_write_start = m_pos;
            m_write_last = m_pos;
            m_write_buf.push_back((uint8_t)(new_value & 0xFF));
            //Этот же байт идет в динамик: пишущую ленту слышно, как и читающую
            m_write_byte = (uint8_t)(new_value & 0xFF);
            m_write_bits = 8;
        }
        return;
    }

    if (callback_id == 1) {
        // Input changed
        if (is_recording) {
            if (m_tape_enc == TapeEnc::BK) {
                // The БК decoder measures whole periods between rising edges,
                // the same reference point the monitor uses
                if ((old_value & 1) == 0 && (new_value & 1) != 0) {
                    if (has_last_edge)
                        bk_decoder.add_period((uint32_t)(cycle_counter - last_edge_cycles));
                    last_edge_cycles = cycle_counter;
                    has_last_edge = true;
                }
                return;
            }
            if (m_tape_enc == TapeEnc::UKNC) {
                // The УК-НЦ reader times the interval between any two edges,
                // so the decoder gets every one of them
                if (((old_value ^ new_value) & 1) == 0) return;
                if (has_last_edge)
                    uknc_decoder.add_half((uint32_t)(cycle_counter - last_edge_cycles));
                last_edge_cycles = cycle_counter;
                has_last_edge = true;
                return;
            }
            if (m_tape_enc == TapeEnc::RK86) {
                // Both edges carry information here: a bit cell is two half
                // periods and the level of every one of them is a half of the
                // answer, so what the decoder gets is the level that just
                // ended and how long it was held
                if (((old_value ^ new_value) & 1) == 0) return;
                if (has_last_edge)
                    rk86_decoder.add_run((uint8_t)(old_value & 1),
                                         (uint32_t)(cycle_counter - last_edge_cycles));
                last_edge_cycles = cycle_counter;
                last_level = (uint8_t)(new_value & 1);
                has_last_edge = true;
                return;
            }
            if ((old_value & 1) != 0 && (new_value & 1) == 0) {
                if (has_last_edge) {
                    const uint64_t delta_cycles = cycle_counter - last_edge_cycles;
                    // const unsigned delta_us = static_cast<unsigned int>(delta_cycles * 1000000 / m_system_clock);
                    write_edge(delta_cycles);
                }
                last_edge_cycles = cycle_counter;
                has_last_edge = true;
            }
        }
    } else {
        // Motor changed
        // std::cout << "TapeRecorder: motor = " << (new_value & 1) << std::endl;
        //Линия двигателя ведет саму лентопротяжку, а не только картинку в
        //окне: машина, которая управляет магнитофоном сама (Ириша поднимает
        //DTR, Юниор командует лентопротяжкой по разъему ДУ), должна получать
        //ленту в движении и без открытого окна - в том числе в сценариях
        if ((new_value & 1)==1 && (old_value & 1)==0) {
            motor_on = true;
            if (!is_recording && data_size>0) set_tape_mode(TAPE_READ);
        }
        if ((new_value & 1)==0 && (old_value & 1)==1) {
            motor_on = false;
            if (tape_mode == TAPE_READ) set_tape_mode(TAPE_STOPPED);
        }
    }
}

void TapeRecorder::write_edge(uint64_t counter)
{
    if (writer_state == TapeWriterState::Measuring) {
        measured_time = (measured_time * measured_counter + counter) / (measured_counter + 1);
        if (++measured_counter > 100) {
            writer_state = TapeWriterState::Preamble;
            measured_time_x2 = measured_time * 2;
            max_delta = measured_time / 8;
        }
        return;
    }
    const uint64_t diff_to_short = (counter > measured_time) ? (counter - measured_time) : (measured_time - counter);
    const uint64_t diff_to_long = (counter > measured_time_x2) ? (counter - measured_time_x2) : (measured_time_x2 - counter);
    bool is_short = diff_to_short < max_delta;
    bool is_long = diff_to_long < max_delta*2;
    if (writer_state == TapeWriterState::Preamble || writer_state == TapeWriterState::Stops) {
        if (is_long) {
            // Start bit detected
            writer_state = TapeWriterState::Data;
            byte_counter = 0;
            short_counter = 0;
            current_byte = 0;
        }
        return;
    }
    if (writer_state == TapeWriterState::Data) {
        if (is_short) {
            if (short_counter != 0) {
                // 1 detected
                short_counter = 0;
                store_bit(1);
            } else
                short_counter++;
        } else
        if (is_long) {
            // 0 detected
            short_counter = 0;
            store_bit(0);
        }
    }
}

void TapeRecorder::store_bit(const unsigned bit)
{
    current_byte = current_byte | ((bit & 1) << byte_counter);
    if (byte_counter++ > 6) {
        writer_state = TapeWriterState::Stops;
        if (recorded_bytes.size() == recorded_bytes.capacity())
            recorded_bytes.reserve(recorded_bytes.capacity() + 1024);
        recorded_bytes.push_back(current_byte);
    }
}

void TapeRecorder::set_recording(bool recording)
{
    //На ленту Спектрума мы не пишем: у нее контейнер под готовые блоки, а не
    //под сигнал, и запускать здесь чужой декодер значило бы молча писать мусор
    if (medium == TapeMedium::Pulses) return;
    if (medium == TapeMedium::Bytes) { set_transport(recording?T_RECORD:T_STOP); return; }
    if (!recording && is_recording && m_tape_enc == TapeEnc::RK86 && has_last_edge) {
        // The level the line was left at is the second half of the last bit
        // cell and carries its value, but no edge closes it: the machine stops
        // writing and the line stays where the last half period put it. The
        // run has to be closed here, or the last byte of the record - the low
        // half of the checksum - is one bit short and gets dropped.
        rk86_decoder.add_run(last_level, (uint32_t)(cycle_counter - last_edge_cycles));
        has_last_edge = false;
    }
    is_recording = recording;
    if (is_recording && m_tape_enc == TapeEnc::BK) {
        has_last_edge = false;
        bk_decoder.reset();
        recorded_bytes.clear();
    }
    if (is_recording && m_tape_enc == TapeEnc::UKNC) {
        has_last_edge = false;
        uknc_decoder.reset();
        recorded_bytes.clear();
    }
    if (is_recording && m_tape_enc == TapeEnc::RK86) {
        has_last_edge = false;
        rk86_decoder.reset();
        recorded_bytes.clear();
    }
    if (is_recording && m_tape_enc == TapeEnc::MSX) {
        has_last_edge = false;
        writer_state = TapeWriterState::Measuring;
        measured_counter = 0;
        measured_time = 0;
        recorded_bytes.clear();
        recorded_bytes.reserve(1024);
    }
    notify_state();
}

unsigned TapeRecorder::get_record_size()
{
    //У ленты-накопителя сохраняют не последнюю запись, а всю кассету, и
    //только если машина на нее писала
    if (medium == TapeMedium::Bytes)
    {
        if (!m_dirty) return 0;
        size_t total = 4;
        for (size_t i = 0; i < m_records.size(); i++) total += 8 + m_records[i].data.size() + 4;
        return (unsigned)total;
    }
    if (medium == TapeMedium::Pulses) return 0;
    if (m_tape_enc == TapeEnc::BK) return bk_decoder.file()->size();
    if (m_tape_enc == TapeEnc::UKNC) return uknc_decoder.file()->size();
    if (m_tape_enc == TapeEnc::RK86) {
        const size_t size = rk86_decoder.file()->size();
        const size_t skip = (size != 0 && !record_keeps_sync())? 1 : 0;
        return (unsigned)(size - skip);
    }
    return recorded_bytes.size();
}

// The first extension of a file mask like "Mikrosha (*.rkm)", with the dot.
// A compound one is kept whole: "*.uknc.tap" gives ".uknc.tap", so that a
// recording is offered under the name the machine's own tapes carry. Empty
// when the mask names none.
static std::string first_file_extension(const std::string &mask)
{
    const size_t p = mask.find("*.");
    if (p == std::string::npos) return "";
    std::string ext = ".";
    for (size_t i = p + 2; i < mask.size(); i++) {
        const char c = mask[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
            ext += c;
        else if (c == '.' && ext.back() != '.')
            ext += c;
        else
            break;
    }
    if (!ext.empty() && ext.back() == '.') ext.pop_back();
    return (ext.size() > 1)? ext : std::string("");
}

// The [TapeFiles] entry describing how a file of that extension is put on the
// tape. A machine specific entry wins over the generic one, the same way the
// tape recorder window resolves it.
std::string TapeRecorder::format_for_extension(const std::string &ext)
{
    if (ext.empty() || sd == nullptr || !sd->read_setup) return "";
    std::string fmt = sd->read_setup("TapeFiles", sd->system_type + "." + ext, "");
    if (fmt.empty()) fmt = sd->read_setup("TapeFiles", ext, "");
    return fmt;
}

// Whether a recorded file keeps the synchronisation byte. The formats of one
// and the same signal disagree about it: everything the [TapeFiles] entry
// lists in front of "data" is added on playback and therefore does not belong
// in the file, so a .rk drops the $E6 and a .rko, whose entry adds none, keeps
// it. Getting this wrong gives a file the machine that wrote it cannot read.
bool TapeRecorder::record_keeps_sync()
{
    const std::string ext = first_file_extension(files);
    if (ext.empty()) return false;
    const std::string fmt = format_for_extension(ext.substr(1));
    if (fmt.empty()) return false;

    std::vector<std::string> parts = split_string(fmt, ';', true);
    for (size_t i = 1; i < parts.size(); i++) {
        if (parts[i] == "data") break;
        std::vector<std::string> bytes = split_string(parts[i], ':', true);
        //The description comes from the ini file, where the bytes of a header
        //carry a prefix of their own
        if (!bytes.empty() && parse_numeric_value(bytes[0], 10) == rk86_tape::SYNC)
            return false;
    }
    return true;
}

// The name and, more importantly, the extension the recorded data has to be
// saved under to be readable back. Empty when there is nothing to suggest.
std::string TapeRecorder::get_record_name()
{
    if (medium == TapeMedium::Bytes) return loaded_name.empty()?std::string("tape.bt"):loaded_name;
    if (medium == TapeMedium::Pulses) return "";
    if (m_tape_enc == TapeEnc::BK) return bk_decoder.name();
    if (m_tape_enc == TapeEnc::RK86 || m_tape_enc == TapeEnc::UKNC) {
        // A Радио-86РК tape carries no name, only the addresses, but the
        // extension still decides how the file goes back on the tape, and the
        // one the machine loads from is the one it has just written
        const std::string ext = first_file_extension(files);
        return ext.empty()? std::string("") : "tape" + ext;
    }
    return "";
}

std::vector<uint8_t> * TapeRecorder::get_record_data()
{
    if (medium != TapeMedium::Levels) return &m_write_buf;
    if (m_tape_enc == TapeEnc::BK) {
        recorded_bytes = *bk_decoder.file();
        return &recorded_bytes;
    }
    if (m_tape_enc == TapeEnc::UKNC) {
        recorded_bytes = *uknc_decoder.file();
        return &recorded_bytes;
    }
    if (m_tape_enc == TapeEnc::RK86) {
        const std::vector<uint8_t> * f = rk86_decoder.file();
        const size_t skip = (!f->empty() && !record_keeps_sync())? 1 : 0;
        recorded_bytes.assign(f->begin() + skip, f->end());
        return &recorded_bytes;
    }
    return &recorded_bytes;
}

//Единственное место, где меняется режим лентопротяжки. Окно узнает о нем
//отсюда, кто бы ни был источником - кнопка, сценарий или сама машина
void TapeRecorder::set_tape_mode(unsigned int new_mode)
{
    if (tape_mode == new_mode) return;
    tape_mode = new_mode;
    notify_state();
}

void TapeRecorder::notify_state()
{
    if (on_mode_changed) on_mode_changed(tape_mode);
}

void TapeRecorder::play()
{
    if (medium != TapeMedium::Levels) { if (!m_records.empty()) set_transport(T_PLAY); return; }
    if (data_size > 0) set_tape_mode(TAPE_READ);
}

void TapeRecorder::stop()
{
    if (medium != TapeMedium::Levels) { set_transport(T_STOP); return; }
    set_tape_mode(TAPE_STOPPED);
}

void TapeRecorder::rewind()
{
    if (medium != TapeMedium::Levels)
    {
        //У ленты с лентопротяжкой откат к началу ее останавливает: на живой
        //машине перемотка и воспроизведение - разные клавиши
        set_transport(T_STOP);
        m_pos = 0;
        m_ticks = 0;
        if (medium == TapeMedium::Pulses) zx_locate(0); else locate(0);
        notify_state();
        return;
    }
    data_position = 0;
    bit_shift = 7;
    ticks_counter = 0;
}

void TapeRecorder::mute(bool muted)
{
    speaker->set_muted(muted);
}

void TapeRecorder::volume(unsigned int volume)
{
    speaker->set_volume(volume);

}

void TapeRecorder::set_baud_rate(unsigned int baud)
{
    baud_rate = baud;
    ticks_per_bit = m_system_clock / baud_rate;
}

void TapeRecorder::set_data(const std::vector<uint8_t> &new_data){
    data = new_data;
    set_tape_mode(TAPE_STOPPED);
    data_size = data.size();
    data_position = 0;
    bit_shift = 7;
    total_seconds = (data_size * 8) / baud_rate;
    ticks_counter = 0;
}

void TapeRecorder::encode_msx(const std::vector<uint8_t> &buffer, std::vector<uint8_t> &buffer_encoded)
{
    size_t buf_size = buffer.size();
    // 11 nibbles per byte: 1 start + 8 data + 2 stop
    buffer_encoded.reserve(buffer_encoded.size() + (buf_size * 11 + 1) / 2);

    std::vector<uint8_t> nibbles;
    nibbles.reserve(buf_size * 11);

    for (size_t i = 0; i < buf_size; i++) {
        uint8_t b = buffer[i];
        nibbles.push_back(0xC);  // Start bit: 0 -> 1100
        for (int j = 0; j < 8; j++) {
            int bit = (b >> j) & 1;
            nibbles.push_back(bit ? 0xA : 0xC);
        }
        nibbles.push_back(0xA);  // Stop bit 1: 1 -> 1010
        nibbles.push_back(0xA);  // Stop bit 2: 1 -> 1010
    }

    // Pack nibble pairs into bytes (MSB first)
    for (size_t i = 0; i < nibbles.size(); i += 2) {
        uint8_t hi = nibbles[i];
        uint8_t lo = (i + 1 < nibbles.size()) ? nibbles[i + 1] : 0xA;
        buffer_encoded.push_back((hi << 4) | lo);
    }
}

emulator::Result TapeRecorder::load_file(const std::string &file_name, const std::string &fmt)
{
    std::vector<std::string> parts = split_string(fmt, ';', true);
    if (parts.empty())
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Tape file format is not defined")) + "}");

    //Имя кассеты - тоже состояние устройства: окно показывает то, что лежит в
    //лентопротяжке, кто бы ее туда ни положил - кнопка или сценарий
    loaded_name = dsk_tools::get_filename(file_name);

    std::vector<std::string> first = split_string(parts[0], ':', true);
    if (first.size() < 2)
        return emulator::Result::error(emulator::ErrorCode::BadParameters,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Incorrect tape file format")) + "} " + fmt);

    std::string tape_format = first[0];

    //Носители с самоописательным контейнером читают файл сами, и второй
    //маркер строки формата у них не скорость: у zx это tap или tzx
    if (tape_format == "unior") return bytes_load_file(file_name);
    if (tape_format == "zx")    return zx_load_file(file_name, first[1] == "tzx");

    //The description comes from the ini file, where numbers are plain decimal
    //and the bytes of a header carry a prefix of their own
    int baud = parse_numeric_value(first[1], 10);

    std::vector<uint8_t> buffer;

    for (size_t i=1; i< parts.size(); i++ ) {
        if (parts[i] == "data") {
            long long fsize = dsk_tools::utf8_file_size(file_name);
            if (fsize > 0) {
                dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
                if (file.is_open()) {
                    size_t old_size = buffer.size();
                    buffer.resize(old_size + static_cast<size_t>(fsize));
                    file.read(reinterpret_cast<char*>(buffer.data() + old_size), fsize);
                    file.close();
                }
            }
        } else {
            std::vector<std::string> bytes = split_string(parts[i], ':', true);
            uint8_t b = parse_numeric_value(bytes[0], 10);
            unsigned int count = parse_numeric_value(bytes[1], 10);

            for (unsigned int j = 0; j < count; j++) {
                buffer.push_back(b);
            }
        }
    }

    unsigned int buf_size = buffer.size();

    std::vector<uint8_t> buffer_encoded;

    if (tape_format == "rk86") {
        buffer_encoded.reserve(buf_size*2);
        set_baud_rate(baud*2);
        for (unsigned int i=0; i < buf_size; i++) {
            PartsRecLE T;
            T.w = 0;
            uint8_t b = buffer[i];
            for (int j=0; j<8; j++)
                T.w += (b & (1 << j)) << j;
            T.w |= ~(T.w << 1) & 0xAAAA;
            buffer_encoded.push_back(T.b.H);
            buffer_encoded.push_back(T.b.L);
        }
        set_data(buffer_encoded);
    } else
    if (tape_format == "msx") {
        set_baud_rate(baud*4);
        buffer_encoded.resize(256 * 4, 0xAA);
        encode_msx(buffer, buffer_encoded);
        set_data(buffer_encoded);
    } else
    if (tape_format == "uknc") {
        // Every bit of the УК-НЦ signal is four short half periods long, so
        // the stream runs at four times the rate the format string names
        set_baud_rate(baud * 4);
        uknc_tape::encode(buffer, buffer_encoded);
        set_data(buffer_encoded);
    } else
    if (tape_format == "bk" || tape_format == "bk-ascii") {
        // For the БК the rate is given directly in units, one unit being the
        // half period of a synchronisation pulse
        set_baud_rate(baud);
        const std::string tape_name = dsk_tools::get_file_basename(file_name);
        if (tape_format == "bk-ascii")
            // A БЕЙСИК program in text form goes to the tape as a series of
            // numbered files, not as a single one
            bk_tape::encode_ascii(buffer, tape_name, buffer_encoded);
        else
            bk_tape::encode(buffer, tape_name, buffer_encoded);
        set_data(buffer_encoded);
    } else {
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unknown tape format!")) + "} " + tape_format);
    }

    notify_state();
    return emulator::Result::ok();
}

void TapeRecorder::clock(unsigned int counter)
{
    if (medium == TapeMedium::Bytes)  { bytes_clock(counter); return; }
    if (medium == TapeMedium::Pulses) { zx_clock(counter); return; }
    cycle_counter += counter;
    if (tape_mode != TAPE_STOPPED) {
        if (ticks_counter < ticks_per_bit) {
            ticks_counter += counter;
        } else {
            ticks_counter -= ticks_per_bit;
            if (data_position < data_size) {
                if (tape_mode == TAPE_READ) {
                    unsigned int v = (data[data_position] >> bit_shift) & 1;
                    i_output.change(v);
                    i_speaker.change(v);
                } else {
                    //TODO: write
                }
                if (--bit_shift < 0) {
                    bit_shift = 7;
                    data_position++;
                }
            } else {
                stop();
            }
        }
    }
    speaker->clock(counter);
}

int TapeRecorder::get_position()
{
    //Положение в секундах: единиц ленты, пройденных головкой, на тактов в
    //единице, на частоту. У ленты Спектрума единица - такт, у остальных -
    //битовый интервал, и формула от этого не меняется
    if (medium == TapeMedium::Pulses)
        return (m_system_clock > 0)?(int)(m_pos / m_system_clock):0;
    if (medium == TapeMedium::Bytes)
        return (baud_rate > 0)?(int)(m_pos / baud_rate):0;
    return (data_position * 8) / baud_rate;;
}

int TapeRecorder::get_total()
{
    if (medium == TapeMedium::Pulses)
        return (m_system_clock > 0)?(int)(m_total_units / m_system_clock):0;
    if (medium == TapeMedium::Bytes)
        return (baud_rate > 0)?(int)(m_total_units / baud_rate):0;
    return total_seconds;
}

int TapeRecorder::get_mode()
{
    if (medium != TapeMedium::Levels) return tape_mode_of(m_transport);
    return tape_mode;
}

//------------------- Introspection and control ----------------------------//

void TapeRecorder::save_state(StateWriter &w)
{
    ComputerDevice::save_state(w);

    w.n("mode", tape_mode);
    w.n("position", data_position);
    w.n("ticks_counter", ticks_counter);
    w.n("bit_shift", static_cast<uint32_t>(bit_shift));
    w.n("baud_rate", baud_rate);
    w.b("recording", is_recording);

    //The writer side: where an edge was last seen, and the bit being measured
    w.n64("cycle_counter", cycle_counter);
    w.n64("last_edge_cycles", last_edge_cycles);
    w.b("has_last_edge", has_last_edge);
    w.u("last_level", last_level, 8);
    w.n("writer_state", static_cast<uint32_t>(writer_state));
    w.n("measured_counter", measured_counter);
    w.n64("measured_time", measured_time);
    w.n64("measured_time_x2", measured_time_x2);
    w.n64("max_delta", max_delta);
    w.n("byte_counter", byte_counter);
    w.n("short_counter", short_counter);
    w.u("current_byte", current_byte, 8);

    //The tape itself, and what has been recorded onto it. A recording exists
    //nowhere but here until somebody saves it, so a path would lose it.
    //У ленты Спектрума в снимок едет исходный файл: развернутая волна заняла
    //бы мегабайты, а построить ее заново не стоит ничего
    if (!data.empty() && medium == TapeMedium::Levels)
        w.blob("data", name + ".tape", data.data(), data.size());
    if (!recorded_bytes.empty())
        w.blob("recorded", name + "-rec.tape", recorded_bytes.data(), recorded_bytes.size());

    if (medium != TapeMedium::Levels)
    {
        if (medium == TapeMedium::Bytes)
        {
            //Кассета едет в снимке целиком: записанное машиной есть только здесь
            std::vector<uint8_t> image;
            build_bt(image);
            if (!image.empty()) w.blob("tape", name + ".bt", image.data(), image.size());
        }
        else if (!m_source.empty())
            w.blob("zx", name + ".tap", m_source.data(), m_source.size());

        w.b("dirty", m_dirty);
        w.n("transport", (unsigned int)m_transport);
        w.n64("pos", m_pos);
        w.n64("ticks", m_ticks);
        w.b("half", m_half);
        w.n64("sound", m_sound_edges);
        w.u("write_byte", m_write_byte, 8);
        w.n("write_bits", m_write_bits);
        w.n64("writes", m_writes);
        w.n64("bytes_read", m_bytes_read);
    }
}

emulator::Result TapeRecorder::load_state(const StateReader &r)
{
    emulator::Result res = ComputerDevice::load_state(r);
    if (!res) return res;

    r.u("mode", tape_mode);
    r.u("position", data_position);
    r.u("ticks_counter", ticks_counter);
    r.u("bit_shift", bit_shift);
    uint32_t baud = baud_rate;
    if (r.u("baud_rate", baud)) set_baud_rate(baud);
    r.b("recording", is_recording);

    r.n64("cycle_counter", cycle_counter);
    r.n64("last_edge_cycles", last_edge_cycles);
    r.b("has_last_edge", has_last_edge);
    r.u("last_level", last_level);
    uint32_t state = static_cast<uint32_t>(writer_state);
    if (r.u("writer_state", state)) writer_state = static_cast<TapeWriterState>(state);
    r.u("measured_counter", measured_counter);
    r.n64("measured_time", measured_time);
    r.n64("measured_time_x2", measured_time_x2);
    r.n64("max_delta", max_delta);
    r.u("byte_counter", byte_counter);
    r.u("short_counter", short_counter);
    r.u("current_byte", current_byte);

    std::vector<uint8_t> tape;
    if (r.blob("data", tape)) { data.swap(tape); data_size = static_cast<unsigned int>(data.size()); }
    recorded_bytes.clear();
    r.blob("recorded", recorded_bytes);

    if (medium != TapeMedium::Levels)
    {
        std::vector<uint8_t> image;
        if (medium == TapeMedium::Bytes)
        {
            if (r.blob("tape", image) && !image.empty())
            {
                emulator::Result res2 = parse_bt(image.data(), image.size(), name);
                if (!res2) return res2;
                rebuild_timeline();
            }
        }
        else if (r.blob("zx", image) && !image.empty())
        {
            //Волна строится заново из тех же байтов - она и есть производная
            std::vector<TapeRecord> recs;
            std::string err;
            const bool tzx = image.size() > 7 && std::string((const char*)image.data(), 7) == "ZXTape!";
            const bool ok = tzx?zx_tape::parse_tzx(image.data(), image.size(), m_system_clock, recs, err)
                               :zx_tape::parse_tap(image.data(), image.size(), m_system_clock, recs, err);
            if (!ok)
                return emulator::Result::error(emulator::ErrorCode::FileError,
                    "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + err);
            m_source.swap(image);
            m_records.swap(recs);
            rebuild_timeline();
        }
        r.b("dirty", m_dirty);

        unsigned int tr = 0;
        if (r.u("transport", tr)) m_transport = (TapeTransport)tr;
        r.n64("pos", m_pos);
        r.n64("ticks", m_ticks);
        r.b("half", m_half);
        r.n64("sound", m_sound_edges);
        r.u("write_byte", m_write_byte);
        r.u("write_bits", m_write_bits);
        r.n64("writes", m_writes);
        r.n64("bytes_read", m_bytes_read);
        if (medium == TapeMedium::Pulses) zx_locate(m_pos); else locate(m_pos);
        m_carrier = false;
        i_ready.change(1);
    }
    return emulator::Result::ok();
}

std::vector<DeviceFieldInfo> TapeRecorder::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = ComputerDevice::get_device_fields();
    r.push_back({"mode",        "0 - stopped, 1 - playing",         false});
    r.push_back({"position",    "Current position, seconds",        false});
    r.push_back({"total",       "Total length, seconds",            false});
    r.push_back({"size",        "Size of the loaded data, bytes",   false});
    r.push_back({"baudrate",    "Current baud rate",                false});
    r.push_back({"recording",   "1 if recording is on",             false});
    r.push_back({"recorded",    "Size of the recorded data, bytes", false});
    //Лента - таймлайн записей у любого носителя, поэтому эти поля есть у всех
    //машин, а не только у той, ради которой они когда-то появились
    r.push_back({"transport", "Transport state: 0 stop, 1 play, 2 record, 3 back, 4 forward", false});
    r.push_back({"records",   "Records on the tape",                                          false});
    r.push_back({"record",    "Record the head is at, and the byte inside it",                false});
    r.push_back({"bytes",     "Bytes handed to the machine since reset",                      false});
    r.push_back({"writes",    "Records written by the machine",                               false});
    r.push_back({"sound",     "Edges the tape has sent to the speaker",                       false});
    r.push_back({"pulses",    "Half period lengths of the loaded waveform, in machine cycles", true});
    return r;
}

void TapeRecorder::get_save_data(std::vector<uint8_t> &out)
{
    if (medium == TapeMedium::Bytes)
    {
        build_bt(out);
        if (!out.empty()) m_dirty = false;
        return;
    }
    if (medium == TapeMedium::Pulses) { out.clear(); return; }
    const std::vector<uint8_t> * data = get_record_data();
    out = (data != nullptr)?*data:std::vector<uint8_t>();
}

std::vector<DeviceCommandInfo> TapeRecorder::get_device_commands()
{
    std::vector<DeviceCommandInfo> r = ComputerDevice::get_device_commands();
    r.push_back({"load",    "\"file\" [, \"format\"]",  "Loads a tape image, the format defaults to the [TapeFiles] ini entry"});
    r.push_back({"play",    "",                         "Starts playback"});
    r.push_back({"stop",    "",                         "Stops playback"});
    r.push_back({"rewind",  "",                         "Rewinds to the beginning"});
    r.push_back({"record",  "[0|1]",                    "Switches recording on or off"});
    r.push_back({"save",    "[\"file\"]",                 "Writes the recorded data out, by default under the name the machine used"});
    r.push_back({"seek",    "<record>",                 "Moves the head to a record by its number"});
    return r;
}

bool TapeRecorder::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    out.numeric = true;
    if (field == "mode")        { out.values.push_back(get_mode());                 return true; }
    if (field == "recording")   { out.values.push_back(is_recording?1:0);           return true; }

    if (field == "transport") { out.width = 8;  out.values.push_back((unsigned int)m_transport); return true; }
    if (field == "records")   { out.width = 16; out.values.push_back((unsigned int)m_records.size()); return true; }
    if (field == "record")
    {
        out.width = 16;
        out.values.push_back((unsigned int)m_rec);
        out.values.push_back((unsigned int)m_byte);
        return true;
    }
    if (field == "bytes")     { out.width = 32; out.values.push_back((unsigned int)m_bytes_read); return true; }
    if (field == "writes")    { out.width = 32; out.values.push_back((unsigned int)m_writes); return true; }
    if (field == "sound")     { out.width = 32; out.values.push_back((unsigned int)m_sound_edges); return true; }
    //Волна с начала ленты, а не от головки: сценарию нужна проверка того, что
    //лента построена верно, и она не должна зависеть от того, когда ее сняли
    if (field == "pulses")
    {
        out.width = 16;
        if (medium != TapeMedium::Pulses || m_records.empty()) return true;
        zx_tape::Pulser pp;
        pp.start(m_records[0].data.data(), m_records[0].data.size());
        for (unsigned int i = 0; i <= to; i++)
        {
            const unsigned int v = pp.next();
            if (v == 0) break;
            if (i >= from) out.values.push_back(v);
        }
        return true;
    }

    //Counters and sizes are not byte sized, LOGDEFS must not truncate them
    out.width = 32;
    if (field == "position")    { out.values.push_back(get_position());             return true; }
    if (field == "total")       { out.values.push_back(get_total());                return true; }
    if (field == "size")        { out.values.push_back(data_size);                  return true; }
    if (field == "baudrate")    { out.values.push_back(baud_rate);                  return true; }
    if (field == "recorded")    { out.values.push_back(get_record_size());          return true; }
    out.width = 0;

    out.numeric = false;
    return ComputerDevice::get_field(field, from, to, out);
}

emulator::Result TapeRecorder::send_command(const std::string &command, const std::string &parameters)
{
    std::vector<std::string> p = split_params(parameters);

    if (command == "seek")
    {
        if (p.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Command 'seek' expects a record number")) + "}");
        const unsigned int n = parse_numeric_value(p[0]);
        if (n >= m_records.size())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "No such record on the tape")) + "} " + p[0]);
        set_transport(T_STOP);
        m_pos = record_start(n);
        m_ticks = 0;
        if (medium == TapeMedium::Pulses) zx_locate(m_pos); else locate(m_pos);
        notify_state();
        return emulator::Result::ok();
    }

    if (command == "load")
    {
        if (p.empty() || p[0].empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Command 'load' expects a file name")) + "}");

        std::string file = find_file_location(sd, p[0]);
        if (file.empty()) file = p[0];

        //The format may be given explicitly, otherwise it is taken from the ini
        //by the file extension, exactly as the tape recorder window does it
        std::string fmt = (p.size() > 1)?p[1]:std::string("");
        if (fmt.empty())
        {
            std::string ext = str_tolower(dsk_tools::get_file_ext(file));
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
            fmt = format_for_extension(ext);
        }
        if (fmt.empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unknown tape file format for")) + " " + p[0] + "}");

        return load_file(file, fmt);
    }

    if (command == "play")   { play();   return emulator::Result::ok(); }
    if (command == "stop")   { stop();   return emulator::Result::ok(); }
    if (command == "rewind") { rewind(); return emulator::Result::ok(); }

    if (command == "record") {
        bool on = p.empty() || p[0].empty() || parse_numeric_value(p[0]) != 0;
        set_recording(on);
        return emulator::Result::ok();
    }

    if (command == "save") {
        //Writes out what has been recorded, so that a script can check that a
        //tape written by the machine reads back into it
        std::vector<uint8_t> image;
        get_save_data(image);
        const std::vector<uint8_t> * out = &image;
        if (out->empty())
            return emulator::Result::error(emulator::ErrorCode::BadParameters,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Nothing has been recorded")) + "}");

        std::string file = (p.empty() || p[0].empty())?get_record_name():p[0];
        if (file.empty()) file = "tape.bin";
        file = resolve_output_path(sd, file);

        dsk_tools::UTF8_ofstream f(file, std::ios::binary);
        if (!f.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to save file!")) + "} " + file);
        f.write(reinterpret_cast<const char*>(out->data()), (std::streamsize)out->size());
        f.close();
        return emulator::Result::ok();
    }

    return ComputerDevice::send_command(command, parameters);
}

// Лента - один поток: пауза, преамбула с данными, снова пауза. Здесь только
// пересчитывается общая длина, положение головки не трогается
void TapeRecorder::rebuild_timeline()
{
    m_total_units = 0;
    for (size_t i = 0; i < m_records.size(); i++)
        m_total_units += m_records[i].gap + m_records[i].units;

    //После последней записи на кассете остается ракорд. Без него лента
    //кончалась бы ровно на последнем байте, а машина этот байт читает уже
    //после того, как он прошел головку: контрольная сумма блока, записанного
    //в самом конце ленты, терялась бы всегда
    if (medium == TapeMedium::Bytes) m_total_units += (uint64_t)baud_rate * 2;
    //У ленты Спектрума ракорд тоже нужен, только в тактах: последний блок
    //кончается ровно на конце ленты, а ПЗУ дочитывает его уже после того, как
    //последний перепад прошел головку. Без секунды тишины самый большой блок
    //игры не доходит до конца - ПЗУ отвечает "R Tape loading error"
    else if (medium == TapeMedium::Pulses) m_total_units += m_system_clock;

    //У чистой кассеты длина своя: она не кончается там, где кончилось
    //записанное, иначе разметка уперлась бы в конец ленты на первом же секторе
    if (m_blank && medium == TapeMedium::Bytes && m_total_units < m_blank_units)
        m_total_units = m_blank_units;
}

uint64_t TapeRecorder::record_start(size_t index) const
{
    uint64_t p = 0;
    for (size_t i = 0; i < index && i < m_records.size(); i++)
        p += m_records[i].gap + m_records[i].units;
    if (index < m_records.size()) p += m_records[index].gap;
    return p;
}

// Где стоит головка после перемотки: в паузе или внутри записи
void TapeRecorder::locate(uint64_t pos)
{
    uint64_t p = 0;
    for (size_t i = 0; i < m_records.size(); i++)
    {
        const uint64_t gap_end = p + m_records[i].gap;
        const uint64_t rec_end = gap_end + m_records[i].units;
        if (pos < gap_end) { m_rec = i; m_in_gap = true;  m_byte = 0; return; }
        if (pos < rec_end) { m_rec = i; m_in_gap = false; m_byte = (size_t)((pos - gap_end) / 8); return; }
        p = rec_end;
    }
    m_rec = m_records.size();
    m_in_gap = true;
    m_byte = 0;
}

void TapeRecorder::set_transport(TapeTransport t)
{
    if (m_transport == t) return;

    if (m_transport == T_RECORD && t != T_RECORD) commit_write();

    m_transport = t;
    m_ticks = 0;
    //Перемотка двигает только положение головки: какая запись под ней, никто
    //при этом не считает. Без пересчета лента после отката назад играла бы
    //в пустоту до той записи, на которой ее остановили в прошлый раз - машина
    //ждала бы синхросимвола, которого уже не будет
    locate(m_pos);
    if (t == T_RECORD)
    {
        m_write_buf.clear();
        m_write_start = m_pos;
    }

    //Несущая снимается сразу: в паузе и на стоящей ленте сигнала нет
    if (t != T_RECORD) set_carrier(false);
    else set_carrier(true);

    //Лента стоит - в динамике тишина, а не последний уровень
    m_bit_out = -1;
    m_write_bits = 0;
    if (t == T_STOP) sound_level(0);

    //Окно магнитофона живет на тех же полях, что и у обычной лентопротяжки:
    //признак движения плюс признак записи
    //motor_on говорит окну «лентой командует машина»: закрываясь, оно такую
    //ленту не останавливает. Это верно для накопителя, но не для ленты,
    //которую пустил человек, - там движение показывает линия ~motor
    if (i_control.linked != 0)
    {
        is_recording = (t == T_RECORD);
        motor_on = (t != T_STOP);
    }
    set_tape_mode(tape_mode_of(t));
    notify_state();
}

// Чем занята лентопротяжка - в тех понятиях, которыми живет окно магнитофона
unsigned int TapeRecorder::tape_mode_of(TapeTransport t)
{
    switch (t)
    {
        case T_FORWARD: return TAPE_FORWARD;
        case T_BACK:    return TAPE_BACK;
        case T_STOP:    return TAPE_STOPPED;
        default:        return TAPE_READ;
    }
}

// DSR у ВВ51 - это «есть сигнал с ленты», а не «лентопротяжка крутится». На
// этом держится все чтение: и ПЗУ (FC04 выходит с переносом, когда сигнала
// нет), и загрузчик TCP/M, который читает запись до пропадания несущей, а не
// по счетчику. С DSR по движению ленты загрузчик читал бы кассету насквозь
void TapeRecorder::set_carrier(bool on)
{
    if (m_carrier == on) return;
    m_carrier = on;
    i_ready.change(on?0:1);
}

void TapeRecorder::advance_unit()
{
    if (m_transport == T_BACK) {
        if (m_pos > 0) m_pos--;
        return;
    }

    if (m_pos < m_total_units) m_pos++;

    if (m_transport != T_PLAY) return;

    //Байт отдается приемнику на границе байта внутри записи. Преамбулу ВВ51
    //отбросит сам: он ищет синхросимвол
    if (m_rec >= m_records.size()) { set_carrier(false); return; }
    const TapeRecord &r = m_records[m_rec];
    const uint64_t gap_end = record_start(m_rec);
    set_carrier(m_pos > gap_end && m_pos <= gap_end + r.units);
    if (m_pos <= gap_end) return;

    const uint64_t in_rec = m_pos - gap_end;
    if (in_rec % 8 != 0) return;
    const size_t index = (size_t)(in_rec / 8) - 1;
    if (index < r.data.size())
    {
        i_data_out.change(r.data[index]);
        m_bytes_read++;
        m_byte = index;
        m_in_gap = false;
    }
    if (in_rec >= r.units)
    {
        m_rec++;
        m_in_gap = true;
        m_byte = 0;
    }
}

// Уровень в динамике. Перепады считаются: сценарию нечем услышать ленту, а
// счетчик перепадов - ровно то, что отличает звучащую ленту от молчащей
void TapeRecorder::sound_level(unsigned int level)
{
    if (level == m_sound_level) return;
    m_sound_level = level;
    m_sound_edges++;
    tape_sound(level);
}

// Бит под головкой - для динамика. Данные машине отдаются байтами, но лента
// записана битами, и слышно должно быть их
int TapeRecorder::current_bit() const
{
    if (m_transport != T_PLAY || !m_carrier) return -1;
    if (m_rec >= m_records.size()) return -1;
    const TapeRecord &r = m_records[m_rec];
    const uint64_t gap_end = record_start(m_rec);
    if (m_pos <= gap_end) return -1;
    const uint64_t in_rec = m_pos - gap_end;
    const size_t index = (size_t)((in_rec - 1) / 8);
    if (index >= r.data.size()) return -1;
    return (r.data[index] >> ((in_rec - 1) % 8)) & 1;
}

// Бит, который передатчик ВВ51 сейчас кладет на ленту. Запись слышна так же,
// как чтение: байт разбирается младшим разрядом вперед, как его и шлет ВВ51
int TapeRecorder::next_write_bit()
{
    if (m_write_bits == 0) return -1;
    const int bit = (m_write_byte >> (8 - m_write_bits)) & 1;
    m_write_bits--;
    return bit;
}

// Записанное ложится на ленту одной записью. Головка стирает то, поверх чего
// пишет, поэтому заменяются записи, начало которых она прошла уже в записи.
// Та, что началась раньше, остается: это маркер, который машина только что
// прочитала, а писать начала после него - без маркера блок стал бы не найти.
void TapeRecorder::commit_write()
{
    if (m_write_buf.size() < 5) { m_write_buf.clear(); return; }

    std::vector<uint64_t> starts(m_records.size(), 0);
    {
        uint64_t p = 0;
        for (size_t i = 0; i < m_records.size(); i++)
        {
            p += m_records[i].gap;
            starts[i] = p;
            p += m_records[i].units;
        }
    }

    const uint64_t ws = m_write_start;
    const uint64_t we = ws + (uint64_t)m_write_buf.size() * 8;
    //Запас на неточность модели лентопротяжки: машина метит в паузу перед
    //блоком, а где именно она окажется, зависит от того, когда ее остановили
    const uint64_t slack = (baud_rate > 16)?(baud_rate / 16):1;

    size_t first = m_records.size();
    size_t last = m_records.size();
    for (size_t i = 0; i < m_records.size(); i++)
    {
        if (starts[i] + slack < ws) continue;
        if (starts[i] > we) break;
        if (first == m_records.size()) first = i;
        last = i + 1;
    }

    if (first == m_records.size())
    {
        //Голова не задела ни одной записи: новая встает между соседями
        first = 0;
        while (first < m_records.size() && starts[first] <= ws) first++;
        last = first;
    }

    const uint64_t prev_end = (first == 0)?0
        :(starts[first - 1] + m_records[first - 1].units);
    const bool has_next = last < m_records.size();
    const uint64_t next_start = has_next?starts[last]:0;

    TapeRecord r;
    r.gap = (uint32_t)((ws > prev_end)?(ws - prev_end):0);
    r.data = m_write_buf;
    r.units = (uint64_t)r.data.size() * 8;

    m_records.erase(m_records.begin() + (long)first, m_records.begin() + (long)last);
    m_records.insert(m_records.begin() + (long)first, r);

    if (has_next)
    {
        const uint64_t new_end = ws + m_records[first].units;
        m_records[first + 1].gap = (uint32_t)((next_start > new_end)?(next_start - new_end):(baud_rate / 4));
        //Пауза следующей записи пересчитана, значит ее числа из файла больше
        //не описывают ленту
        m_records[first + 1].from_file = false;
    }

    m_writes++;
    m_dirty = true;
    m_write_buf.clear();
    rebuild_timeline();
    locate(m_pos);
}

void TapeRecorder::bytes_clock(unsigned int counter)
{
    if (m_transport != T_STOP && ticks_per_bit > 0)
    {
        //Перемотка идет быстрее воспроизведения во столько же раз, во сколько
        //у настоящего магнитофона
        const unsigned int speed = (m_transport == T_BACK || m_transport == T_FORWARD)?m_fast_speed:1;
        //Шаг - половина битового интервала: сами данные приходят байтами, но
        //в динамик уходит уровень, который меняется в середине каждого бита.
        //Это манчестер, которым лента и записана, и звучит он как у остальных
        //машин: в паузах тихо, на записи - характерный треск
        m_ticks += (uint64_t)counter * speed * 2;
        while (m_ticks >= ticks_per_bit)
        {
            m_ticks -= ticks_per_bit;
            m_half = !m_half;
            if (!m_half)
            {
                advance_unit();
                m_bit_out = (m_transport == T_RECORD)?next_write_bit():current_bit();
            }
            sound_level((m_bit_out < 0)?0:((unsigned int)m_bit_out ^ (m_half?1u:0u)));
        }
        //Машина перестала слать байты, а лента идет - на ней пошла пауза, и
        //записанное до нее кончилось. Так пишет COPY: лентопротяжку в запись
        //ставит человек, а программа выкладывает блоки один за другим с
        //паузами между ними. Без этого вся кассета стала бы одной записью
        if (m_transport == T_RECORD && !m_write_buf.empty()
            && m_pos > m_write_last + SILENCE_BITS) commit_write();

        if ((m_transport == T_PLAY || m_transport == T_FORWARD) && m_pos >= m_total_units) set_transport(T_STOP);
        if (m_transport == T_BACK && m_pos == 0) set_transport(T_STOP);
    }
    clock_sound(counter);
}

void TapeRecorder::reset(bool cold)
{
    ComputerDevice::reset(cold);
    if (medium == TapeMedium::Levels) return;
    m_transport = T_STOP;
    m_ticks = 0;
    m_write_buf.clear();
    m_carrier = false;
    i_ready.change(1);
    if (medium == TapeMedium::Pulses) zx_locate(m_pos); else locate(m_pos);
}

// Образ кассеты .bt. Разбор и сборка живут в tape_bt.h, здесь остаются только
// сообщения: переводы привязаны к устройству, а не к контейнеру
emulator::Result TapeRecorder::parse_bt(const uint8_t * raw, size_t size, const std::string &where)
{
    std::vector<TapeRecord> recs;
    std::string err;
    if (!bt_tape::parse(raw, size, baud_rate, recs, m_unit_per_bit, m_blank, err))
    {
        if (err == "preamble")
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Not a Unior tape image")) + "} " + where);
        if (err == "empty")
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Tape image holds no records")) + "} " + where);
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + where);
    }
    m_records.swap(recs);
    rebuild_timeline();
    return emulator::Result::ok();
}

void TapeRecorder::build_bt(std::vector<uint8_t> &out) const
{
    bt_tape::build(m_records, m_unit_per_bit, baud_rate, out);
}

emulator::Result TapeRecorder::bytes_load_file(const std::string &file_name)
{
    //Файл из одного заголовка - чистая кассета, ее и размечают с нуля
    const long long fsize = dsk_tools::utf8_file_size(file_name);
    if (fsize < 4)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + file_name);

    std::vector<uint8_t> image((size_t)fsize);
    {
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (!file.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + file_name);
        file.read(reinterpret_cast<char*>(image.data()), fsize);
        file.close();
    }

    medium = TapeMedium::Bytes;
    m_tape_enc = TapeEnc::UNIOR;
    emulator::Result res = parse_bt(image.data(), image.size(), file_name);
    if (!res) return res;

    m_dirty = false;
    m_pos = 0;
    m_ticks = 0;
    m_write_buf.clear();
    locate(0);
    set_transport(T_STOP);
    loaded_name = dsk_tools::get_filename(file_name);
    notify_state();

    return emulator::Result::ok();
}

//--------------------------- Лента ZX Spectrum -----------------------------//

emulator::Result TapeRecorder::zx_load_file(const std::string &file_name, bool tzx)
{
    const long long fsize = dsk_tools::utf8_file_size(file_name);
    if (fsize < 2)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + file_name);

    std::vector<uint8_t> image((size_t)fsize);
    {
        dsk_tools::UTF8_ifstream file(file_name, std::ios::binary);
        if (!file.is_open())
            return emulator::Result::error(emulator::ErrorCode::FileError,
                "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + file_name);
        file.read(reinterpret_cast<char*>(image.data()), fsize);
        file.close();
    }

    std::vector<TapeRecord> recs;
    std::string err;
    const bool ok = tzx?zx_tape::parse_tzx(image.data(), image.size(), m_system_clock, recs, err)
                       :zx_tape::parse_tap(image.data(), image.size(), m_system_clock, recs, err);
    if (!ok)
        return emulator::Result::error(emulator::ErrorCode::FileError,
            "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Unable to read tape image")) + "} " + err);

    medium = TapeMedium::Pulses;
    m_tape_enc = TapeEnc::ZX;
    //Единица этой ленты - такт процессора: длительности у Спектрума разные, и
    //общей сетки, на которую легли бы все, нет
    ticks_per_bit = 1;
    m_source.swap(image);
    m_records.swap(recs);
    m_blank = false;
    m_dirty = false;
    rebuild_timeline();
    m_pos = 0;
    m_ticks = 0;
    zx_locate(0);
    set_transport(T_STOP);
    loaded_name = dsk_tools::get_filename(file_name);
    notify_state();

    return emulator::Result::ok();
}

// Где стоит головка и какой полупериод сейчас на линии. Внутри записи автомат
// проигрывается с ее начала: перемотка - дело редкое, а держать состояние на
// каждый импульс незачем
void TapeRecorder::zx_locate(uint64_t pos)
{
    m_zx_level = 0;
    uint64_t p = 0;
    for (size_t i = 0; i < m_records.size(); i++)
    {
        const uint64_t gap_end = p + m_records[i].gap;
        const uint64_t rec_end = gap_end + m_records[i].units;
        if (pos < gap_end)
        {
            //Пауза - это ровный уровень: у линии нет третьего состояния, и
            //первый перепад пилот-тона приходится на ее конец
            m_rec = i; m_in_gap = true; m_byte = 0;
            m_zx_pulse = (unsigned int)(gap_end - pos);
            return;
        }
        if (pos < rec_end)
        {
            m_rec = i; m_in_gap = false; m_byte = 0;
            m_zx.start(m_records[i].data.data(), m_records[i].data.size());
            uint64_t q = gap_end;
            unsigned int v = m_zx.next();
            while (v != 0 && q + v <= pos) { q += v; m_zx_level ^= 1; v = m_zx.next(); }
            m_zx_pulse = (v != 0)?(unsigned int)(q + v - pos):0;
            return;
        }
        p = rec_end;
    }
    m_rec = m_records.size(); m_in_gap = true; m_byte = 0; m_zx_pulse = 0;
}

// Длительность следующего полупериода. Ноль - лента кончилась
unsigned int TapeRecorder::zx_next_pulse()
{
    while (m_rec < m_records.size())
    {
        if (m_in_gap)
        {
            m_in_gap = false;
            m_zx.start(m_records[m_rec].data.data(), m_records[m_rec].data.size());
        }
        const unsigned int v = m_zx.next();
        if (v != 0) return v;
        m_rec++;
        if (m_rec < m_records.size())
        {
            m_in_gap = true;
            return (unsigned int)m_records[m_rec].gap;
        }
    }
    return 0;
}

void TapeRecorder::zx_clock(unsigned int counter)
{
    if (m_transport != T_STOP)
    {
        const unsigned int speed = (m_transport == T_BACK || m_transport == T_FORWARD)?m_fast_speed:1;
        const uint64_t step = (uint64_t)counter * speed;
        if (m_transport == T_BACK)
        {
            m_pos = (m_pos > step)?(m_pos - step):0;
            zx_locate(m_pos);
            if (m_pos == 0) set_transport(T_STOP);
        }
        else
        {
            m_pos += step;
            if (m_pos >= m_total_units) { m_pos = m_total_units; set_transport(T_STOP); }
            else if (m_transport == T_PLAY)
            {
                //Полупериоды здесь от 667 тактов и длиннее, а слайс - десяток,
                //так что за вызов кончается самое большее один. Цикл стоит на
                //случай длинной паузы, которую машина проспала целиком
                m_ticks += step;
                while (m_zx_pulse != 0 && m_ticks >= m_zx_pulse)
                {
                    m_ticks -= m_zx_pulse;
                    m_zx_level ^= 1;
                    i_output.change(m_zx_level);
                    sound_level(m_zx_level);
                    m_zx_pulse = zx_next_pulse();
                }
            }
            else zx_locate(m_pos);
        }
    }
    clock_sound(counter);
}


ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
