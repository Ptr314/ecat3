// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Computer selection window, source

#include <QDirIterator>

#include "emulator/config.h"
#include "openconfigwindow.h"
#include "ui_openconfigwindow.h"

#include "emulator/utils.h"

ComputerFamily::ComputerFamily(QString type, QString name):
    QStandardItem(name),
    type(type)
{
    setData(type);
}

ComputerModel::ComputerModel(QString type, QString name, QString version, QString path):
    QStandardItem(version),
    type(type),
    name(name),
    path(path)
{
    setData(path, Qt::UserRole);
}


OpenConfigWindow::OpenConfigWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OpenConfigWindow),
    selected_path("")
{
    ui->setupUi(this);
}

OpenConfigWindow::OpenConfigWindow(QWidget *parent, Emulator * e) :
    OpenConfigWindow(parent)
{
    this->e = e;
    list_machines(e->work_path);

    QFile file(e->data_path + "description.css");
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, OpenConfigWindow::tr("Error"), OpenConfigWindow::tr("Error opening CSS file"));
        return;
    }

    ui->textBrowser->document()->setDefaultStyleSheet(file.readAll());
    file.close();
}


OpenConfigWindow::~OpenConfigWindow()
{
    delete ui;
}

void OpenConfigWindow::list_machines(QString work_path)
{
    bool load_debugs = e->read_setup("Startup", "show_debug_versions", "0") == "1";

    QStandardItemModel * model = new QStandardItemModel();
    QStandardItem * node = model->invisibleRootItem();

    qDebug() << "Listing machines in " << work_path;
    QDirIterator it(work_path, QDirIterator::Subdirectories);
    while (it.hasNext()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        QFileInfo fi = it.nextFileInfo();
#else
        QFileInfo fi = QFileInfo(it.next());
#endif
        if (fi.suffix().toLower() == "cfg")
        {
            //qDebug() << fi.absoluteFilePath();
            EmulatorConfig * config = new EmulatorConfig(fi.absoluteFilePath());
            EmulatorConfigDevice * system = config->get_device("system");

            bool is_debug = system->get_parameter("debug", false).value == "1";

            if (!is_debug || load_debugs)
            {

                QString type = system->get_parameter("type").value;
                QString name = system->get_parameter("name").value;
                QString version = (is_debug?"* ":"") + system->get_parameter("version", false).value;

                int index = -1;
                for (int i = 0; i < node->rowCount(); i++)
                    if (node->child(i)->data().toString() == type)
                    {
                        index = i;
                        break;
                    }
                ComputerFamily * family;
                if (index < 0)
                {
                    family = new ComputerFamily(type, name);
                    node->appendRow(family);
                } else
                    family = dynamic_cast<ComputerFamily*>(node->child(index));

                //qDebug() << "DATA:" << family->data().toString();

                ComputerModel * computer = new ComputerModel(type, name, (!version.isEmpty())?version:name, fi.absoluteFilePath());
                family->appendRow(computer);

                //qDebug() << "DATA:" << computer->data().toString();
            }

            delete config;
        }
    }
    model->sort(0);
    ui->treeView->setHeaderHidden(true);
    ui->treeView->setModel(model);
    ui->treeView->expandAll();

    connect(ui->treeView , SIGNAL(clicked(QModelIndex)), this, SLOT(set_description(QModelIndex)));

}

void OpenConfigWindow::set_description(QModelIndex index)
{
    selected_path = index.data(Qt::UserRole).toString();
    if (!selected_path.isEmpty())
    {
        QString text_path = selected_path.left(selected_path.size()-3) + "md";
        //qDebug() << "DESCRIPTION:" << text_path;
        QFile file(text_path);
        if (!file.open(QIODevice::ReadOnly)) {
            ui->textBrowser->setPlainText(OpenConfigWindow::tr("No any description file found for this machine"));
            return;
        }

        QString html = "<body>" + md2html(file.readAll()) +"</body>";
        ui->textBrowser->document()->setHtml(html);

        file.close();
    }
}

void OpenConfigWindow::on_closeButton_clicked()
{
    close();
}


void OpenConfigWindow::on_okButton_clicked()
{
    if (!selected_path.isEmpty()) {
        emit load_config(selected_path, ui->defaultCheck->isChecked());
        close();
    }
}

