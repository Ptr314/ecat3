// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: OpenGL renderer widget, header

#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QImage>
#include <QMutex>
#include <functional>
#include <vector>

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    GLWidget(QWidget* parent = nullptr);
    ~GLWidget();

public:
    //Called from the emulator's render thread; each one posts to the GUI thread
    void updateTexture(const QImage& image);
    void setImageSize(const QSize& size); // (0,0) - растянуть с сохранением пропорций
    void setAspectRatioScale(float scale); // Установить коэффициент масштабирования пропорций
    //SCREEN_FILTERING_NONE, _LINEAR (or any other) and _CRT of core.h
    void setFiltering(int mode);
    //Lines the beam draws per frame, for the CRT look
    void setScanLines(int lines);
    //The picture the way the window shows it, without the border around it;
    //GUI thread only. Null before the first frame
    QImage grabPicture();
    //Called on the GUI thread with every frame drawn, as RGBA rows from the
    //bottom, the way OpenGL reads them: the picture without the border, cut
    //as grabPicture() cuts it but read from the frame just drawn instead of
    //drawing it again. Null: none
    typedef std::function<void(const uint8_t * rgba, int w, int h)> FrameHook;
    void setFrameHook(FrameHook hook) { frameHook = hook; }

public slots:
    //Run on the GUI thread, which owns the widget
    void applyPendingUpdate();
    void applyImageSize(QSize size);
    void applyAspectRatioScale(float scale);
    void applyFiltering(int mode);
    void applyScanLines(int lines);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QOpenGLShaderProgram* program;
    QOpenGLShaderProgram* crtProgram;
    QOpenGLTexture* texture;
    QImage pendingImage;
    QMutex mutex;
    GLuint vbo;
    QSize imageDisplaySize; // (0,0) - режим растягивания с пропорциями
    float aspectRatioScale; // Коэффициент масштабирования пропорций
    int filterMode;
    int scanLines;          // 0 - as many as the texture has rows
    QRect pictureRect;      // Где картинка легла в последнем кадре, в физических пикселях
    FrameHook frameHook;
    std::vector<uint8_t> frameRows;     // Кадр для frameHook, снизу вверх, как его отдает OpenGL
};
