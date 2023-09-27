#ifndef OPENCONFIGWINDOW_H
#define OPENCONFIGWINDOW_H

#include <QDialog>

namespace Ui {
class OpenConfigWindow;
}

class OpenConfigWindow : public QDialog
{
    Q_OBJECT

public:
    explicit OpenConfigWindow(QWidget *parent = nullptr);
    ~OpenConfigWindow();

private:
    Ui::OpenConfigWindow *ui;
};

#endif // OPENCONFIGWINDOW_H
