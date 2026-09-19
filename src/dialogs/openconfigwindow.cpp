// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Computer selection window, source

#include <QDirIterator>
#include <QFile>
#include <QTreeView>
#include <QMessageBox>

#include "emulator/config.h"
#include "emulator/config_ext.h"
#include "emulator/utils.h"
#include "openconfigwindow.h"
#include "ui_openconfigwindow.h"

#include "qt_utils.h"
#include "genericdbgwnd.h"
#include "exteditorwindow.h"

ComputerFamily::ComputerFamily(QString type, QString name):
    QStandardItem(name),
    type(type)
{
    setEditable(false);
    setData(type);
}

ComputerModel::ComputerModel(QString type, QString name, QString version, QString path, int order):
    QStandardItem(version),
    type(type),
    name(name),
    path(path),
    order(order)
{
    setEditable(false);
    setData(path, Qt::UserRole);
}

bool ComputerModel::operator<(const QStandardItem &other) const
{
    const ComputerModel * m = dynamic_cast<const ComputerModel*>(&other);
    if (m != nullptr && order != m->order) return order < m->order;
    return QStandardItem::operator<(other);
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

    {
        // Setting the initial state must not trigger a repeated listing
        QSignalBlocker blocker(ui->debugCheck);
        ui->debugCheck->setChecked(e->read_setup("Startup", "show_debug_versions", "0") == "1");
    }
    {
        QSignalBlocker blocker(ui->userButton);
        ui->userButton->setChecked(e->read_setup("Startup", "show_user_configs", "0") == "1");
    }

    // Editable items would swallow the double click to open an editor
    // instead of emitting doubleClicked()
    ui->treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->treeView, &QTreeView::clicked, this, &OpenConfigWindow::set_description);
    connect(ui->treeView, &QTreeView::doubleClicked, this, &OpenConfigWindow::on_item_double_clicked);

    list_machines(QString::fromStdString(e->work_path));
    update_buttons();

    QFile file(QString::fromStdString(e->data_path + "description.css"));
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(0, OpenConfigWindow::tr("Error"), OpenConfigWindow::tr("Error opening CSS file"));
        return;
    }

    ui->textBrowser->document()->setDefaultStyleSheet(file.readAll());
    file.close();

    QFile default_md(QString::fromStdString(e->work_path + "default.md"));
    if (default_md.open(QIODevice::ReadOnly)) {
        QString html = "<body>" + QString::fromStdString(md2html(default_md.readAll().toStdString())) + "</body>";
        ui->textBrowser->document()->setHtml(html);
        default_md.close();
    }
}


OpenConfigWindow::~OpenConfigWindow()
{
    delete ui;
}

void OpenConfigWindow::list_machines(QString work_path)
{
    bool load_debugs = ui->debugCheck->isChecked();

    selected_path = "";

    QStandardItemModel * model = new QStandardItemModel();
    QStandardItem * node = model->invisibleRootItem();

    MachinePaths paths;
    paths.computers_path = e->work_path;

    //Configurations of the user that do not load are shown greyed out with
    //the reason, rather than dropped: a user has no other way to find out why
    //the variant is not in the list
    QStandardItem * broken = nullptr;

    auto add_machine = [&](const QFileInfo &fi, bool users) {
        //Only the system section is read, of the base for an extension, with
        //the extension's own changes to it applied
        EmulatorConfig config;
        MachineSource source;
        emulator::Result res = load_machine_description(fi.absoluteFilePath().toStdString(), paths, config, source, true);
        if (!res) {
            if (!users) return;
            if (broken == nullptr) {
                broken = new QStandardItem(OpenConfigWindow::tr("Configurations with errors"));
                broken->setEditable(false);
                broken->setData(QString("~broken"));
                node->appendRow(broken);
            }
            QStandardItem * item = new QStandardItem(fi.fileName());
            item->setEditable(false);
            item->setEnabled(false);
            item->setToolTip(translateResultMessage(res.message));
            broken->appendRow(item);
            return;
        }
        EmulatorConfigDevice * system = config.get_device("system");
        if (system == nullptr) return;

        bool is_debug = system->get_parameter("debug", false).value == "1";
        if (is_debug && !load_debugs) return;

        QString type = QString::fromStdString(system->get_parameter("type", false).value);
        QString name = QString::fromStdString(system->get_parameter("name", false).value);
        if (type.isEmpty() || name.isEmpty()) return;
        QString version = QString::fromStdString((is_debug?"* ":"") + system->get_parameter("version", false).value);

        // Position inside the family, 0 when not set. Always decimal:
        // the chooser reads the file without the machine's own radix
        int order = 0;
        std::string order_str = system->get_parameter("order", false).value;
        if (!order_str.empty())
            try {
                order = static_cast<int>(parse_numeric_value(order_str, 10));
            } catch (std::invalid_argument &) {}

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

        //An extension may have a description of its own, otherwise it shows
        //that of its base
        QString description = QString::fromStdString(machine_file_stem(source.file)) + ".md";
        if (!QFileInfo::exists(description))
            description = QString::fromStdString(machine_file_stem(source.base_cfg)) + ".md";

        ComputerModel * computer = new ComputerModel(type, name, (!version.isEmpty())?version:name, fi.absoluteFilePath(), order);
        computer->setData(description, Qt::UserRole + 2);
        family->appendRow(computer);
    };

    //Either the distributed machines or those of the user, by the switch on
    //the tool bar
    const bool users = ui->userButton->isChecked();
    const QString root = users ? QString::fromStdString(e->user_ext_path) : work_path;
    if (!root.isEmpty() && QFileInfo(root).isDir())
    {
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
            QFileInfo fi = it.nextFileInfo();
#else
            QFileInfo fi = QFileInfo(it.next());
#endif
            if (is_machine_file(fi.fileName().toStdString())) add_machine(fi, users);
        }
    }

    model->sort(0);
    ui->treeView->setHeaderHidden(true);
    QAbstractItemModel * old_model = ui->treeView->model();
    ui->treeView->setModel(model);
    if (old_model != nullptr) delete old_model;
    ui->treeView->expandAll();

    //A new model comes with a new selection model. The keyboard and
    //accessibility tools select an item too, not only a click
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &selected, const QItemSelection &) {
                if (!selected.indexes().isEmpty()) set_description(selected.indexes().first());
            });
    update_buttons();
}

void OpenConfigWindow::update_buttons()
{
    //Only an extension is the user's to change: a .cfg is the base set
    const QString p = selected_path.toLower();
    const bool is_cfg = p.endsWith(".cfg");
    const bool is_zip = p.endsWith(".ext.zip");
    const bool is_ext = p.endsWith(".ext");
    ui->copyButton->setEnabled(is_cfg || is_ext);
    ui->editButton->setEnabled(is_ext);
    ui->deleteButton->setEnabled(is_ext || is_zip);
}

void OpenConfigWindow::select_path(const QString &path)
{
    QAbstractItemModel * model = ui->treeView->model();
    if (model == nullptr) return;
    for (int i = 0; i < model->rowCount(); i++) {
        const QModelIndex family = model->index(i, 0);
        for (int j = 0; j < model->rowCount(family); j++) {
            const QModelIndex item = model->index(j, 0, family);
            if (QFileInfo(item.data(Qt::UserRole).toString()) == QFileInfo(path)) {
                //Selecting it runs set_description() through the selection model
                ui->treeView->setCurrentIndex(item);
                ui->treeView->scrollTo(item);
                return;
            }
        }
    }
}

void OpenConfigWindow::open_editor(bool copy)
{
    if (selected_path.isEmpty()) return;
    ExtEditorWindow editor(this, e, selected_path, copy, all_versions());
    if (!editor.is_valid()) return;
    editor.exec();

    // "Run" in the editor loads the configuration the way OK here does
    if (!editor.run_file().isEmpty()) {
        emit load_config(editor.run_file(), ui->defaultCheck->isChecked());
        close();
        return;
    }
    if (editor.saved_file().isEmpty()) return;
    const QString saved = editor.saved_file();
    //Shown where it went: a copy saved to the user directory switches the
    //list over to it
    const QString user_dir = QDir::cleanPath(QString::fromStdString(e->user_ext_path));
    const bool in_user_dir = !user_dir.isEmpty() &&
        QDir::cleanPath(QFileInfo(saved).absolutePath()).startsWith(user_dir, Qt::CaseInsensitive);
    if (ui->userButton->isChecked() != in_user_dir) {
        QSignalBlocker blocker(ui->userButton);
        ui->userButton->setChecked(in_user_dir);
        e->write_setup("Startup", "show_user_configs", in_user_dir ? "1" : "0");
    }
    list_machines(QString::fromStdString(e->work_path));
    select_path(saved);
}

QStringList OpenConfigWindow::all_versions() const
{
    //Of the machines in both directories, whichever the list shows: a copy
    //must not take the name of one hidden by the switch
    QStringList r;
    MachinePaths paths;
    paths.computers_path = e->work_path;
    QStringList roots;
    roots << QString::fromStdString(e->work_path) << QString::fromStdString(e->user_ext_path);
    for (int i = 0; i < roots.size(); i++)
    {
        if (roots[i].isEmpty() || !QFileInfo(roots[i]).isDir()) continue;
        QDirIterator it(roots[i], QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            if (!is_machine_file(fi.fileName().toStdString())) continue;
            EmulatorConfig config;
            MachineSource source;
            if (!load_machine_description(fi.absoluteFilePath().toStdString(), paths, config, source, true)) continue;
            EmulatorConfigDevice * system = config.get_device("system");
            if (system != nullptr) r << QString::fromStdString(system->get_parameter("version", false).value);
        }
    }
    return r;
}

void OpenConfigWindow::on_userButton_toggled(bool checked)
{
    e->write_setup("Startup", "show_user_configs", checked ? "1" : "0");
    list_machines(QString::fromStdString(e->work_path));
}

void OpenConfigWindow::on_copyButton_clicked()
{
    open_editor(true);
}

void OpenConfigWindow::on_editButton_clicked()
{
    open_editor(false);
}

void OpenConfigWindow::on_deleteButton_clicked()
{
    const QString path = selected_path;
    const QString p = path.toLower();
    if (!p.endsWith(".ext") && !p.endsWith(".ext.zip")) return;
    QString version;
    const QModelIndexList selected = ui->treeView->selectionModel()->selectedIndexes();
    if (!selected.isEmpty()) version = selected.first().data(Qt::DisplayRole).toString();
    if (QMessageBox::question(this, tr("Delete the configuration"),
            tr("Delete the configuration \"%1\"?\n%2").arg(version, QDir::toNativeSeparators(path)))
        != QMessageBox::Yes) return;
    if (!QFile::remove(path)) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot delete the file") + " " + QDir::toNativeSeparators(path));
        return;
    }
    list_machines(QString::fromStdString(e->work_path));
}

void OpenConfigWindow::set_description(QModelIndex index)
{
    selected_path = index.data(Qt::UserRole).toString();
    update_buttons();
    if (!selected_path.isEmpty())
    {
        QString text_path = index.data(Qt::UserRole + 2).toString();
        //qDebug() << "DESCRIPTION:" << text_path;
        QFile file(text_path);
        if (!file.open(QIODevice::ReadOnly)) {
            ui->textBrowser->setPlainText(OpenConfigWindow::tr("No any description file found for this machine"));
            return;
        }

        QString html = "<body>" + QString::fromStdString(md2html(file.readAll().toStdString())) +"</body>";
        ui->textBrowser->document()->setHtml(html);

        file.close();
    }
}

void OpenConfigWindow::on_item_double_clicked(QModelIndex index)
{
    set_description(index);
    on_okButton_clicked();
}

void OpenConfigWindow::on_closeButton_clicked()
{
    close();
}


void OpenConfigWindow::on_debugCheck_toggled(bool checked)
{
    e->write_setup("Startup", "show_debug_versions", checked?"1":"0");
    list_machines(QString::fromStdString(e->work_path));
}


void OpenConfigWindow::on_okButton_clicked()
{
    if (!selected_path.isEmpty()) {
        emit load_config(selected_path, ui->defaultCheck->isChecked());
        close();
    }
}

