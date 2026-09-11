// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: On-screen keyboard of the emulated machine

#pragma once

#ifdef HAVE_QT_SVG

#include <QWidget>
#include <QTimer>
#include <QPixmap>
#include <QRectF>
#include <QMap>
#include <QSvgRenderer>

#include "emulator/emulator.h"
#include "dialogs/genericdbgwnd.h"

// The drawing of the machine's own keyboard. Keys are elements of the SVG
// whose id is the same name the machine's native key table uses, so the
// picture needs no separate table of its own.
class KeyboardView : public QWidget
{
    Q_OBJECT
public:
    KeyboardView(QWidget *parent, Emulator *e);

    bool load();                            //false: no picture, bad file or no key elements
    QSize sizeHint() const override;
    int heightForWidth(int w) const override;
    qreal aspect() const;                   //height/width of the drawing, 0 when there is none
    void release_all();

public slots:
    void poll();                            //repaints when the set of held keys changed
    void rerender();                        //redraws the vector after a resize has settled

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    struct KeyBox {
        QString id;
        QRectF  box;                        //in viewBox coordinates
    };

    Keyboard * kbd() const;                 //looked up every time: a machine change deletes it
    QString key_at(const QPoint &p) const;
    void press(const QString &id, bool down);
    QRectF map_box(const QRectF &box) const;
    QRectF doc_box(const QString &id) const;
    QPointF snap(const QPointF &p) const;   //to the device pixel grid
    void layout_target();                   //m_target from the widget size and m_dpr
    const QPixmap & mask_of(const QString &id, const QRectF &target, const QColor &colour);
    QStringList leds_dark() const;          //indicators that are not lit right now

    Emulator *  m_e;
    QSvgRenderer m_svg;
    QRectF      m_viewbox;
    QRectF      m_target;                   //aspect-fit area of the drawing inside the widget
    qreal       m_dpr = 1;                  //device pixels per logical one the cache is made for
    QVector<KeyBox> m_boxes;
    QVector<KeyBox> m_leds;                 //indicator lamps, same shape as a key box
    QPixmap     m_cache;                    //the whole drawing, possibly at the previous size
    QTimer      m_rerender;                 //fires once the resizing has stopped
    QMap<QString, QPixmap> m_masks;         //highlight silhouettes, dropped on resize
    QStringList m_shown;                    //what the last repaint highlighted
    QStringList m_dark;                     //what the last repaint shaded out
    QStringList m_lamp_keys;                //keys whose state a lamp already shows
    QString     m_mouse_key;                //held by the mouse, empty when none
    QStringList m_latched;                  //modifiers clicked on, held until clicked again
    unsigned int m_reset_seen = 0;          //Keyboard::reset_count() at the last poll
};

class KeyboardWindow : public GenericDbgWnd
{
    Q_OBJECT
public:
    KeyboardWindow(QWidget *parent, Emulator *e);

    bool valid() const { return m_valid; }
    void reload();                          //after a machine change

protected:
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

private:
    int height_for_window_width(int w) const;

    Emulator *     m_e;
    KeyboardView * m_view;
    QTimer         m_timer;
    bool           m_valid;
    bool           m_fixing_aspect = false;   //guards the resize() inside resizeEvent()
};

#endif // HAVE_QT_SVG
