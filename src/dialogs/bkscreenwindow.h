// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Debug window of the БК screen, header

#pragma once

#include <utility>
#include <vector>

#include <QImage>
#include <QWidget>

#include "dialogs/genericdbgwnd.h"
#include "emulator/emulator.h"

class QComboBox;
class QLabel;
class QSpinBox;
class BKDisplay;

//The picture of a screen buffer, stretched to the window with its proportions
class BKScreenPicture : public QWidget
{
    Q_OBJECT
public:
    explicit BKScreenPicture(QWidget *parent);
    void set_image(const QImage &image);

signals:
    //The dot of the image under the mouse, or -1, -1 when it left the picture
    void hovered(int x, int y);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

private:
    QImage m_image;
    QRect target() const;
};

//Any 16 KB page of memory drawn as the БК screen: from the processor's space as
//it sees it now, or from any RAM device of the machine. A static
//picture - no beam, scroll or quarter screen, one palette for the whole frame -
//for programs that build the picture elsewhere and then copy it to the screen
class BKScreenWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    BKScreenWindow(QWidget *parent, Emulator * e, BKDisplay * d);

public slots:
    void update_view() override;

private:
    Emulator * e;
    BKDisplay * d;
    CPU * m_cpu;
    int m_base;

    QComboBox * m_source;
    QSpinBox * m_page;
    QLabel * m_where;
    QComboBox * m_mode;
    QComboBox * m_palette;
    QLabel * m_info;
    QLabel * m_status;
    BKScreenPicture * m_picture;

    unsigned int m_start;           //the first byte shown, in the source: the page start
    std::vector<uint8_t> m_bytes;   //the 16 KB taken at the last update
    unsigned int m_have;            //of them, the ones the source has
    unsigned int m_shown_mode;      //BK_MODE_* the picture was drawn in
    int m_hover_x;
    int m_hover_y;

    QString source_key() const;
    unsigned int source_size() const;
    void set_page_range();
    void set_page(int page);
    void show_where(bool board, const std::vector<std::pair<unsigned int, unsigned int>> &mapped);
    void show_hover();
    void load_choice();
    void save_choice();
};

GenericDbgWnd * CreateBKScreenWindow(QWidget *parent, Emulator * e, ComputerDevice * d);
