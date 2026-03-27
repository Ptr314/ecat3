// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Result type for error propagation

#pragma once

#include <string>
#include <utility>

namespace emulator {

    enum class ErrorCode {
        Ok = 0,
        ConfigError
    };

    struct Result {
        ErrorCode code;
        std::string message;
        static Result ok() {return Result{ ErrorCode::Ok, "" };}
        static Result error(const ErrorCode c, std::string msg) {return Result{ c, std::move(msg) };}
        static Result error(const ErrorCode c) {return Result{ c, "" };}
        bool isOk() const {return code == ErrorCode::Ok;}
        operator bool() const { return isOk(); }
    };

} // namespace emulator