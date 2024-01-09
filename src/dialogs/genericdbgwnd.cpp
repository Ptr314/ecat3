#include "genericdbgwnd.h"

GenericDbgWnd::GenericDbgWnd(QWidget *parent):
    QDialog(parent)
{}

void GenericDbgWnd::update_view()
{
    update();
}
