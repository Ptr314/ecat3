// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: On-screen keyboard of the emulated machine

#include "keyboardwindow.h"

#ifdef HAVE_QT_SVG

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QFile>

#include "emulator/script/script_types.h"

//Highlight of a pressed key. Alpha only: the fill is the accent colour and the
//shape comes from the key itself, so it works on a keycap of any outline
#define KEY_HIGHLIGHT       QColor(255, 196, 0, 110)
//An indicator that is not lit. Black at 80% leaves a fifth of the original
//colour, which is what the browser reproduces with filter: brightness(0.2)
#define LED_OFF_SHADE       QColor(0, 0, 0, 204)
#define POLL_INTERVAL_MS    40
//Long enough that a drag never redraws the vector mid-flight, short enough that
//the blurry stretch is not noticed once the mouse stops
#define RERENDER_DELAY_MS   150
//Layout margin around the drawing, needed when the window height is derived
//from its width
#define MARGIN              4

KeyboardView::KeyboardView(QWidget *parent, Emulator *e):
      QWidget(parent)
    , m_e(e)
{
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(false);
    setMinimumWidth(320);

    m_rerender.setSingleShot(true);
    m_rerender.setInterval(RERENDER_DELAY_MS);
    connect(&m_rerender, &QTimer::timeout, this, &KeyboardView::rerender);
}

qreal KeyboardView::aspect() const
{
    if (m_viewbox.width() <= 0) return 0;
    return m_viewbox.height() / m_viewbox.width();
}

Keyboard * KeyboardView::kbd() const
{
    if (m_e == nullptr || m_e->dm == nullptr) return nullptr;
    return dynamic_cast<Keyboard*>(m_e->dm->get_device_by_name("keyboard", false));
}

// The box an element occupies in the coordinates the drawing is rendered in.
// boundsOnElement() leaves the transforms of the parents out on purpose, and
// Inkscape hangs one on every layer it makes - the keys of the Агат sit in a
// layer moved by 4.76 mm. transformForElement() is exactly that missing chain,
// and without it the highlight, and every click with it, lands millimetres off.
QRectF KeyboardView::doc_box(const QString &id) const
{
    return m_svg.transformForElement(id).mapRect(m_svg.boundsOnElement(id));
}

bool KeyboardView::load()
{
    m_boxes.clear();
    m_leds.clear();
    m_masks.clear();
    m_cache = QPixmap();
    m_shown.clear();
    m_dark.clear();
    m_lamp_keys.clear();
    m_mouse_key.clear();
    m_latched.clear();

    Keyboard * k = kbd();
    if (k == nullptr || k->picture_file().empty()) return false;
    m_reset_seen = k->reset_count();

    const QString file = QString::fromStdString(k->picture_file());
    if (!QFile::exists(file) || !m_svg.load(file) || !m_svg.isValid()) return false;

    m_viewbox = m_svg.viewBoxF();
    if (m_viewbox.width() <= 0 || m_viewbox.height() <= 0) return false;

    //The drawing is asked about the keys the machine actually has, so a picture
    //shared between machines lights up only what the current one carries
    const std::vector<std::string> &ids = k->key_ids();
    for (size_t i = 0; i < ids.size(); i++) {
        const QString id = QString::fromStdString(ids[i]);
        if (!m_svg.elementExists(id)) continue;
        const QRectF b = doc_box(id);
        if (b.isEmpty()) continue;
        //A stroke bleeds half its width past the page edge, so the centre is
        //what has to be inside: a key whose middle is outside means the drawing
        //and the renderer disagree about coordinates, and every hit would miss
        if (!m_viewbox.contains(b.center())) continue;
        KeyBox kb;
        kb.id = id;
        kb.box = b;
        m_boxes.append(kb);
    }
    if (m_boxes.isEmpty()) return false;

    //Lamps are optional and never clickable, so a drawing without them costs
    //nothing here - which is why the БК picture is unaffected
    const std::vector<Keyboard::Indicator> leds = k->indicators();
    for (size_t i = 0; i < leds.size(); i++) {
        const QString id = QString::fromStdString(leds[i].id);
        if (!m_svg.elementExists(id)) continue;
        const QRectF b = doc_box(id);
        if (b.isEmpty() || !m_viewbox.contains(b.center())) continue;
        KeyBox kb;
        kb.id = id;
        kb.box = b;
        m_leds.append(kb);
        //The lamp says which alphabet is on, so the key stops saying it too
        const QString key = QString::fromStdString(leds[i].key);
        if (!key.isEmpty() && !m_lamp_keys.contains(key)) m_lamp_keys.append(key);
    }

    //So that the first frame already shows the right lamp burning
    poll();
    return true;
}

QStringList KeyboardView::leds_dark() const
{
    QStringList r;
    Keyboard * k = kbd();
    if (k == nullptr) return r;
    const std::vector<Keyboard::Indicator> leds = k->indicators();
    for (size_t i = 0; i < leds.size(); i++)
        if (!leds[i].lit) r.append(QString::fromStdString(leds[i].id));
    return r;
}

QSize KeyboardView::sizeHint() const
{
    if (m_viewbox.width() <= 0) return QSize(640, 240);
    return QSize(900, int(900.0 * m_viewbox.height() / m_viewbox.width()));
}

int KeyboardView::heightForWidth(int w) const
{
    if (m_viewbox.width() <= 0) return w / 3;
    return int(w * m_viewbox.height() / m_viewbox.width());
}

QRectF KeyboardView::map_box(const QRectF &box) const
{
    if (m_viewbox.width() <= 0) return QRectF();
    const qreal sx = m_target.width() / m_viewbox.width();
    const qreal sy = m_target.height() / m_viewbox.height();
    return QRectF(m_target.x() + (box.x() - m_viewbox.x()) * sx,
                  m_target.y() + (box.y() - m_viewbox.y()) * sy,
                  box.width() * sx, box.height() * sy);
}

// A logical coordinate is not a pixel: under a fractional scale of the screen
// (125% or 150% on Windows) it falls between two device pixels, and a pixmap
// drawn there is resampled - which is what blurred the lettering even when the
// cache itself was sharp.
QPointF KeyboardView::snap(const QPointF &p) const
{
    return QPointF(qRound(p.x() * m_dpr) / m_dpr, qRound(p.y() * m_dpr) / m_dpr);
}

// Size and corner are rounded in device pixels, so that the cache rendered at
// this size is copied onto the screen one to one.
void KeyboardView::layout_target()
{
    const qreal k = qMin(width() / m_viewbox.width(), height() / m_viewbox.height());
    const qreal w = qRound(m_viewbox.width() * k * m_dpr) / m_dpr;
    const qreal h = qRound(m_viewbox.height() * k * m_dpr) / m_dpr;
    m_target = QRectF(snap(QPointF((width() - w) / 2.0, (height() - h) / 2.0)), QSizeF(w, h));
}

void KeyboardView::resizeEvent(QResizeEvent *)
{
    if (m_viewbox.width() <= 0) return;

    m_dpr = devicePixelRatioF();
    layout_target();

    // Redrawing the whole picture costs 20 ms at 900 px and 44 ms at 2400 px,
    // and a drag fires resize events all the way. So the old pixmap is stretched
    // to the new size while the mouse moves - blurry but free - and the vector
    // is redrawn once, when the dragging stops.
    m_masks.clear();    // one key is under a millisecond, no point deferring those
    if (!m_cache.isNull()) m_rerender.start();
}

void KeyboardView::rerender()
{
    m_cache = QPixmap();
    update();
}

// The silhouette of one element: it is rendered alone, its alpha turned into a
// solid stencil and filled with the given colour. Thresholding the alpha
// matters -- a gradient stop or a fill-opacity below 1 would otherwise wash the
// overlay out exactly where the element is most transparent.
// The cache is keyed by id alone: a key is only ever drawn with the highlight
// and a lamp only ever with the shade, so one id means one colour.
const QPixmap & KeyboardView::mask_of(const QString &id, const QRectF &target, const QColor &colour)
{
    QMap<QString, QPixmap>::iterator it = m_masks.find(id);
    if (it != m_masks.end()) return it.value();

    //In device pixels, like the cache: a silhouette made at the logical size
    //would be stretched over a sharp key and soften its edge
    const QSize size = (target.size() * m_dpr).toSize().expandedTo(QSize(1, 1));
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        m_svg.render(&p, id, QRectF(QPointF(0, 0), QSizeF(size)));
    }

    QImage stencil(size, QImage::Format_ARGB32_Premultiplied);
    stencil.fill(Qt::transparent);
    for (int y = 0; y < size.height(); y++) {
        const QRgb * src = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        QRgb * dst = reinterpret_cast<QRgb*>(stencil.scanLine(y));
        for (int x = 0; x < size.width(); x++)
            if (qAlpha(src[x]) > 0) dst[x] = 0xFF000000u;
    }
    {
        QPainter p(&stencil);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(stencil.rect(), colour);
    }

    QPixmap mask = QPixmap::fromImage(stencil);
    mask.setDevicePixelRatio(m_dpr);
    return m_masks.insert(id, mask).value();
}

void KeyboardView::paintEvent(QPaintEvent *)
{
    if (m_boxes.isEmpty() || m_target.isEmpty()) return;

    //The window went to a screen of another scale: everything rendered so far
    //was made for the old one
    const qreal dpr = devicePixelRatioF();
    if (dpr != m_dpr) {
        m_dpr = dpr;
        layout_target();
        m_masks.clear();
        m_cache = QPixmap();
    }

    //The vector is rendered at the resolution of the screen, not at the logical
    //size: with Qt 6 a scale of 125% or 150% reaches the widget as it is, and
    //a logical-size cache was stretched by that much on every paint
    const QSize device = (m_target.size() * m_dpr).toSize();
    QPainter p(this);
    if (m_cache.isNull()) {
        m_cache = QPixmap(device);
        m_cache.setDevicePixelRatio(m_dpr);
        m_cache.fill(Qt::transparent);
        QPainter cp(&m_cache);
        cp.setRenderHint(QPainter::Antialiasing, true);
        m_svg.render(&cp, QRectF(QPointF(0, 0), m_target.size()));
    }
    if (m_cache.size() == device) {
        //Copied one to one onto a corner snapped to the pixel grid: nothing is
        //resampled, so the lettering is as sharp as the renderer made it
        p.drawPixmap(m_target.topLeft(), m_cache);
    } else {
        //While a drag is in flight the cache is still the size it had before,
        //and stretching it is what keeps the drag smooth
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(m_target, m_cache, QRectF(m_cache.rect()));
    }

    for (int i = 0; i < m_boxes.size(); i++)
        if (m_shown.contains(m_boxes[i].id)) {
            const QRectF t = map_box(m_boxes[i].box);
            p.drawPixmap(snap(t.topLeft()), mask_of(m_boxes[i].id, t, KEY_HIGHLIGHT));
        }

    //A lamp that is not lit is blacked out; the burning one is left as drawn
    for (int i = 0; i < m_leds.size(); i++)
        if (m_dark.contains(m_leds[i].id)) {
            const QRectF t = map_box(m_leds[i].box);
            p.drawPixmap(snap(t.topLeft()), mask_of(m_leds[i].id, t, LED_OFF_SHADE));
        }
}

QString KeyboardView::key_at(const QPoint &pos) const
{
    for (int i = 0; i < m_boxes.size(); i++)
        if (map_box(m_boxes[i].box).contains(pos)) return m_boxes[i].id;
    return QString();
}

void KeyboardView::press(const QString &id, bool down)
{
    if (id.isEmpty()) return;
    const std::string sid = id.toStdString();
    m_e->key_event_id(sid, down);

    //KEYDOWN/KEYUP resolve a native id against the loaded machine, so a click
    //on the drawing replays exactly as it happened
    std::vector<std::string> args;
    args.push_back(sid);
    m_e->record_verb(down ? SCRIPT_CMD_KEYDOWN : SCRIPT_CMD_KEYUP, args);
}

void KeyboardView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    Keyboard * k = kbd();
    if (k == nullptr) return;

    const QString id = key_at(event->pos());
    if (id.isEmpty()) return;

    switch (k->click_mode(id.toStdString())) {
        case Keyboard::CLICK_TAP:
            press(id, true);
            press(id, false);
            break;
        case Keyboard::CLICK_TOGGLE: {
            const int at = m_latched.indexOf(id);
            if (at >= 0) {
                m_latched.removeAt(at);
                press(id, false);
            } else {
                m_latched.append(id);
                press(id, true);
            }
            break;
        }
        default:
            m_mouse_key = id;
            press(id, true);
            break;
    }
    poll();
}

void KeyboardView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (!m_mouse_key.isEmpty()) {
        press(m_mouse_key, false);
        m_mouse_key.clear();
        poll();
    }
}

void KeyboardView::release_all()
{
    if (!m_mouse_key.isEmpty()) {
        press(m_mouse_key, false);
        m_mouse_key.clear();
    }
    for (int i = 0; i < m_latched.size(); i++) press(m_latched[i], false);
    m_latched.clear();
}

void KeyboardView::poll()
{
    Keyboard * k = kbd();
    if (k == nullptr) return;

    //The machine has been reset since the last look: it is holding nothing any
    //more, so the latches drawn here are stale. They are dropped rather than
    //released - there is nothing left to release, and sending one would press
    //the modifier back on. This is what kept a cold restart from working
    //without closing the window: СУ stayed engaged in the keyboard while the
    //picture showed it free.
    const unsigned int seq = k->reset_count();
    if (seq != m_reset_seen) {
        m_reset_seen = seq;
        m_latched.clear();
        m_mouse_key.clear();
    }

    const std::vector<std::string> held = k->ids_held();
    QStringList now;
    for (size_t i = 0; i < held.size(); i++) {
        const QString id = QString::fromStdString(held[i]);
        //A key whose lamp is drawn is left alone: the register is on the
        //picture once. keyboard.pressed still reports it - what the machine
        //holds does not depend on what the drawing shows
        if (m_lamp_keys.contains(id)) continue;
        now.append(id);
    }
    const QStringList dark = leds_dark();

    //Repaint only on a change: this runs 25 times a second
    if (now == m_shown && dark == m_dark) return;
    m_shown = now;
    m_dark = dark;
    update();
}

//----------------------------------------------------------------------------

KeyboardWindow::KeyboardWindow(QWidget *parent, Emulator *e):
      GenericDbgWnd(parent)
    , m_e(e)
{
    setWindowTitle(tr("Keyboard"));
    setSizeGripEnabled(true);

    m_view = new KeyboardView(this, e);
    m_valid = m_view->load();

    QVBoxLayout * layout = new QVBoxLayout(this);
    layout->setContentsMargins(MARGIN, MARGIN, MARGIN, MARGIN);
    layout->addWidget(m_view);
    setLayout(layout);

    //The width the user left it at last time. Height follows from the drawing,
    //so only the width is worth keeping
    //The window's own minimum is not known until the layout runs, so the view's
    //is what an obviously bad saved value is checked against
    const int saved = QString::fromStdString(
        m_e->read_setup("Keyboard", "width", "0")).toInt();
    const int w = (saved >= m_view->minimumWidth() + 2 * MARGIN)
                    ? saved
                    : m_view->sizeHint().width() + 2 * MARGIN;
    resize(w, height_for_window_width(w));

    connect(&m_timer, &QTimer::timeout, m_view, &KeyboardView::poll);
    m_timer.start(POLL_INTERVAL_MS);
}

void KeyboardWindow::reload()
{
    m_valid = m_view->load();
    if (m_valid) resize(width(), height_for_window_width(width()));
    m_view->update();
}

int KeyboardWindow::height_for_window_width(int w) const
{
    const qreal a = m_view->aspect();
    if (a <= 0) return height();
    return int((w - 2 * MARGIN) * a) + 2 * MARGIN;
}

// The drawing has one shape, so the window keeps it: letting it be stretched
// only adds empty bands above and below the keyboard.
void KeyboardWindow::resizeEvent(QResizeEvent *event)
{
    GenericDbgWnd::resizeEvent(event);

    if (!m_valid || m_fixing_aspect) return;
    const int want = height_for_window_width(width());
    if (qAbs(height() - want) <= 1) return;

    m_fixing_aspect = true;         // the resize below comes back here
    resize(width(), want);
    m_fixing_aspect = false;
}

void KeyboardWindow::closeEvent(QCloseEvent *event)
{
    m_timer.stop();
    m_view->release_all();
    m_e->write_setup("Keyboard", "width", std::to_string(width()));
    GenericDbgWnd::closeEvent(event);
}

// The dialog takes the keyboard focus away from the main window, so it has to
// feed the machine itself -- otherwise typing stops the moment it is opened.
void KeyboardWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) { event->ignore(); return; }

    m_e->key_event(event->key(), event->modifiers(), true);
    if (event->key() == EmuKey::Cancel) {
        std::vector<std::string> args;
        args.push_back((event->modifiers() & Qt::AltModifier)?"cold":"soft");
        m_e->record_verb(SCRIPT_CMD_RESET, args);
    } else {
        m_e->record_key(static_cast<unsigned int>(event->key()), event->nativeScanCode(), true);
    }
}

void KeyboardWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) { event->ignore(); return; }

    m_e->key_event(event->key(), event->modifiers(), false);
    m_e->record_key(static_cast<unsigned int>(event->key()), event->nativeScanCode(), false);
}

#endif // HAVE_QT_SVG
