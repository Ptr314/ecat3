// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: OpenGL renderer widget, source

#include <cmath>

#include "GLWidget.h"
#include "emulator/core.h"

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

//A picture tube: every scan line is a beam with a flat top and a steep edge
//(exp(-(d/w)^4), not a gaussian, whose long tails filled the gaps and made
//text look merely blurred), a little wider where it is brighter, summed with
//the beams of the lines above and below in linear light, then a light
//aperture grille of screen pixels. The texture is sampled at
//the centres of its texels (nearest filter), so a surface that doubles its
//lines still gives each scan line one colour. Where a scan line has less than
//two screen pixels the beams cannot be drawn without moire, and the effect
//fades to the plain picture
static const char* crtFragmentShaderSrc = R"(
    varying vec2 vTexCoord;
    uniform sampler2D tex;
    uniform vec2 texSize;
    uniform vec2 outSize;
    uniform float scanLines;

    vec3 texel(float x, float line) {
        vec2 uv = vec2((x + 0.5) / texSize.x, (line + 0.5) / scanLines);
        vec3 c = texture2D(tex, uv).rgb;
        return c * c;
    }

    //Neighbouring texels of a line blended over a short slope only: the dots
    //of a tube are soft, but not as soft as a linear filter makes them
    vec3 row(float line) {
        float x = vTexCoord.x * texSize.x - 0.5;
        float x0 = floor(x);
        float f = x - x0;
        float sharp = max(outSize.x / texSize.x, 1.0);
        f = clamp((f - 0.5) * sharp + 0.5, 0.0, 1.0);
        return mix(texel(max(x0, 0.0), line), texel(min(x0 + 1.0, texSize.x - 1.0), line), f);
    }

    vec3 beam(vec3 c, float d) {
        vec3 w = mix(vec3(0.30), vec3(0.40), sqrt(c));
        vec3 x = vec3(d) / w;
        x *= x;
        return c * exp(-x * x);
    }

    //The centre of a scan line, in screen pixels from the top, put on the
    //centre of a pixel: at two pixels a line, both pixels of it would
    //otherwise lie half a pixel off the centre and come out alike - no lines
    float lineCentre(float line, float ppl) {
        return floor((line + 0.5) * ppl) + 0.5;
    }

    vec3 lineAt(float line, float py, float ppl) {
        if (line < 0.0 || line > scanLines - 1.0) return vec3(0.0);
        return beam(row(line), abs(py - lineCentre(line, ppl)) / ppl);
    }

    void main() {
        float ppl = outSize.y / scanLines;
        float py = vTexCoord.y * outSize.y;
        float l = floor(py / ppl);
        vec3 c = lineAt(l - 1.0, py, ppl) + lineAt(l, py, ppl) + lineAt(l + 1.0, py, ppl);

        float m = mod(floor(gl_FragCoord.x), 3.0);
        vec3 mask = vec3(0.88);
        if (m < 0.5) mask.r = 1.0; else if (m < 1.5) mask.g = 1.0; else mask.b = 1.0;
        //The beam covers about 0.6 of its line: brought back to the level of
        //the plain picture
        c *= mask * 1.75;

        vec3 plain = texel(floor(vTexCoord.x * texSize.x), floor(vTexCoord.y * scanLines));
        float strength = clamp((ppl - 1.75) / 0.25, 0.0, 1.0);
        c = mix(plain, c, strength);
        gl_FragColor = vec4(sqrt(c), 1.0);
    }
)";

GLWidget::GLWidget(QWidget* parent)
    : QOpenGLWidget(parent), program(nullptr), crtProgram(nullptr), texture(nullptr), vbo(0),
    imageDisplaySize(0, 0), aspectRatioScale(1.0f), filterMode(SCREEN_FILTERING_NONE), scanLines(0) {}

GLWidget::~GLWidget() {
    makeCurrent();
    delete program;
    delete crtProgram;
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

    crtProgram = new QOpenGLShaderProgram();
    crtProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSrc);
    crtProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, crtFragmentShaderSrc);
    //A driver that cannot compile it leaves the plain picture
    if (!crtProgram->link()) {
        delete crtProgram;
        crtProgram = nullptr;
    }

    //The texture holds the frame as the image has it, top row first, so the
    //quad takes it upside down: t = 0 at the top. The image used to be
    //mirrored on the CPU instead, a full copy of the frame 50 times a second
    static const float vertices[] = {
        // pos      // tex
        -1, -1,    0, 1,
        1, -1,    1, 1,
        -1,  1,    0, 0,
        1,  1,    1, 0,
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
        //One texture for as long as the frame keeps its size, and the frame
        //copied straight into it: a new QOpenGLTexture from a QImage converts
        //the image to RGBA and allocates the storage again on every frame
        const QImage frame = (pendingImage.format() == QImage::Format_RGB32
                              || pendingImage.format() == QImage::Format_ARGB32)
                             ? pendingImage : pendingImage.convertToFormat(QImage::Format_RGB32);
        if (!texture || texture->width() != frame.width() || texture->height() != frame.height()) {
            delete texture;
            texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
            texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
            texture->setSize(frame.width(), frame.height());
            //No mip chain: the texture is drawn at or above 1:1 and neither
            //filter uses one
            texture->setMipLevels(1);
            texture->allocateStorage(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8);
            //The default wrap mode is Repeat: the linear filter then blends the last
            //row and column with the first ones, which drew a thin copy of the
            //opposite edge along the right and the bottom of the picture
            texture->setWrapMode(QOpenGLTexture::ClampToEdge);
        }
        //RGB32 is 0xFFRRGGBB, stored B, G, R, A in memory; rows are packed,
        //four bytes a pixel, so the default unpack alignment fits
        texture->setData(QOpenGLTexture::BGRA, QOpenGLTexture::UInt8, frame.constBits());
        pendingImage = QImage();
    }

    if (!texture) {
        pictureRect = QRect();
        return;
    }

    const bool crt = filterMode == SCREEN_FILTERING_CRT && crtProgram != nullptr;
    QOpenGLShaderProgram * const prog = crt ? crtProgram : program;
    prog->bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    //The CRT shader picks the texels itself and must get them unblended
    const QOpenGLTexture::Filter filter = (filterMode == SCREEN_FILTERING_NONE || crt)
                                          ? QOpenGLTexture::Nearest : QOpenGLTexture::Linear;
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
    pictureRect = QRect(imgX, imgY, imgW, imgH);
    const float scaleX = (float)imgW / fbW;
    const float scaleY = (float)imgH / fbH;
    const float offsetX = (float)(2 * imgX + imgW) / fbW - 1.0f;
    const float offsetY = 1.0f - (float)(2 * imgY + imgH) / fbH;

    prog->setUniformValue("imageScale", scaleX, scaleY);
    prog->setUniformValue("imageOffset", offsetX, offsetY);
    if (crt) {
        //A count that does not divide the rows (a frame of a new size whose
        //lines have not come yet) is taken as the rows themselves
        const int rows = texture->height();
        const int lines = (scanLines > 0 && scanLines <= rows && rows % scanLines == 0) ? scanLines : rows;
        prog->setUniformValue("texSize", (float)texture->width(), (float)rows);
        prog->setUniformValue("outSize", (float)imgW, (float)imgH);
        prog->setUniformValue("scanLines", (float)lines);
    }

    int posLoc = prog->attributeLocation("position");
    int texLoc = prog->attributeLocation("texCoord");

    prog->enableAttributeArray(posLoc);
    prog->setAttributeBuffer(posLoc, GL_FLOAT, 0, 2, 4 * sizeof(float));
    prog->enableAttributeArray(texLoc);
    prog->setAttributeBuffer(texLoc, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    texture->bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    texture->release();

    prog->disableAttributeArray(posLoc);
    prog->disableAttributeArray(texLoc);
    prog->release();

    if (frameHook) {
        const QRect r = pictureRect.intersected(QRect(0, 0, fbW, fbH));
        if (!r.isEmpty()) {
            frameRows.resize((size_t)r.width() * 4 * r.height());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            //The framebuffer counts rows from the bottom
            glReadPixels(r.x(), fbH - r.y() - r.height(), r.width(), r.height(),
                         GL_RGBA, GL_UNSIGNED_BYTE, frameRows.data());
            frameHook(frameRows.data(), r.width(), r.height());
        }
    }
}

QImage GLWidget::grabPicture() {
    //grabFramebuffer() runs paintGL() into an offscreen buffer of the widget's
    //size, so the texture, the filter and pictureRect are those of this frame.
    //A picture larger than the window (a fixed scale) is cut where it is cut
    //on the screen
    const QImage frame = grabFramebuffer();
    const QRect r = pictureRect.intersected(frame.rect());
    if (r.isEmpty()) return QImage();
    QImage picture = frame.copy(r);
    picture.setDevicePixelRatio(1);
    //Opaque: the frame keeps the alpha of the clear colour, the PNG should not
    picture = picture.convertToFormat(QImage::Format_RGB32);
    return picture;
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

void GLWidget::setFiltering(int mode) {
    QMetaObject::invokeMethod(this, "applyFiltering", Qt::QueuedConnection, Q_ARG(int, mode));
}

void GLWidget::setScanLines(int lines) {
    QMetaObject::invokeMethod(this, "applyScanLines", Qt::QueuedConnection, Q_ARG(int, lines));
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

void GLWidget::applyFiltering(int mode) {
    filterMode = mode;
    update();
}

void GLWidget::applyScanLines(int lines) {
    scanLines = lines;
    update();
}
