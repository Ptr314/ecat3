#include "dialogs/debugdialog.h"

DebugDialog::DebugDialog(QWidget *parent, Emulator * e, ComputerDevice * device):
    QDialog(parent),
    e(e),
    d(device)
{}

DebugDialog::DebugDialog(QWidget *parent):
    QDialog(parent)
{}

DebugDialog DebugDialog::CreateDialog(QWidget *parent, Emulator * e, ComputerDevice * device)
{
    return new DebugDialog(parent, e, device);
}
