// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Video recording through an external ffmpeg, source

#include "video_recorder.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include "libs/dsk_tools/src/utils.h"
#elif !defined(__EMSCRIPTEN__)
    #include <csignal>
    #include <cerrno>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/wait.h>
#endif

namespace {

//Pictures waiting for ffmpeg. Copies of one picture take no room, so this is
//reached only when ffmpeg encodes slower than the pictures change
const size_t QUEUE_LIMIT = 256u * 1024 * 1024;

FILE * open_file(const std::string &path, const char * mode)
{
#ifdef _WIN32
    return _wfopen(dsk_tools::utf8_to_wide(path).c_str(), dsk_tools::utf8_to_wide(mode).c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

void delete_file(const std::string &path)
{
#ifdef _WIN32
    DeleteFileW(dsk_tools::utf8_to_wide(path).c_str());
#else
    remove(path.c_str());
#endif
}

bool move_file(const std::string &from, const std::string &to)
{
#ifdef _WIN32
    return MoveFileExW(dsk_tools::utf8_to_wide(from).c_str(), dsk_tools::utf8_to_wide(to).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

//"dir/name.mp4" -> "dir/name" + suffix
std::string with_suffix(const std::string &file, const std::string &suffix)
{
    const size_t slash = file.find_last_of("/\\");
    const size_t dot = file.find_last_of('.');
    const std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                             ? file.substr(0, dot) : file;
    return stem + suffix;
}

void put32(FILE * f, uint32_t v)
{
    const unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
    fwrite(b, 1, 4, f);
}

bool little_endian()
{
    const uint16_t one = 1;
    return *reinterpret_cast<const unsigned char*>(&one) == 1;
}

void put16(FILE * f, uint16_t v)
{
    const unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
    fwrite(b, 1, 2, f);
}

//16-bit mono PCM; the sizes are written again when the file is closed
void write_wav_header(FILE * f, unsigned int rate, uint32_t data_bytes)
{
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data_bytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
    put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data_bytes);
}

#ifdef _WIN32
//The quoting CommandLineToArgvW and the C runtime undo
std::wstring quote_argument(const std::wstring &a)
{
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    size_t slashes = 0;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] == L'\\') { slashes++; continue; }
        if (a[i] == L'"') r.append(slashes * 2 + 1, L'\\');
        else r.append(slashes, L'\\');
        slashes = 0;
        r += a[i];
    }
    r.append(slashes * 2, L'\\');
    r += L'"';
    return r;
}
#endif

} // namespace

VideoRecorder::VideoRecorder() : m_state(IDLE), m_frames_written(0)
{
}

VideoRecorder::~VideoRecorder()
{
    {
        compat_lock_guard lock(m_mutex);
        if (m_state.load() == RECORDING) {
            m_stopping = true;
            m_state = FINISHING;
        }
    }
    join();
}

const char * VideoRecorder::extension(int codec)
{
    switch (codec) {
        case VIDEO_CODEC_ZMBV:   return ".avi";
        case VIDEO_CODEC_PRORES: return ".mov";
        default:                 return ".mp4";
    }
}

const char * VideoRecorder::codec_name(int codec)
{
    switch (codec) {
        case VIDEO_CODEC_ZMBV:   return "zmbv";
        case VIDEO_CODEC_H265:   return "h265";
        case VIDEO_CODEC_PRORES: return "prores";
        default:                 return "h264";
    }
}

int VideoRecorder::codec_by_name(const std::string &name)
{
    if (name == "zmbv") return VIDEO_CODEC_ZMBV;
    if (name == "h265") return VIDEO_CODEC_H265;
    if (name == "prores") return VIDEO_CODEC_PRORES;
    return VIDEO_CODEC_H264;
}

void VideoRecorder::join()
{
#if USE_QT_THREADING
    if (m_thread) {
        m_thread->join();
        delete m_thread;
        m_thread = nullptr;
    }
#else
    if (m_thread.joinable()) m_thread.join();
#endif
}

bool VideoRecorder::start(const VideoSettings &s, uint64_t clock_freq, std::string &error)
{
    if (m_state.load() == RECORDING || m_state.load() == FINISHING) {
        error = "A recording is already running";
        return false;
    }
    join();
    if (s.ffmpeg.empty()) {
        error = "ffmpeg is not found";
        return false;
    }
    if (clock_freq == 0 || s.fps_num == 0 || s.fps_den == 0) {
        error = "No machine is running";
        return false;
    }

    compat_lock_guard lock(m_mutex);
    m_settings = s;
    m_clock_freq = clock_freq;
    m_started = false;
    m_start = 0;
    m_due = 0;
    m_width = m_height = 0;
    m_last.reset();
    m_queue.clear();
    m_queued_bytes = 0;
    m_stopping = false;
    m_message.clear();
    m_video_file = with_suffix(s.file, std::string(".video") + extension(s.codec));
    m_audio_file = with_suffix(s.file, ".audio.wav");
    m_audio = nullptr;
    m_audio_rate = 0;
    m_audio_samples = 0;
    m_process = Process();
    m_process_started = false;
    m_frames_written = 0;
    if (!s.log_file.empty()) delete_file(s.log_file);
    m_state = RECORDING;

#if USE_QT_THREADING
    m_thread = EmuThread::create([this]() { worker(); });
#else
    m_thread = std::thread([this]() { worker(); });
#endif
    return true;
}

void VideoRecorder::set_start(uint64_t clock)
{
    compat_lock_guard lock(m_mutex);
    m_start = clock;
    m_started = true;
}

bool VideoRecorder::launch(std::string &error)
{
    const VideoSettings &s = m_settings;
    std::vector<std::string> a;
    a.push_back(s.ffmpeg);
    a.push_back("-hide_banner");
    a.push_back("-nostdin");    //The input is the picture; ffmpeg must not take it for keys
    a.push_back("-loglevel"); a.push_back("error");
    a.push_back("-y");
    a.push_back("-f"); a.push_back("rawvideo");
    a.push_back("-pix_fmt"); a.push_back("rgba");
    a.push_back("-s"); a.push_back(std::to_string(m_width) + "x" + std::to_string(m_height));
    a.push_back("-framerate");
    a.push_back(s.fps_den == 1 ? std::to_string(s.fps_num)
                               : std::to_string(s.fps_num) + "/" + std::to_string(s.fps_den));
    a.push_back("-i"); a.push_back("pipe:0");

    //4:2:0 colour halves the resolution of the colour both ways, which smears
    //a picture of a few hundred machine pixels; enlarged by a whole number
    //without smoothing it keeps its pixels
    const bool yuv420 = s.codec == VIDEO_CODEC_H264 || s.codec == VIDEO_CODEC_H265;
    std::vector<std::string> filters;
    if (yuv420 && s.upscale && m_height > 0 && m_height < 480) {
        const unsigned int k = (480 + m_height - 1) / m_height;
        filters.push_back("scale=iw*" + std::to_string(k) + ":ih*" + std::to_string(k) + ":flags=neighbor");
    }
    if (s.codec != VIDEO_CODEC_ZMBV)
        filters.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
    if (s.sar > 0 && (s.sar < 0.999 || s.sar > 1.001))
        filters.push_back("setsar=" + std::to_string((int)(s.sar * 10000 + 0.5)) + "/10000");
    if (!filters.empty()) {
        std::string vf;
        for (size_t i = 0; i < filters.size(); i++) vf += (i ? "," : "") + filters[i];
        a.push_back("-vf"); a.push_back(vf);
    }

    switch (s.codec) {
        case VIDEO_CODEC_ZMBV:
            a.push_back("-c:v"); a.push_back("zmbv");
            a.push_back("-pix_fmt"); a.push_back("bgr0");
            break;
        case VIDEO_CODEC_H265:
            a.push_back("-c:v"); a.push_back("libx265");
            a.push_back("-preset"); a.push_back("fast");
            a.push_back("-crf"); a.push_back("20");
            a.push_back("-pix_fmt"); a.push_back("yuv420p");
            a.push_back("-tag:v"); a.push_back("hvc1");
            a.push_back("-x265-params"); a.push_back("log-level=error");
            break;
        case VIDEO_CODEC_PRORES:
            a.push_back("-c:v"); a.push_back("prores_ks");
            a.push_back("-profile:v"); a.push_back("3");
            a.push_back("-pix_fmt"); a.push_back("yuv422p10le");
            break;
        default:
            a.push_back("-c:v"); a.push_back("libx264");
            a.push_back("-preset"); a.push_back("veryfast");
            a.push_back("-crf"); a.push_back("18");
            a.push_back("-pix_fmt"); a.push_back("yuv420p");
            break;
    }
    a.push_back(m_video_file);

    if (!spawn(a, s.log_file, true, m_process, error)) return false;
    m_process_started = true;
    return true;
}

//Under the lock. The first picture fixes the size and starts ffmpeg; null
//when that failed
std::shared_ptr<std::vector<uint8_t> > VideoRecorder::make_picture(const uint8_t * rgba, unsigned int w, unsigned int h, bool bottom_up)
{
    if (m_width == 0) {
        m_width = w;
        m_height = h;
        std::string error;
        if (!launch(error)) {
            m_message = error;
            m_stopping = true;
            m_state = FAILED;
            return std::shared_ptr<std::vector<uint8_t> >();
        }
    }

    std::shared_ptr<std::vector<uint8_t> > p(new std::vector<uint8_t>((size_t)m_width * m_height * 4));
    const uint32_t * in = reinterpret_cast<const uint32_t*>(rgba);
    uint32_t * out = reinterpret_cast<uint32_t*>(p->data());
    if (w == m_width && h == m_height) {
        if (!bottom_up) memcpy(p->data(), rgba, p->size());
        else
            for (unsigned int y = 0; y < h; y++)
                memcpy(out + (size_t)y * w, in + (size_t)(h - 1 - y) * w, (size_t)w * 4);
    } else {
        //Another video mode, another window size: the size of the video stays,
        //the picture is fitted into it pixel by pixel
        for (unsigned int y = 0; y < m_height; y++) {
            unsigned int sy = (unsigned int)((uint64_t)y * h / m_height);
            if (bottom_up) sy = h - 1 - sy;
            const uint32_t * row = in + (size_t)sy * w;
            for (unsigned int x = 0; x < m_width; x++)
                *out++ = row[(uint64_t)x * w / m_width];
        }
    }
    return p;
}

void VideoRecorder::set_frame(const uint8_t * rgba, unsigned int w, unsigned int h, bool bottom_up)
{
    if (rgba == nullptr || w == 0 || h == 0) return;
    if (m_state.load() != RECORDING) return;

    compat_lock_guard lock(m_mutex);
    if (m_state.load() != RECORDING || m_stopping || m_settings.frame_locked) return;

    std::shared_ptr<std::vector<uint8_t> > p = make_picture(rgba, w, h, bottom_up);
    if (p) m_last = p;
}

void VideoRecorder::push_frame(const uint8_t * rgba, unsigned int w, unsigned int h)
{
    if (rgba == nullptr || w == 0 || h == 0) return;
    if (m_state.load() != RECORDING) return;

    compat_lock_guard lock(m_mutex);
    if (m_state.load() != RECORDING || m_stopping || !m_settings.frame_locked || !m_started) return;

    //Far behind: the frame still counts, as a copy of the last picture
    if (!m_last || m_queued_bytes <= 2 * QUEUE_LIMIT) {
        std::shared_ptr<std::vector<uint8_t> > p = make_picture(rgba, w, h, false);
        if (!p) return;
        //A picture that has not changed is written as another copy of the last
        if (!m_last || *p != *m_last) m_last = p;
    }
    queue_last(1);
}

void VideoRecorder::queue_due(uint64_t clock)
{
    if (m_settings.frame_locked) return;
    if (!m_started || !m_last || clock < m_start) return;
    const uint64_t due = (clock - m_start) * m_settings.fps_num / (m_clock_freq * m_settings.fps_den) + 1;
    if (due <= m_due) return;
    queue_last(due - m_due);
    m_due = due;
}

//Under the lock: the last picture, count more times
void VideoRecorder::queue_last(uint64_t count)
{
    if (!m_last || count == 0) return;
    if (!m_queue.empty() && m_queue.back().pixels == m_last) {
        m_queue.back().count += count;
        return;
    }
    Frame f;
    f.pixels = m_last;
    f.count = count;
    m_queue.push_back(f);
    m_queued_bytes += m_last->size();
}

void VideoRecorder::advance(uint64_t clock)
{
    if (m_state.load() != RECORDING) return;
    m_mutex.lock();
    if (m_stopping) { m_mutex.unlock(); return; }
    queue_due(clock);
    //Waiting here holds back only the screen: the machine runs on, and the
    //frames it passes meanwhile become copies of the next picture
    while (m_queued_bytes > QUEUE_LIMIT && m_queue.size() > 2 && !m_stopping) {
        m_mutex.unlock();
        compat_sleep_ms(2);
        m_mutex.lock();
    }
    m_mutex.unlock();
}

void VideoRecorder::add_audio(const int16_t * samples, size_t count, unsigned int rate)
{
    if (count == 0 || rate == 0) return;
    compat_lock_guard lock(m_mutex);
    if (m_state.load() != RECORDING || m_stopping) return;
    if (m_audio == nullptr) {
        if (m_audio_rate != 0) return;     //It could not be opened
        m_audio_rate = rate;
        m_audio = open_file(m_audio_file, "wb");
        if (m_audio == nullptr) return;
        write_wav_header(m_audio, rate, 0);
    }
    if (rate != m_audio_rate) return;
    if (little_endian()) fwrite(samples, 2, count, m_audio);
    else for (size_t i = 0; i < count; i++) put16(m_audio, (uint16_t)samples[i]);
    m_audio_samples += count;
}

void VideoRecorder::stop(uint64_t clock)
{
    compat_lock_guard lock(m_mutex);
    if (m_state.load() != RECORDING) return;
    queue_due(clock);
    m_stopping = true;
    m_state = FINISHING;
}

std::string VideoRecorder::message()
{
    compat_lock_guard lock(m_mutex);
    return m_message;
}

void VideoRecorder::fail(const std::string &why)
{
    compat_lock_guard lock(m_mutex);
    if (m_message.empty()) m_message = why;
    m_stopping = true;
    m_state = FAILED;
}

std::string VideoRecorder::log_tail()
{
    if (m_settings.log_file.empty()) return std::string();
    FILE * f = open_file(m_settings.log_file, "rb");
    if (f == nullptr) return std::string();
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    //The last lines say what went wrong; the first ones are often a warning
    size_t pos = text.size();
    for (int lines = 0; lines < 3 && pos != std::string::npos && pos > 0; lines++)
        pos = text.find_last_of('\n', pos - 1);
    return (pos == std::string::npos) ? text : text.substr(pos + 1);
}

void VideoRecorder::worker()
{
    bool broken = false;
    for (;;) {
        Frame f;
        {
            compat_lock_guard lock(m_mutex);
            if (m_queue.empty()) {
                if (m_stopping) break;
                f.count = 0;
            } else {
                f = m_queue.front();
                m_queue.pop_front();
            }
        }
        if (f.count == 0) {
            compat_sleep_ms(2);
            continue;
        }
        for (uint64_t i = 0; i < f.count && !broken; i++) {
            if (write_input(m_process, f.pixels->data(), f.pixels->size())) m_frames_written++;
            else broken = true;
        }
        {
            compat_lock_guard lock(m_mutex);
            m_queued_bytes -= f.pixels->size();
            if (broken) {
                m_queue.clear();
                m_queued_bytes = 0;
                m_stopping = true;
            }
        }
        if (broken) break;
    }
    finish();
}

void VideoRecorder::finish()
{
    if (m_audio != nullptr) {
        const uint32_t bytes = (uint32_t)(m_audio_samples * 2);
        fseek(m_audio, 0, SEEK_SET);
        write_wav_header(m_audio, m_audio_rate, bytes);
        fclose(m_audio);
        m_audio = nullptr;
    }
    const bool with_audio = m_audio_samples > 0;

    if (!m_process_started) {
        if (with_audio) delete_file(m_audio_file);
        fail(m_message.empty() ? "No picture was recorded" : m_message);
        return;
    }

    close_input(m_process);
    const int code = wait_exit(m_process);
    if (code != 0) {
        if (with_audio) delete_file(m_audio_file);
        delete_file(m_video_file);
        const std::string tail = log_tail();
        fail(tail.empty() ? "ffmpeg exited with code " + std::to_string(code) : tail);
        return;
    }

    if (!with_audio) {
        if (!move_file(m_video_file, m_settings.file)) {
            fail("Unable to write " + m_settings.file);
            return;
        }
    } else {
        std::vector<std::string> a;
        a.push_back(m_settings.ffmpeg);
        a.push_back("-hide_banner");
        a.push_back("-nostdin");
        a.push_back("-loglevel"); a.push_back("error");
        a.push_back("-y");
        a.push_back("-i"); a.push_back(m_video_file);
        a.push_back("-i"); a.push_back(m_audio_file);
        a.push_back("-map"); a.push_back("0:v:0");
        a.push_back("-map"); a.push_back("1:a:0");
        a.push_back("-c:v"); a.push_back("copy");
        //The samples come at the rate the machine really makes them, which is
        //a little off the nominal one (the divider is rounded) and written
        //so into the WAV; resampled here, they keep in step with the picture
        a.push_back("-ar"); a.push_back("48000");
        if (m_settings.codec == VIDEO_CODEC_H264 || m_settings.codec == VIDEO_CODEC_H265) {
            a.push_back("-c:a"); a.push_back("aac");
            a.push_back("-b:a"); a.push_back("192k");
            a.push_back("-movflags"); a.push_back("+faststart");
        } else {
            a.push_back("-c:a"); a.push_back("pcm_s16le");
        }
        a.push_back(m_settings.file);

        Process mux;
        std::string error;
        int mux_code = -1;
        if (spawn(a, m_settings.log_file, false, mux, error)) mux_code = wait_exit(mux);
        delete_file(m_audio_file);
        if (mux_code != 0) {
            //The picture is there: better a video without sound than nothing
            const std::string tail = error.empty() ? log_tail() : error;
            move_file(m_video_file, m_settings.file);
            fail("The sound was not added: " + tail);
            return;
        }
        delete_file(m_video_file);
    }

    compat_lock_guard lock(m_mutex);
    m_message = m_settings.file;
    m_state = DONE;
}

// --- The ffmpeg process ---

#if defined(__EMSCRIPTEN__)

bool VideoRecorder::spawn(const std::vector<std::string> &, const std::string &, bool, Process &, std::string &error)
{
    error = "Video recording is not available here";
    return false;
}
bool VideoRecorder::write_input(Process &, const uint8_t *, size_t) { return false; }
void VideoRecorder::close_input(Process &) {}
int VideoRecorder::wait_exit(Process &) { return -1; }

#elif defined(_WIN32)

bool VideoRecorder::spawn(const std::vector<std::string> &args, const std::string &log, bool with_input, Process &p, std::string &error)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE in_read = nullptr, in_write = nullptr;
    if (with_input) {
        if (!CreatePipe(&in_read, &in_write, &sa, 1 << 20)) {
            error = "Unable to create a pipe for ffmpeg";
            return false;
        }
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    } else {
        in_read = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!log.empty()) {
        out = CreateFileW(dsk_tools::utf8_to_wide(log).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (out == INVALID_HANDLE_VALUE)
        out = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd;
    for (size_t i = 0; i < args.size(); i++) {
        if (i) cmd += L' ';
        cmd += quote_argument(dsk_tools::utf8_to_wide(args[i]));
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_read;
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    //No console window: the emulator is a windowed program, and a console
    //program started from it would get one of its own
    const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    if (in_read) CloseHandle(in_read);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) {
        if (in_write) CloseHandle(in_write);
        error = "Unable to start " + args[0];
        return false;
    }
    CloseHandle(pi.hThread);
    p.process = pi.hProcess;
    p.input = in_write;
    return true;
}

bool VideoRecorder::write_input(Process &p, const uint8_t * data, size_t size)
{
    if (p.input == nullptr) return false;
    while (size > 0) {
        DWORD written = 0;
        const DWORD part = (DWORD)std::min<size_t>(size, 1u << 24);
        if (!WriteFile((HANDLE)p.input, data, part, &written, nullptr) || written == 0) return false;
        data += written;
        size -= written;
    }
    return true;
}

void VideoRecorder::close_input(Process &p)
{
    if (p.input != nullptr) {
        CloseHandle((HANDLE)p.input);
        p.input = nullptr;
    }
}

int VideoRecorder::wait_exit(Process &p)
{
    close_input(p);
    if (p.process == nullptr) return -1;
    WaitForSingleObject((HANDLE)p.process, INFINITE);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess((HANDLE)p.process, &code);
    CloseHandle((HANDLE)p.process);
    p.process = nullptr;
    return (int)code;
}

#else

bool VideoRecorder::spawn(const std::vector<std::string> &args, const std::string &log, bool with_input, Process &p, std::string &error)
{
    //A write into the pipe of an ffmpeg that has quit must fail, not kill
    //the emulator
    signal(SIGPIPE, SIG_IGN);

    int fds[2] = {-1, -1};
    if (with_input && pipe(fds) != 0) {
        error = "Unable to create a pipe for ffmpeg";
        return false;
    }
    std::vector<char*> argv;
    for (size_t i = 0; i < args.size(); i++) argv.push_back(const_cast<char*>(args[i].c_str()));
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        if (with_input) { close(fds[0]); close(fds[1]); }
        error = "Unable to start " + args[0];
        return false;
    }
    if (pid == 0) {
        int in = with_input ? fds[0] : open("/dev/null", O_RDONLY);
        if (in >= 0) dup2(in, 0);
        int out = log.empty() ? -1 : open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out < 0) out = open("/dev/null", O_WRONLY);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); }
        if (with_input) close(fds[1]);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    if (with_input) {
        close(fds[0]);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    p.pid = pid;
    p.input = with_input ? fds[1] : -1;
    return true;
}

bool VideoRecorder::write_input(Process &p, const uint8_t * data, size_t size)
{
    if (p.input < 0) return false;
    while (size > 0) {
        const ssize_t n = write(p.input, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        data += n;
        size -= (size_t)n;
    }
    return true;
}

void VideoRecorder::close_input(Process &p)
{
    if (p.input >= 0) {
        close(p.input);
        p.input = -1;
    }
}

int VideoRecorder::wait_exit(Process &p)
{
    close_input(p);
    if (p.pid <= 0) return -1;
    int status = 0;
    while (waitpid(p.pid, &status, 0) < 0 && errno == EINTR) {}
    p.pid = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#endif
