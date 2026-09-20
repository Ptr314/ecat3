// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration extension editor window, source

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>

#include "exteditorwindow.h"
#include "ui_exteditorwindow.h"
#include "genericdbgwnd.h"
#include "emulator/utils.h"
#include "dsk_tools/dsk_tools.h"
// MSVC resolves the "utils.h" inside dsk_tools.h against the includer's
// directory; the path helpers are in the real one
#include "libs/dsk_tools/src/utils.h"

namespace {

// Field titles come from two contexts: the fields of their own, and the
// device options whose names they share
QString field_title(const std::string &s)
{
    const QString t = QCoreApplication::translate("ConfigFields", s.c_str());
    if (t != QString::fromStdString(s)) return t;
    return QCoreApplication::translate("DeviceOptions", s.c_str());
}

QLineEdit * read_only_edit(const QString &text)
{
    QLineEdit * edit = new QLineEdit(text);
    edit->setReadOnly(true);
    edit->setFrame(false);
    return edit;
}

QToolButton * small_button(const QString &icon, const QString &tip)
{
    QToolButton * b = new QToolButton();
    b->setIcon(QIcon(icon));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

ExtEditorWindow::ExtEditorWindow(QWidget *parent, Emulator * e, const QString &path, bool copy, const QStringList &taken) :
    QDialog(parent),
    ui(new Ui::ExtEditorWindow),
    e(e)
{
    ui->setupUi(this);
    ui->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->table->setSelectionMode(QAbstractItemView::NoSelection);
    ui->table->setFocusPolicy(Qt::NoFocus);
    ui->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    MachinePaths paths;
    paths.computers_path = e->work_path;
    paths.cache_path = e->cache_path;
    emulator::Result res = m_model.open(path.toStdString(), paths);
    if (!res) {
        QMessageBox::warning(parent, tr("Error"), translateResultMessage(res.message));
        return;
    }
    m_valid = true;

    // A copy is a new file: it has no name until it is saved, and a version
    // of its own
    if (copy) {
        m_model.file.clear();
        //A copy belongs to the user, whatever the original said
        m_model.ext.is_protected = false;
        std::vector<std::string> used;
        for (int i = 0; i < taken.size(); i++) used.push_back(taken[i].toStdString());
        m_model.version = unique_version(m_model.version, used);
    }

    add_row(tr("Base"), read_only_edit(QString::fromStdString(m_model.extends)));
    m_file_edit = read_only_edit(QDir::toNativeSeparators(QString::fromStdString(m_model.file)));
    add_row(tr("File name"), m_file_edit);
    m_version_edit = new QLineEdit(QString::fromStdString(m_model.version));
    connect(m_version_edit, &QLineEdit::textEdited, this, [this]() { m_dirty = true; });
    add_row(tr("Version"), m_version_edit);

    for (size_t i = 0; i < m_model.fields.size(); i++) add_field(static_cast<int>(i));

    // The @script part, in the syntax of an .ecat file (docs/SCRIPTING.md)
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    ui->scriptEdit->setFont(mono);
    ui->scriptEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    ui->scriptEdit->setPlainText(QString::fromStdString(m_model.ext.script));
    connect(ui->scriptEdit, &QPlainTextEdit::textChanged, this, [this]() { m_dirty = true; });
}

ExtEditorWindow::~ExtEditorWindow()
{
    delete ui;
}

void ExtEditorWindow::add_row(const QString &title, QWidget * value)
{
    const int row = ui->table->rowCount();
    ui->table->insertRow(row);
    QTableWidgetItem * item = new QTableWidgetItem(title);
    item->setFlags(Qt::ItemIsEnabled);
    ui->table->setItem(row, 0, item);
    ui->table->setCellWidget(row, 1, value);
}

void ExtEditorWindow::add_field(int index)
{
    DeviceConfigField &f = m_model.fields[index];
    const QString title = QString::fromStdString(f.device) + ": " + field_title(f.field.title);

    if (f.field.type == CONFIG_FIELD_CHOICE)
    {
        QComboBox * combo = new QComboBox();
        const std::string current = f.present ? f.value : f.field.def;
        int selected = -1;
        for (size_t i = 0; i < f.field.values.size(); i++) {
            combo->addItem(field_title(f.field.values[i].title), QString::fromStdString(f.field.values[i].value));
            if (f.field.values[i].value == current) selected = static_cast<int>(i);
        }
        // A value the device does not list is shown as it is written
        if (selected < 0 && !current.empty()) {
            combo->addItem(QString::fromStdString(current), QString::fromStdString(current));
            selected = combo->count() - 1;
        }
        combo->setCurrentIndex(selected);
        connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                [this, index, combo](int i) {
                    DeviceConfigField &f = m_model.fields[index];
                    f.present = true;
                    f.value = combo->itemData(i).toString().toStdString();
                    f.extended.clear();
                    m_dirty = true;
                });
        add_row(title, combo);
        return;
    }

    if (f.field.type == CONFIG_FIELD_STRING)
    {
        QLineEdit * edit = new QLineEdit(f.present ? QString::fromStdString(f.value) : QString());
        edit->setPlaceholderText(QString::fromStdString(f.field.def));
        connect(edit, &QLineEdit::textEdited, this, [this, index](const QString &text) {
            DeviceConfigField &f = m_model.fields[index];
            // Empty means the parameter is not written at all
            f.present = !text.isEmpty();
            f.value = text.toStdString();
            f.extended.clear();
            m_dirty = true;
        });
        add_row(title, edit);
        return;
    }

    // CONFIG_FIELD_FILE: the name is chosen, never typed
    QWidget * box = new QWidget();
    QHBoxLayout * layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    FileRow row;
    row.field = index;
    row.edit = read_only_edit(QString());
    QToolButton * pick = small_button(":/icons/rec_open", tr("Select a file"));
    QToolButton * clear = small_button(":/icons/clear", tr("Clear the field"));
    row.embed = new QCheckBox(tr("Embed"));
    row.embed->setToolTip(tr("Store the file inside the configuration"));
    row.embed->setChecked(f.is_inline());
    // Some files are only ever referred to (a hard disk the machine writes)
    row.embed->setVisible(f.field.embeddable);
    layout->addWidget(row.edit, 1);
    layout->addWidget(pick);
    layout->addWidget(clear);
    layout->addWidget(row.embed);

    const int n = m_files.size();
    m_files.push_back(row);
    show_file(m_files[n]);

    connect(pick, &QToolButton::clicked, this, [this, n]() {
        FileRow &r = m_files[n];
        DeviceConfigField &f = m_model.fields[r.field];
        const QString name = QFileDialog::getOpenFileName(this, tr("Select a file"),
            QString::fromStdString(e->get_last_path()), QString::fromStdString(f.field.files));
        if (name.isEmpty()) return;
        r.picked = QFileInfo(name).absoluteFilePath();
        f.present = true;
        f.value = QDir::fromNativeSeparators(r.picked).toStdString();
        f.extended.clear();
        m_dirty = true;
        show_file(r);
    });

    connect(clear, &QToolButton::clicked, this, [this, n]() {
        FileRow &r = m_files[n];
        DeviceConfigField &f = m_model.fields[r.field];
        r.picked.clear();
        f.present = false;
        f.value.clear();
        f.extended.clear();
        QSignalBlocker blocker(r.embed);
        r.embed->setChecked(false);
        m_dirty = true;
        show_file(r);
    });

    connect(row.embed, &QCheckBox::toggled, this, [this, n](bool checked) {
        FileRow &r = m_files[n];
        DeviceConfigField &f = m_model.fields[r.field];
        m_dirty = true;
        // Data stored in the file has no file behind it to refer to instead
        if (!checked && f.is_inline() && r.picked.isEmpty()) {
            if (QMessageBox::question(this, tr("Embedded file"),
                    tr("The file is stored inside the configuration and there is nothing to refer to instead. Clear the field?"))
                == QMessageBox::Yes) {
                f.present = false;
                f.value.clear();
                f.extended.clear();
                show_file(r);
            } else {
                QSignalBlocker blocker(r.embed);
                r.embed->setChecked(true);
            }
        }
    });

    add_row(title, box);
}

void ExtEditorWindow::show_file(const FileRow &row)
{
    const DeviceConfigField &f = m_model.fields[row.field];
    QString text;
    if (!row.picked.isEmpty())
        text = QDir::toNativeSeparators(row.picked);
    else if (f.present)
        text = QString::fromStdString(f.value);
    if (f.present && f.is_inline() && row.picked.isEmpty())
        text += " " + tr("[embedded]");
    row.edit->setText(text);
}

QString ExtEditorWindow::resolve_existing(const DeviceConfigField &f) const
{
    // Where the emulator itself would look for the file when loading
    SystemData sd;
    sd.system_path = dsk_tools::get_file_path(m_model.base_cfg);
    if (!m_model.file.empty()) sd.ext_path = dsk_tools::get_file_path(m_model.file);
    sd.software_path = e->software_path;
    sd.data_path = e->data_path;
    return QString::fromStdString(find_file_location(&sd, f.value));
}

bool ExtEditorWindow::save()
{
    const QString version = m_version_edit->text().trimmed();
    if (version.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("The version must not be empty"));
        return false;
    }

    QString path = QString::fromStdString(m_model.file);
    if (path.isEmpty())
    {
        QMessageBox box(QMessageBox::Question, tr("Save the configuration"),
                        tr("Where should the new configuration be saved?"), QMessageBox::NoButton, this);
        QPushButton * base_button = box.addButton(tr("Next to the base configuration"), QMessageBox::AcceptRole);
        QPushButton * user_button = box.addButton(tr("To the user directory"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();

        QString dir;
        if (box.clickedButton() == base_button)
            dir = QFileInfo(QString::fromStdString(m_model.base_cfg)).absolutePath();
        else if (box.clickedButton() == user_button)
            dir = QString::fromStdString(e->user_ext_path);
        else
            return false;
        if (!QDir().mkpath(dir)) {
            QMessageBox::warning(this, tr("Error"), tr("Cannot create the directory") + " " + dir);
            return false;
        }
        if (!dir.endsWith('/')) dir += '/';
        path = QString::fromStdString(unique_ext_file(dir.toStdString(),
                   dsk_tools::get_file_basename(m_model.base_cfg)));
    }
    const QDir dir = QFileInfo(path).absoluteDir();

    for (int i = 0; i < m_files.size(); i++)
    {
        FileRow &r = m_files[i];
        DeviceConfigField &f = m_model.fields[r.field];
        if (r.embed->isChecked())
        {
            // A new file, or the one the field refers to now
            QString source = r.picked;
            if (source.isEmpty() && f.present && !f.is_inline()) {
                source = resolve_existing(f);
                if (source.isEmpty()) {
                    QMessageBox::warning(this, tr("Error"), tr("File not found") + ": " + QString::fromStdString(f.value));
                    return false;
                }
            }
            if (!source.isEmpty()) {
                emulator::Result res = ExtEditModel::embed_file(f, source.toStdString());
                if (!res) {
                    QMessageBox::warning(this, tr("Error"), translateResultMessage(res.message));
                    return false;
                }
                r.picked.clear();
            }
        }
        else if (!r.picked.isEmpty())
        {
            // Relative when the file lies next to the configuration: the two
            // can then be moved together
            const QString rel = dir.relativeFilePath(r.picked);
            const QString value = (rel.startsWith("..") || QDir::isAbsolutePath(rel))
                ? QDir::fromNativeSeparators(r.picked) : rel;
            f.value = value.toStdString();
            f.extended.clear();
        }
    }

    m_model.version = version.toStdString();
    // Nothing but blank lines is no script at all: @script is not written
    const QString script = ui->scriptEdit->toPlainText();
    m_model.ext.script = script.trimmed().isEmpty() ? std::string() : script.toStdString();
    const std::string text = m_model.build();

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        out.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())) {
        QMessageBox::warning(this, tr("Error"), tr("Cannot write the file") + " " + path);
        return false;
    }
    out.close();

    m_model.file = path.toStdString();
    m_file_edit->setText(QDir::toNativeSeparators(path));
    m_saved = path;
    m_dirty = false;
    for (int i = 0; i < m_files.size(); i++) show_file(m_files[i]);
    return true;
}

void ExtEditorWindow::on_saveButton_clicked()
{
    save();
}

void ExtEditorWindow::on_runButton_clicked()
{
    // The emulator loads the file, so what is on the screen has to be in it
    if (m_dirty || m_model.file.empty())
    {
        if (QMessageBox::question(this, tr("Configuration editor"),
                tr("The configuration has to be saved before it runs. Save it?"),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Save) != QMessageBox::Save)
            return;
        if (!save()) return;
    }
    m_run = QString::fromStdString(m_model.file);
    QDialog::accept();
}

void ExtEditorWindow::on_cancelButton_clicked()
{
    reject();
}

void ExtEditorWindow::reject()
{
    if (m_dirty)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Configuration editor"),
            tr("The configuration has been changed. Save it?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save && !save()) return;
    }
    QDialog::reject();
}
