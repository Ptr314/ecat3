// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Qt-dependent utility functions (not part of the emulator core)

#pragma once

#include <vector>
#include <string>
#include <QString>

QString pad_string(QString s, QChar c, int len, bool from_left = true);

unsigned decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp);

std::string md2html(const std::string &md);

// The language of the interface as two letters ("ru", "en", "bg"), set by the
// main window whenever it loads a translation
void set_ui_language(const QString &lang);
QString ui_language();
// A description in the language of the interface: <stem>.<lang>.md beside
// <stem>.md when there is one, else <stem>.md itself (written in Russian)
QString localized_description(const QString &md_path);

class QKeyEvent;
// The key a host key event stands for, the same under any host layout: a
// non-Latin character (Russian layout) is replaced by the key of the US layout
int host_layout_key(const QKeyEvent *event);
