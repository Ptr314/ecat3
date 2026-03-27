// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration defines

#pragma once

#define PROJECT_NAME "eCat3"
#define VERSION_MAJOR "3"
#define VERSION_MINOR "5"
#define VERSION_PATCH "3"
#define PROJECT_VERSION "3.5.3"

/* #undef EXTERNAL_Z80 */
/* #undef CPU_STOPPED */
/* #undef LOGGER */
/* #undef LOG_NAME */
/* #undef LOG_LIMIT */

/* #undef LOG_8255 */
/* #undef LOG_CPU */
/* #undef LOG_FDD */
/* #undef LOG_PORTS */
/* #undef LOG_MAPPER */
/* #undef LOG_PAGE_MAPPER */
/* #undef LOG_INTERFACES */
/* #undef LOG_AGAT */

// Fallback for translation markers when building without QObject
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif
