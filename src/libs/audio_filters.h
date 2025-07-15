// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Butterworth audio filter

#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

class ButterworthLowPassFilter {
private:
    // Коэффициенты фильтра
    float b0, b1, b2, a1, a2;
    // Состояния (задержки)
    float x1 = 0.0f, x2 = 0.0f; // предыдущие входные значения
    float y1 = 0.0f, y2 = 0.0f; // предыдущие выходные значения

public:
    // Инициализация фильтра с заданной частотой среза и частотой дискретизации
    void setup(float sampleRate, float cutoffFreq) {
        if (cutoffFreq <= 0 || cutoffFreq >= sampleRate / 2) {
            // Некорректная частота среза
            return;
        }

        // Расчет промежуточных значений
        float omega = 2.0f * M_PI * cutoffFreq / sampleRate;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float alpha = sn / (2.0f * 0.7071f); // Q = 0.7071 для Баттерворта

        // Расчет коэффициентов
        b0 = (1.0f - cs) / 2.0f;
        b1 = 1.0f - cs;
        b2 = b0;
        float a0 = 1.0f + alpha;
        a1 = -2.0f * cs;
        a2 = 1.0f - alpha;

        // Нормализация коэффициентов
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    }

    // Обработка одного отсчета
    float process(float input) {
        // Расчет выходного значения
        float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

        // Обновление состояний
        x2 = x1;
        x1 = input;
        y2 = y1;
        y1 = output;

        return output;
    }

    // Очистка состояний фильтра
    void reset() {
        x1 = x2 = 0.0f;
        y1 = y2 = 0.0f;
    }
};

