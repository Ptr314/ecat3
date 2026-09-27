// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: OpenGL renderer widget, source

#include <cmath>

#include "GLWidget.h"

static const char* vertexShaderSrc = R"(
    attribute vec2 position;
    attribute vec2 texCoord;
    varying vec2 vTexCoord;
    uniform vec2 imageScale;
    uniform vec2 imageOffset;
    void main() {
        vec2 pos = position * imageScale + imageOffset;
        gl_Position = vec4(pos, 0.0, 1.0);
        vTexCoord = texCoord;
    }
)";

static const char* fragmentShaderSrc = R"(
    varying vec2 vTexCoord;
    uniform sampler2D tex;
    void main() {
        gl_FragColor = texture2D(tex, vTexCoord);
    }
)";

GLWidget::GLWidget(QWidget* parent)
    : QOpenGLWidget(parent), program(nullptr), texture(nullptr), vbo(0),
    imageDisplaySize(0, 0), aspectRatioScale(1.0f), linearFiltering(false) {}

GLWidget::~GLWidget() {
    makeCurrent();
    delete program;
    delete texture;
    if (vbo) glDeleteBuffers(1, &vbo);
    doneCurrent();
}

void GLWidget::initializeGL() {
    initializeOpenGLFunctions();

    program = new QOpenGLShaderProgram();
    program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSrc);
    program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSrc);
    program->link();

    static const float vertices[] = {
        // pos      // tex
        -1, -1,    0, 0,
        1, -1,    1, 0,
        -1,  1,    0, 1,
        1,  1,    1, 1,
    };

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
}

void GLWidget::resizeGL(int w, int h) {
    //The viewport is set in paintGL(): w and h are in logical pixels, the
    //framebuffer is in physical ones
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void GLWidget::paintGL() {
    //Everything below is counted in physical pixels of the framebuffer. In
    //logical ones an image rect of whole numbers lands on fractions of a pixel
    //under any Windows scale other than 100%
    const qreal dpr = devicePixelRatioF();
    const int fbW = qRound(width() * dpr);
    const int fbH = qRound(height() * dpr);
    glViewport(0, 0, fbW, fbH);
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&mutex);
    if (!pendingImage.isNull()) {
        if (texture) delete texture;
        //DontGenerateMipMaps: the texture is drawn at or above 1:1 and neither
        //filter uses mipmaps, so the mip chain built here 50 times a second was
        //never sampled
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        texture = new QOpenGLTexture(pendingImage.flipped(Qt::Vertical), QOpenGLTexture::DontGenerateMipMaps);
#else
        texture = new QOpenGLTexture(pendingImage.mirrored(), QOpenGLTexture::DontGenerateMipMaps);
#endif
        //The default wrap mode is Repeat: the linear filter then blends the last
        //row and column with the first ones, which drew a thin copy of the
        //opposite edge along the right and the bottom of the picture
        texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        pendingImage = QImage();
    }

    if (!texture) return;

    program->bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    const QOpenGLTexture::Filter filter = linearFiltering ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest;
    texture->setMinificationFilter(filter);
    texture->setMagnificationFilter(filter);

    // Размер изображения в физических пикселях
    int imgW, imgH;
    if (imageDisplaySize.isNull()) {
        // Режим растягивания с сохранением пропорций
        float widgetAspect = (float)fbW / fbH;
        float imageAspect = (float)texture->width() / texture->height();

        float borderWidth = 20 * dpr;
        float borderAspect = std::min((fbW - 2*borderWidth)/fbW, (fbH - 2*borderWidth)/fbH);

        // Применяем коэффициент масштабирования пропорций
        imageAspect *= aspectRatioScale;

        float w, h;
        if (widgetAspect > imageAspect) {
            // Шире, чем изображение - ограничиваем по высоте
            h = fbH;
            w = fbH * imageAspect;
        } else {
            // Уже, чем изображение - ограничиваем по ширине
            w = fbW;
            h = fbW / imageAspect;
        }
        imgW = qRound(w * borderAspect);
        imgH = qRound(h * borderAspect);
    } else {
        // Режим фиксированного размера с центрированием
        imgW = qRound(imageDisplaySize.width() * dpr);
        imgH = qRound(imageDisplaySize.height() * dpr);
    }

    //The corner is a whole pixel, so at an integer scale every texel covers
    //the same number of screen pixels. The old code divided by width() / 2 in
    //integers, which moved the picture by a fraction of a pixel whenever the
    //widget was an odd number of pixels wide
    const int imgX = (fbW - imgW) / 2;
    const int imgY = (fbH - imgH) / 2;
    const float scaleX = (float)imgW / fbW;
    const float scaleY = (float)imgH / fbH;
    const float offsetX = (float)(2 * imgX + imgW) / fbW - 1.0f;
    const float offsetY = 1.0f - (float)(2 * imgY + imgH) / fbH;

    program->setUniformValue("imageScale", scaleX, scaleY);
    program->setUniformValue("imageOffset", offsetX, offsetY);

    int posLoc = program->attributeLocation("position");
    int texLoc = program->attributeLocation("texCoord");

    program->enableAttributeArray(posLoc);
    program->setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 4 * sizeof(float));
    program->enableAttributeArray(texLoc);
    program->setAttributeBuffer(texLoc, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    texture->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    texture->release();

    program->disableAttributeArray(posLoc);
    program->disableAttributeArray(texLoc);
    program->release();
}

//The setters below are called from the emulator's render thread. A QWidget
//belongs to the GUI thread, so the work is posted to it instead of being done
//here: the fields they change are read by paintGL() on the GUI thread.
void GLWidget::updateTexture(const QImage& image) {
    {
        QMutexLocker locker(&mutex);
        pendingImage = image;
    }
    QMetaObject::invokeMethod(this, "applyPendingUpdate", Qt::QueuedConnection);
}

void GLWidget::setImageSize(const QSize& size) {
    QMetaObject::invokeMethod(this, "applyImageSize", Qt::QueuedConnection, Q_ARG(QSize, size));
}

void GLWidget::setAspectRatioScale(float scale) {
    QMetaObject::invokeMethod(this, "applyAspectRatioScale", Qt::QueuedConnection, Q_ARG(float, scale));
}

void GLWidget::setFiltering(bool linear) {
    QMetaObject::invokeMethod(this, "applyFiltering", Qt::QueuedConnection, Q_ARG(bool, linear));
}

//...and these run on the GUI thread
void GLWidget::applyPendingUpdate() {
    update();
}

void GLWidget::applyImageSize(QSize size) {
    imageDisplaySize = size;
    update();
}

void GLWidget::applyAspectRatioScale(float scale) {
    aspectRatioScale = scale;
    update();
}

void GLWidget::applyFiltering(bool linear) {
    linearFiltering = linear;
    update();
}
