// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Configuration extension editor window, header

#pragma once

#include <QDialog>
#include <QStringList>
#include <QVector>

#include "emulator/config_fields.h"
#include "emulator/emulator.h"

class QCheckBox;
class QLineEdit;

namespace Ui {
class ExtEditorWindow;
}

// A table of the parameters a machine lets the user change, written as an
// .ext over its base. The window only shows the fields of ExtEditModel and
// hands the values back; building the text is the model's business.
class ExtEditorWindow : public QDialog
{
    Q_OBJECT

public:
    // path: a .cfg or an .ext to extend (copy = true), or an .ext to edit.
    // taken: versions of every machine in the list, for a unique "(n)"
    ExtEditorWindow(QWidget *parent, Emulator * e, const QString &path, bool copy, const QStringList &taken);
    ~ExtEditorWindow();

    bool is_valid() const { return m_valid; }
    // The file written by the last save, empty if nothing was saved
    QString saved_file() const { return m_saved; }
    // The file to load into the emulator, set when the window closed by "Run"
    QString run_file() const { return m_run; }

public slots:
    void reject() override;

private slots:
    void on_saveButton_clicked();
    void on_runButton_clicked();
    void on_cancelButton_clicked();

private:
    // A file field in the table
    struct FileRow {
        int         field;
        QLineEdit * edit;
        QCheckBox * embed;
        QString     picked;     // chosen in this session, absolute; empty if not
    };

    Ui::ExtEditorWindow * ui;
    Emulator *  e;
    ExtEditModel m_model;
    bool        m_valid = false;
    bool        m_dirty = false;
    QString     m_saved;
    QString     m_run;
    QLineEdit * m_file_edit = nullptr;
    QLineEdit * m_version_edit = nullptr;
    QVector<FileRow> m_files;

    void add_row(const QString &title, QWidget * value);
    void add_field(int index);
    void show_file(const FileRow &row);
    QString resolve_existing(const DeviceConfigField &f) const;
    bool save();
};
