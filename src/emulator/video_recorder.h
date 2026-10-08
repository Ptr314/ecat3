// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Video recording through an external ffmpeg, header
//
// Frames are timed by emulated time, not by the wall clock: frame N of the
// video is the picture shown at start + N / fps of clock_counter. Whoever has
// a picture hands it over with set_frame(), the render thread calls advance()
// once a frame, and every frame that has come due since is written as a copy
// of the last picture. A host that stalls therefore makes no gaps, and a
// picture that does not change costs nothing: the queue holds a picture and
// the number of times it is to be written.
//
// A display that knows its own frame rate (the БК, 3125/64) can instead lock
// the video to the machine: frame_locked, and push_frame() once per frame of
// the machine at the end of its picture, so frame N is frame N of the machine,
// never two halves of neighbouring ones, and the clock adds nothing.
//
// The sound comes the same way, as samples counted in emulated time, and goes
// into a temporary WAV; stop() writes what is left and a second run of ffmpeg
// puts the two together without encoding the picture again.
//
// Nothing of Qt: the frames of "as in the machine" are made on the render
// thread of the core.

#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <atomic>
#include <cstdint>
#include <cstdio>

#include "thread_compat.h"

enum VideoCodec {
    VIDEO_CODEC_ZMBV = 0,
    VIDEO_CODEC_H264,
    VIDEO_CODEC_H265,
    VIDEO_CODEC_PRORES
};

struct VideoSettings {
    std::string ffmpeg;         // The executable
    std::string file;           // The final file, with the extension of the codec
    std::string log_file;       // Where ffmpeg writes what it has to say
    int codec = VIDEO_CODEC_H264;
    uint64_t fps_num = 50;      // Frames a second as a fraction: 3125/64 for the БК
    uint64_t fps_den = 1;
    bool frame_locked = false;  // One frame per push_frame(), none from the clock
    bool native = false;        // Asked for the rate of the machine; Emulator resolves it
    bool upscale = false;       // Small pictures are enlarged for codecs with 4:2:0 colour
    double sar = 1.0;           // Pixel aspect of the picture, 1 for square pixels
};

class VideoRecorder
{
public:
    enum State { IDLE, RECORDING, FINISHING, DONE, FAILED };

    VideoRecorder();
    ~VideoRecorder();

    //The extension of the container a codec is written into, with the dot
    static const char * extension(int codec);
    //"zmbv", "h264", ... and back; unknown names give H.264
    static const char * codec_name(int codec);
    static int codec_by_name(const std::string &name);

    //Prepares a recording; ffmpeg itself starts with the first picture, when
    //the size is known. The clock is the master processor's frequency
    bool start(const VideoSettings &s, uint64_t clock_freq, std::string &error);
    //The moment frame 0 belongs to. Until it is set nothing is written
    void set_start(uint64_t clock);
    //Any thread. The picture is RGBA, row after row (from the bottom one if
    //bottom_up, as OpenGL reads them); a picture of another size than the
    //first one is scaled to it
    void set_frame(const uint8_t * rgba, unsigned int w, unsigned int h, bool bottom_up = false);
    //frame_locked: the picture of one whole frame of the machine, written
    //once. The emulation thread calls it and never waits: when ffmpeg falls
    //far behind, the last picture is repeated instead, which keeps the count
    void push_frame(const uint8_t * rgba, unsigned int w, unsigned int h);
    //The render thread: writes the frames due by this moment. May wait when
    //ffmpeg falls behind, so that no frame is lost
    void advance(uint64_t clock);
    //16-bit mono samples, in emulated time from the start
    void add_audio(const int16_t * samples, size_t count, unsigned int rate);
    //Writes the frames up to this moment and finishes the file in the
    //background; state() tells when it is ready
    void stop(uint64_t clock);

    State state() const { return static_cast<State>(m_state.load()); }
    bool recording() const { return m_state.load() == RECORDING; }
    //The file written, or why it was not
    std::string message();
    uint64_t frames() const { return m_frames_written.load(); }

private:
    struct Frame {
        std::shared_ptr<std::vector<uint8_t> > pixels;
        uint64_t count;
    };

    struct Process {
#ifdef _WIN32
        void * process = nullptr;
        void * input = nullptr;
#else
        int pid = -1;
        int input = -1;
#endif
    };

    VideoSettings m_settings;
    uint64_t m_clock_freq = 0;
    std::atomic<int> m_state;

    compat_mutex m_mutex;                       // Everything below it
    bool m_started = false;                     // set_start() has been called
    uint64_t m_start = 0;
    uint64_t m_due = 0;                         // Frames handed to the queue
    unsigned int m_width = 0, m_height = 0;     // Fixed by the first picture
    std::shared_ptr<std::vector<uint8_t> > m_last;
    std::deque<Frame> m_queue;
    size_t m_queued_bytes = 0;
    bool m_stopping = false;
    std::string m_message;
    std::string m_video_file;                   // The picture alone, until the audio is added
    std::string m_audio_file;
    FILE * m_audio = nullptr;
    unsigned int m_audio_rate = 0;
    uint64_t m_audio_samples = 0;
    Process m_process;
    bool m_process_started = false;

    std::atomic<uint64_t> m_frames_written;

#if USE_QT_THREADING
    EmuThread * m_thread = nullptr;
#else
    std::thread m_thread;
#endif
    void join();
    void worker();
    void finish();
    void fail(const std::string &why);
    bool launch(std::string &error);
    void queue_due(uint64_t clock);
    void queue_last(uint64_t count);
    std::shared_ptr<std::vector<uint8_t> > make_picture(const uint8_t * rgba, unsigned int w, unsigned int h, bool bottom_up);
    std::string log_tail();

    static bool spawn(const std::vector<std::string> &args, const std::string &log, bool with_input, Process &p, std::string &error);
    static bool write_input(Process &p, const uint8_t * data, size_t size);
    static void close_input(Process &p);
    static int wait_exit(Process &p);
};
