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
    void setFiltering(bool linear);
    //The picture the way the window shows it, without the border around it;
    //GUI thread only. Null before the first frame
    QImage grabPicture();

public slots:
    //Run on the GUI thread, which owns the widget
    void applyPendingUpdate();
    void applyImageSize(QSize size);
    void applyAspectRatioScale(float scale);
    void applyFiltering(bool linear);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QOpenGLShaderProgram* program;
    QOpenGLTexture* texture;
    QImage pendingImage;
    QMutex mutex;
    GLuint vbo;
    QSize imageDisplaySize; // (0,0) - режим растягивания с пропорциями
    float aspectRatioScale; // Коэффициент масштабирования пропорций
    bool linearFiltering;
    QRect pictureRect;      // Где картинка легла в последнем кадре, в физических пикселях
};
