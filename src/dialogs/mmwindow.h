#ifndef MMWINDOW_H
#define MMWINDOW_H

#include <QDialog>

namespace Ui {
class MemoryMapperWindow;
}

class MemoryMapperWindow : public QDialog
{
    Q_OBJECT

public:
    explicit MemoryMapperWindow(QWidget *parent = nullptr);
    ~MemoryMapperWindow();

private:
    Ui::MemoryMapperWindow *ui;
};

#endif // MMWINDOW_H
