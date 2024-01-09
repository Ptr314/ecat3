#ifndef GENERICDBGWND_H
#define GENERICDBGWND_H

#include <QDialog>

class GenericDbgWnd : public QDialog
{
    Q_OBJECT
public:
    GenericDbgWnd(QWidget *parent);

signals:
    void data_changed(GenericDbgWnd * src);

public slots:
    virtual void update_view();
};

#endif // GENERICDBGWND_H
