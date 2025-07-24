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
    imageDisplaySize(0, 0), aspectRatioScale(1.0f) {}

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
    glViewport(0, 0, w, h);
    updateImageRect();
}

void GLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&mutex);
    if (!pendingImage.isNull()) {
        if (texture) delete texture;
        texture = new QOpenGLTexture(pendingImage.mirrored());
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        pendingImage = QImage();
    }

    if (!texture) return;

    program->bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    float scaleX, scaleY, offsetX, offsetY;

    if (imageDisplaySize.isNull()) {
        // Режим растягивания с сохранением пропорций
        float widgetAspect = (float)width() / height();
        float imageAspect = (float)texture->width() / texture->height();

        int borderWidth = 20;
        float borderAspect = std::min((float)(width() - 2*borderWidth)/width(), (float)(height() - 2*borderWidth)/height());

        // Применяем коэффициент масштабирования пропорций
        imageAspect *= aspectRatioScale;

        if (widgetAspect > imageAspect) {
            // Шире, чем изображение - ограничиваем по высоте
            scaleY = 1.0f;
            scaleX = imageAspect / widgetAspect;
        } else {
            // Уже, чем изображение - ограничиваем по ширине
            scaleX = 1.0f;
            scaleY = widgetAspect / imageAspect;
        }
        scaleY *= borderAspect;
        scaleX *= borderAspect;

        offsetX = 0.0f;
        offsetY = 0.0f;
    } else {
        // Режим фиксированного размера с центрированием
        scaleX = (float)imageRect.width() / width();
        scaleY = (float)imageRect.height() / height();
        offsetX = (float)(imageRect.x() + imageRect.width() / 2) / (width() / 2) - 1.0f;
        offsetY = 1.0f - (float)(imageRect.y() + imageRect.height() / 2) / (height() / 2);
    }

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

void GLWidget::updateTexture(const QImage& image) {
    QMutexLocker locker(&mutex);
    pendingImage = image;
    updateImageRect();
    update();
}

void GLWidget::setImageSize(const QSize& size) {
    imageDisplaySize = size;
    updateImageRect();
    update();
}

void GLWidget::setAspectRatioScale(float scale) {
    aspectRatioScale = scale;
    update();
}

void GLWidget::updateImageRect() {
    if (imageDisplaySize.isNull()) {
        // В режиме сохранения пропорций imageRect не используется,
        // так как масштабирование делается в шейдере
        imageRect = QRect(0, 0, width(), height());
        return;
    }

    // Центрируем изображение с заданным размером
    int x = (width() - imageDisplaySize.width()) / 2;
    int y = (height() - imageDisplaySize.height()) / 2;

    imageRect = QRect(x, y, imageDisplaySize.width(), imageDisplaySize.height());
}
