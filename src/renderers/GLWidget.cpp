#include "GLWidget.h"

static const char* vertexShaderSrc = R"(
    attribute vec2 position;
    attribute vec2 texCoord;
    varying vec2 vTexCoord;
    void main() {
        gl_Position = vec4(position, 0.0, 1.0);
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
    : QOpenGLWidget(parent), program(nullptr), texture(nullptr), vbo(0) {}

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
}

void GLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&mutex);
    if (!pendingImage.isNull()) {
        if (texture) delete texture;
        texture = new QOpenGLTexture(pendingImage.mirrored());
        texture->setMinificationFilter(QOpenGLTexture::Linear);
        texture->setMagnificationFilter(QOpenGLTexture::Linear);
        pendingImage = QImage(); // clear to avoid reuse
    }

    if (!texture) return;

    program->bind();
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

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
    update();
}
