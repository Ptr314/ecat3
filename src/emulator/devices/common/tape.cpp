// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Tape recorder device

#include "emulator/utils.h"
#include "emulator/devices/cpu/cpu_utils.h"
#include "tape.h"
#include "dsk_tools/dsk_tools.h"

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
{
    device_class = "tape";

    system_clock = (dynamic_cast<CPU*>(im->dm->get_device_by_name("cpu")))->clock;

    EmulatorConfigDevice * speaker_config = new EmulatorConfigDevice(name + "-speaker", "speaker");
    speaker_config->add_parameter("~input", "", name + ".speaker", "", "");

    speaker = new Speaker(im, speaker_config);
}

TapeRecorder::~TapeRecorder()
{
    delete speaker;
}

dsk_tools::Result TapeRecorder::load_config(SystemData *sd)
{
    dsk_tools::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    baud_rate = read_confg_value(cd, "baudrate", false, (unsigned int)1200);

    const std::string enc_str = str_tolower(cd->get_parameter("ecoding", false).value);
    if (enc_str.empty() || enc_str == "msx")
        m_tape_enc = TapeEnc::MSX;
    else if (enc_str == "rk86")
        m_tape_enc = TapeEnc::RK86;
    else
        return dsk_tools::Result::error(dsk_tools::ErrorCode::ConfigError, "{TapeRecorder|" + std::string(QT_TRANSLATE_NOOP("TapeRecorder", "Incorrect encoding")) + "} " + enc_str);

    files = cd->get_parameter("files", false).value;

    if (files.empty()) files = sd->allowed_files;

    res = speaker->load_config(sd);
    if (!res) return res;

    speaker->reset(true);

    return dsk_tools::Result::ok();
}

void TapeRecorder::interface_callback(unsigned callback_id, unsigned new_value, MAYBE_UNUSED unsigned old_value)
{
    if (callback_id == 1) {
        // Input changed
        if (is_recording) {
            if ((old_value & 1) != 0 && (new_value & 1) == 0) {
                if (has_last_edge) {
                    const uint64_t delta_cycles = cycle_counter - last_edge_cycles;
                    // const unsigned delta_us = static_cast<unsigned int>(delta_cycles * 1000000 / system_clock);
                    write_edge(delta_cycles);
                }
                last_edge_cycles = cycle_counter;
                has_last_edge = true;
            }
        }
    } else {
        // Motor changed
        // std::cout << "TapeRecorder: motor = " << (new_value & 1) << std::endl;
        if ((new_value & 1)==1 && (old_value & 1)==0 && !is_recording && data_size>0) {
            emit mode_changed(TAPE_READ);
        }
        if ((new_value & 1)==0 && (old_value & 1)==1 && tape_mode == TAPE_READ) {
            emit mode_changed(TAPE_STOPPED);
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
    is_recording = recording;
    if (is_recording && m_tape_enc == TapeEnc::MSX) {
        has_last_edge = false;
        writer_state = TapeWriterState::Measuring;
        measured_counter = 0;
        measured_time = 0;
        recorded_bytes.clear();
        recorded_bytes.reserve(1024);
    }
}

unsigned TapeRecorder::get_record_size()
{
    return recorded_bytes.size();
}

std::vector<uint8_t> * TapeRecorder::get_record_data()
{
    return &recorded_bytes;
}

void TapeRecorder::play()
{
    if (data_size > 0) {
        tape_mode = TAPE_READ;
    }
}

void TapeRecorder::stop()
{
    tape_mode = TAPE_STOPPED;
}

void TapeRecorder::rewind()
{
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
    ticks_per_bit = system_clock / baud_rate;
}

void TapeRecorder::set_data(const std::vector<uint8_t> &new_data){
    data = new_data;
    tape_mode = TAPE_STOPPED;
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

void TapeRecorder::load_file(const std::string &file_name, const std::string &fmt)
{
    std::vector<std::string> parts = split_string(fmt, ';', true);
    std::vector<std::string> first = split_string(parts[0], ':', true);

    std::string tape_format = first[0];
    int baud = parse_numeric_value(first[1]);

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
            uint8_t b = parse_numeric_value(bytes[0]);
            unsigned int count = parse_numeric_value(bytes[1]);

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
    } else {
        QMessageBox::warning(0, TapeRecorder::tr("Error"), TapeRecorder::tr("Unknown tape format!"));
    }

}

void TapeRecorder::clock(unsigned int counter)
{
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
    return (data_position * 8) / baud_rate;;
}

int TapeRecorder::get_total()
{
    return total_seconds;
}

int TapeRecorder::get_mode()
{
    return tape_mode;
}

ComputerDevice * create_tape_recorder(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new TapeRecorder(im, cd);
}
