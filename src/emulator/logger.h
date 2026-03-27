// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Logger class

#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include "globals.h"

#ifndef LOG_LIMIT
#define LOG_LIMIT 0
#endif

class Logger
{
private:
    unsigned int logged_count;
    std::string log;
    std::string log_name;

    static std::string current_time_string()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d-%H-%M-%S", std::localtime(&t));
        return std::string(buf);
    }

    static std::string to_upper(const std::string &s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

public:
    Logger(const std::string &log_name):
        logged_count(0),
        log_name(log_name)
    {}

    bool log_available()
    {
        return logged_count < LOG_LIMIT;
    }

    void logs(const std::string &s)
    {
        if (log_available())
        {
            log += s + "\x0D\x0A";
            logged_count++;
        }
    }


    ~Logger()
    {
        if (logged_count > 0)
        {
            std::string formattedTime = current_time_string();
            std::string log_file = log_name + "_" + formattedTime + ".log";
            std::cerr << "Logged " << logged_count << " entries" << std::endl;
            std::cerr << "Writing log to " << log_file << std::endl;
            std::ofstream ofs(log_file, std::ios::binary);
            if (ofs.is_open())
            {
                std::string upper_log = to_upper(log);
                ofs.write(upper_log.c_str(), upper_log.size());
                ofs.close();
            }
        } else {
            std::cerr << "Log is empty, none to write" << std::endl;
        }
    }
};