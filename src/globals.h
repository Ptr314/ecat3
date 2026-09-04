// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration defines

#pragma once

#define PROJECT_NAME "eCat3"
#define VERSION_MAJOR "4"
#define VERSION_MINOR "0"
#define VERSION_PATCH "0"
#define PROJECT_VERSION "4.0.0"

/* #undef EXTERNAL_Z80 */
/* #undef CPU_STOPPED */

// Fallback for translation markers when building without QObject
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif
