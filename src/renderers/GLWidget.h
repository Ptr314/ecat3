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

public slots:
    void updateTexture(const QImage& image);
    void setImageSize(const QSize& size); // (0,0) - растянуть с сохранением пропорций
    void setAspectRatioScale(float scale); // Установить коэффициент масштабирования пропорций

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
    QRect imageRect;
    float aspectRatioScale; // Коэффициент масштабирования пропорций
    void updateImageRect();
};
