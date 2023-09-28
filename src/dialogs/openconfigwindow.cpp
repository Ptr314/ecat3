#include <QDirIterator>

#include "emulator/config.h"
#include "openconfigwindow.h"
#include "ui_openconfigwindow.h"

//#include "maddy/parser.h"

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
    ui(new Ui::OpenConfigWindow)
{
    ui->setupUi(this);
}

OpenConfigWindow::OpenConfigWindow(QWidget *parent, QString work_path) :
    OpenConfigWindow(parent)
{
    list_machines(work_path);
}


OpenConfigWindow::~OpenConfigWindow()
{
    delete ui;
}

void OpenConfigWindow::list_machines(QString work_path)
{
    QStandardItemModel * model = new QStandardItemModel();
    QStandardItem * node = model->invisibleRootItem();

    qDebug() << "Listning machines in " << work_path;
    QDirIterator it(work_path, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFileInfo fi = it.nextFileInfo();
        if (fi.suffix().toLower() == "cfg")
        {
            qDebug() << fi.absoluteFilePath();
            EmulatorConfig * config = new EmulatorConfig(fi.absoluteFilePath());
            EmulatorConfigDevice * system = config->get_device("system");

            QString type = system->get_parameter("type").value;
            QString name = system->get_parameter("name").value;
            QString version = system->get_parameter("version", false).value;

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

            qDebug() << "DATA:" << family->data().toString();

            ComputerModel * computer = new ComputerModel(type, name, (!version.isEmpty())?version:name, fi.absoluteFilePath());
            family->appendRow(computer);

            qDebug() << "DATA:" << computer->data().toString();

            delete config;
        }
    }
    model->sort(0);
    ui->treeView->setHeaderHidden(true);
    ui->treeView->setModel(model);
    ui->treeView->expandAll();

    connect(ui->treeView , SIGNAL(clicked(const QModelIndex &)), this, SLOT(set_description(const QModelIndex &)));

}

void OpenConfigWindow::set_description(const QModelIndex & index)
{
    QString path = index.data(Qt::UserRole).toString();
    if (!path.isEmpty())
    {
        QString text_path = path.left(path.size()-3) + "md";
        qDebug() << "DESCRIPTION:" << text_path;
        QFile file(text_path);
        if (!file.open(QIODevice::ReadOnly)) {
            ui->textBrowser->setPlainText(OpenConfigWindow::tr("No any description file found for this machine"));
            return;
        }

        ui->textBrowser->setMarkdown(QString(file.readAll()));

        file.close();
    }
}
