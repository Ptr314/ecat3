// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Threading compatibility layer for MinGW 4.9.2

#pragma once

// Detection: MinGW < 5 ships Win32 threading model without std::thread/std::mutex.
// Manual overrides allow testing either path on any compiler.
#if defined(FORCE_QT_THREADING)
    #define USE_QT_THREADING 1
#elif defined(FORCE_STD_THREADING)
    #define USE_QT_THREADING 0
#elif defined(__MINGW32__) && defined(__GNUC__) && (__GNUC__ < 5)
    #define USE_QT_THREADING 1
#else
    #define USE_QT_THREADING 0
#endif

#if USE_QT_THREADING

#include <functional>

#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>

// QThread subclass that accepts a lambda, matching std::thread usage pattern.
// Heap-allocated via create() because QThread is non-movable.
class EmuThread : public QThread
{
public:
    template<typename F>
    static EmuThread* create(F&& func)
    {
        EmuThread* t = new EmuThread();
        t->m_func = std::forward<F>(func);
        t->start();
        return t;
    }

    void join()
    {
        wait();
    }

private:
    EmuThread() : QThread(nullptr) {}

    void run() override
    {
        m_func();
    }

    std::function<void()> m_func;
};

typedef QMutex compat_mutex;

#else // USE_QT_THREADING == 0

#include <thread>
#include <mutex>

typedef std::mutex compat_mutex;

#endif
