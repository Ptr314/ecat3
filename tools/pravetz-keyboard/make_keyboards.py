# Клавиатуры Правец на экране: рисунки с текстом (src/pics/*_font.svg, в
# deploy/computers/pravetz/files они лежат без _font и в кривых, это делается
# вручную) и таблицы клавиш рисунка (deploy/computers/pravetz/*.keys).
#
# Расположение клавиш - по фотографиям с agatcomp.ru (docs-external/pravetz/
# keyboards): 8А для СССР (ГОСТ) и 8С (БДС) - клавиатура как у PC, 8А
# болгарский (ЯВЕРТЪ и БДС) - своя, ступенями (KEY8A1), 82 и 8М - свои. Надписи
# не переписаны с фотографий, а взяты из таблиц .map: клавиша рисунка - это
# клавиша хоста (с Shift рисунка - другая, в кириллице - третья), и на ней
# написано то, что она пошлет. Поэтому рисунок не расходится с эмуляцией.
#
#   python make_keyboards.py           (из любого каталога)
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PRAVETZ = os.path.normpath(os.path.join(HERE, '..', '..', 'deploy', 'computers', 'pravetz'))
PICS = os.path.normpath(os.path.join(HERE, '..', '..', 'src', 'pics'))

ALIAS = {'minus': '-', 'slash': '/', 'semicolon': ';', 'colon': ':', 'plus': '+',
         'circum': '^', 'under': '_', 'mult': '*', 'enter': 'ret'}


def host(name):
    return ALIAS.get(name.lower(), name if len(name) == 1 else name.lower())


def load_map(name):
    m = {}
    for line in open(os.path.join(PRAVETZ, name), encoding='utf-8'):
        line = line.split('//')[0].strip()
        if not line or line.startswith('@include') or line.startswith('@autorepeat'):
            continue
        left, value = line.split(':', 1) if not line.startswith(':') else (':', line[1:].split(':', 1)[1])
        left = left.strip()
        mods = ''
        if '/' in left[1:]:
            i = left.index('/', 1)
            left, mods = left[:i], left[i + 1:].upper()
        v = value.strip()
        code = int(v[1:], 16) if v.startswith('$') else int(v)
        m[(host(left), 'S' in mods, 'R' in mods, '8' in mods)] = code
    return m


def find(m, key, shift, rus):
    # PravetzKeyboard::find(): Shift as asked, then the other; the alphabet
    # and the 7-bit table falling back to the plain entry
    for s in (shift, not shift):
        for r in ((True, False) if rus else (False,)):
            if (key, s, r, False) in m:
                return m[(key, s, r, False)]
    return None


KOI7 = 'ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЪЗШЭЩЧ'   # $E0-$FE, у Правец $F9 = Ъ


def char(code, alias82=False):
    if code is None or code == 0:
        return ''
    c = code & 0x7F
    if alias82 and c in (0x40, 0x5B, 0x5D, 0x5E):
        return {0x40: 'Ю', 0x5B: 'Ш', 0x5D: 'Щ', 0x5E: 'Ч'}[c]
    if code >= 0xE0 and code < 0xFF:
        return KOI7[code - 0xE0]
    if 0x20 < c < 0x7F:
        return chr(c)
    return ''


# Клавиша: id, ширина в клавишах, клавиша хоста, она же с Shift рисунка,
# в кириллице, в кириллице с Shift; label - надпись функциональной клавиши
class K:
    def __init__(self, kid, w=1.0, plain=None, shift=None, rplain=None, rshift=None, label=None,
                 role=None, fn=None, color=None, gap=0.0):
        self.id, self.w, self.plain, self.shift = kid, w, plain, shift
        self.rplain, self.rshift, self.label, self.role, self.fn = rplain, rshift, label, role, fn
        self.color, self.gap = color, gap


def letters(s):
    return [K('key_' + c.lower(), 1, c) for c in s]


def pc_keys(caps=True):
    """8А для СССР и 8С: клавиатура как у PC"""
    return [
        [K('key_esc', 1, 'esc', label='Esc')]
        + [K('key_' + d, 1, d, s) for d, s in zip('1234567890', '!@#$%^&*()')]
        + [K('key_minus', 1, '-', '_'), K('key_equal', 1, '=', '+'), K('key_backslash', 1, '\\', 'bar'),
           K('key_reset', 1, fn='reset', label='Reset')],
        [K('key_tab', 1.5, 'tab', label='Tab')] + letters('QWERTYUIOP')
        + [K('key_lbracket', 1, '[', '{'), K('key_rbracket', 1, ']', '}'), K('key_del', 1.5, 'del', label='Del')],
        [K('key_ctrl', 1.75, role='ctrl', label='Ctrl')] + letters('ASDFGHJKL')
        + [K('key_semicolon', 1, ';', ':'), K('key_quote', 1, "'", '"'), K('key_ret', 2.25, 'ret', label='Return')],
        [K('key_shift_l', 2.25, role='shift', label='Shift')] + letters('ZXCVBNM')
        + [K('key_comma', 1, ',', '<'), K('key_period', 1, '.', '>'), K('key_slash', 1, '/', '?'),
           K('key_shift_r', 2.75, role='shift', label='Shift')],
        [K('key_caps', 1, fn='caps', label='Caps|Lock'), K('key_cl', 1, fn='cl', label='C/L|Lock'),
         K('key_f1', 1, fn='f1', label='F1'), K('key_space', 7, 'space'), K('key_f2', 1, fn='f2', label='F2'),
         K('key_left', 1, 'left', label='←'), K('key_right', 1, 'right', label='→'),
         K('key_up', 1, 'up', label='↑'), K('key_down', 1, 'down', label='↓')],
    ]


def a8_keys(bds=False):
    """8А болгарский (KEY8A1): ряды 0.53, 1.07, 0.59 и 1.09 клавиши от края, 65
    клавиш, одна внизу слева без надписи. На фото правая нижняя стрелка несет
    колпачок второй "вправо", это ошибка сборки: там "вниз".

    БДС на этой форме не снят: латиница та же, что у ЯВЕРТЪ, а кириллица стоит на
    тех местах, где она у 8С, поэтому знаки верхнего ряда заданы здесь явно:
    иначе клавиша взяла бы кириллицу своего слота с Shift (" с Shift - это Ч)"""
    def b(**kw):
        return kw if bds else {}
    return [
        [K('key_esc', 1, 'esc', label='ESC')]
        + [K('key_' + d, 1, d, s, **b(rshift=r))
           for d, s, r in zip('123456789', '!"#$%&\'()', (None, '@', None, None, None, '^', '&', '*', '('))]
        + [K('key_0', 1, '0', '_', **b(rshift=')')),
           K('key_colon', 1, ':', '*', **b(rplain='-', rshift='_')),
           K('key_minus', 1, '-', '=', **(b(rplain='=', rshift='+') or dict(rshift='$BD'))),
           K('key_ch', 1, '^', '^', **(b(rplain='\\', rshift='bar') or dict(rplain='=', rshift='+'))),
           K('key_reset', 1, fn='reset', label='RST')],
        [K(None, 0.53), K('key_tab', 1, 'tab', label='TAB')] + letters('QWERTYUIOP')
        + [K('key_at', 1, '@', '@', **(b(rplain='[', rshift='{') or dict(rplain='~', rshift='tilde'))),
           K('key_backslash', 1, '\\', 'bar', **b(rplain=']', rshift='}')),
           K('key_ret', 2, 'ret', label='↵')],
        [K(None, 1.07), K('key_ctrl', 1, role='ctrl', label='CTRL')] + letters('ASDFGHJKL')
        + [K('key_semicolon', 1, ';', '+', **(b(rplain=';', rshift=':') or dict(rshift='$AB'))),
           K('key_lbracket', 1, '[', '{', **b(rplain="'", rshift='"')),
           K('key_rbracket', 1, ']', '}', **b(rplain='$DD', rshift='$DD')),
           K('key_cl', 1, fn='cl', label='C/L')],
        [K(None, 0.59), K('key_caps', 1, fn='caps', label='LOCK'), K('key_shift_l', 1, role='shift', label='⇧')]
        + letters('ZXCVBNM')
        + [K('key_comma', 1, ',', '<'), K('key_period', 1, '.', '>'), K('key_slash', 1, '/', '?'),
           K('key_shift_r', 1, role='shift', label='⇧'), K('key_up', 1, 'up', label='↑'),
           K('key_del', 1, 'del', label='DEL')],
        [K(None, 1.09), K(None, 1, label=''), K('key_f1', 1, fn='f1', label='F1'), K('key_f2', 1, fn='f2', label='F2'),
         K('key_space', 7.9, 'space'), K('key_left', 1, 'left', label='←'), K('key_right', 1, 'right', label='→'),
         K('key_down', 1, 'down', label='↓')],
    ]


def p82_keys(late=False):
    """Правец-82: клавиатура Apple ][+ с ЛАТ. Ранняя (KEY82OLD) - с RPT и без
    стрелок, поздняя (KEY82NEW) - со стрелками, без RPT и с КИР вместо ЛАТ"""
    lat = 'КИР' if late else 'ЛАТ'
    return [
        [K('key_' + d, 1, d, s) for d, s in zip('123456789', '!"#$%&\'()')]
        + [K('key_0', 1, '0'), K('key_colon', 1, ':', '*'), K('key_minus', 1, '-', '=', rshift='$BD'),
           K('key_ch', 1, '^', '^', rplain='=', rshift='+'), K('key_reset', 1, fn='reset', label='RST', color='red')],
        [K('key_esc', 1.25, 'esc', label='ОСВ', color='grey')] + letters('QWERTYUIOP')
        + [K('key_at', 1, '@', '@', rplain='~', rshift='tilde'),
           K('key_ret', 2.75, 'ret', label='RETURN') if late else K('key_rpt', 1, fn='rpt', label='RPT'),
           ] + ([] if late else [K('key_ret', 1.75, 'ret', label='RETURN')]),
        [K('key_ctrl', 1.5, role='ctrl', label='МК', color='grey')] + letters('ASDFGHJKL')
        + [K('key_semicolon', 1, ';', '+', rshift='$AB'), K('key_lbracket', 1, '['), K('key_rbracket', 1, ']'),
           K('key_cl', 1.5, fn='cl', label=lat, color='yellow') if late else K('key_down', 1.5, 'down', label='↓')],
        [K('key_shift_l', 1.75, role='shift', label=lat, color='white')] + letters('ZXCVBNM')
        + [K('key_comma', 1, ',', '<'), K('key_period', 1, '.', '>'), K('key_slash', 1, '/', '?'),
           K('key_shift_r', 1, role='shift', label=lat, color='white')]
        + ([K('key_left', 1, 'left', label='←'), K('key_right', 1.25, 'right', label='→')] if late else
           [K('key_cl', 1, fn='cl', label=lat, color='yellow'), K(None, 1.25, gap=0)]),
        [K(None, 1, gap=2.5), K('key_space', 9, 'space')],
    ]


def p8m_keys():
    """Правец-8М: клавиатура 82 с DEL и стрелками"""
    return [
        [K('key_' + d, 1, d, s) for d, s in zip('123456789', '!"#$%&\'()')]
        + [K('key_0', 1, '0'), K('key_colon', 1, ':', '*'), K('key_minus', 1, '-', '=', rshift='$BD'),
           K('key_ch', 1, '^', '^', rplain='=', rshift='+'), K('key_reset', 1, fn='reset', label='RST')],
        [K('key_esc', 1.25, 'esc', label='ОСВ')] + letters('QWERTYUIOP')
        + [K('key_at', 1, '@', '@', rplain='~', rshift='tilde'), K('key_backslash', 1, '\\', 'bar'),
           K('key_ret', 1.75, 'ret', label='RETURN')],
        [K('key_ctrl', 1.5, role='ctrl', label='МК')] + letters('ASDFGHJKL')
        + [K('key_semicolon', 1, ';', '+', rshift='$AB'), K('key_lbracket', 1, '[', '{'),
           K('key_rbracket', 1, ']', '}'), K('key_cl', 1.5, fn='cl', label='КИР')],
        [K('key_shift_l', 1, role='shift', label='КИР'), K('key_del', 1, 'del', label='DEL')] + letters('ZXCVBNM')
        + [K('key_comma', 1, ',', '<'), K('key_period', 1, '.', '>'), K('key_slash', 1, '/', '?'),
           K('key_shift_r', 1, role='shift', label='КИР'), K('key_left', 1, 'left', label='←'),
           K('key_right', 1, 'right', label='→')],
        [K(None, 1, gap=2.5), K('key_space', 9, 'space')],
    ]


STYLES = {
    # корпус, клавиша, ее верх, буквы, кириллица, лампы
    'pc':  dict(body='#e9e6dc', well='#8d8a80', key='#bdbab0', top='#ecebe6', ink='#202020', cyr='#2850c8'),
    '82':  dict(body='#e3cfa4', well='#5a4c32', key='#101010', top='#2a2a2a', ink='#f0f0f0', cyr='#f0f0f0'),
    '8m':  dict(body='#ece6d8', well='#9c8e70', key='#b89466', top='#d8bc8e', ink='#202020', cyr='#202020'),
}
KEY_COLORS = {'red': ('#a01010', '#d83030', '#ffffff'), 'grey': ('#707070', '#9a9a9a', '#ffffff'),
              'white': ('#c8c8c8', '#f2f2f2', '#202020'), 'yellow': ('#c8a000', '#f0cc20', '#202020')}

U = 19.0          # шаг клавиш, мм
PAD = 8.0


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def text(x, y, s, size, fill, anchor='middle', weight='normal'):
    return ('<text x="%.2f" y="%.2f" font-family="Arial" font-size="%.2f" '
            'font-weight="%s" fill="%s" text-anchor="%s">%s</text>' % (x, y, size, weight, fill, anchor, esc(s)))


def key_labels(k, m, alias82=False):
    """Надписи клавиши: латиница, она с Shift, кириллица, она с Shift; надпись,
    совпавшая с другой, пуста. Последнее значение - буква ли это"""
    pk, sk = host(k.plain), host(k.shift or k.plain)
    rp = k.rplain if k.rplain else pk
    rs = k.rshift if k.rshift else (k.rplain if k.rplain else sk)

    def code(h, s, ru):
        return int(h[1:], 16) if len(h) > 1 and h.startswith('$') else find(m, host(h), s, ru)
    lat0, lat1 = char(code(pk, False, False)), char(code(sk, True, False))
    cyr0 = char(code(rp, False, True), alias82)
    cyr1 = char(code(rs, True, True), alias82)
    letter = len(k.plain) == 1 and k.plain.isalpha()
    if not cyr0 and (pk, False, True, True) in m:
        cyr0 = 'Ы'      # только в 8-битной таблице: $5F/$7F
    if lat1 == lat0:
        lat1 = ''
    if letter:
        lat1 = ''
        cyr1 = ''
    if cyr0 == lat0:
        cyr0 = ''
    if cyr1 == lat1 or cyr1 == cyr0:
        cyr1 = ''
    return lat0, lat1, cyr0, cyr1, letter


def key_table(k):
    """Строки таблицы .keys для клавиши рисунка"""
    lines = []
    if k.label is None and k.plain is not None and k.plain != 'space':
        for slot, h in (('', k.plain), ('/S', k.shift), ('/R', k.rplain), ('/SR', k.rshift)):
            if h:
                lines.append('%s%s: %s' % (k.id, slot, h if len(h) > 1 and h.startswith('$') else host_name(h)))
    elif k.plain == 'space':
        lines.append('%s: space' % k.id)
    if k.role:
        lines.append('%s: %s' % (k.role, k.id))
    elif k.fn:
        lines.append('%s: %s' % (k.id, k.fn))
    elif k.label is not None and k.id and k.plain:
        lines.append('%s: %s' % (k.id, host_name(k.plain)))
    return lines


def build(name, title, rows, mapfile, style, cyr_layout, leds, alias82=False, wide=15.0):
    m = load_map(mapfile)
    st = STYLES[style]
    svg, keys = [], []
    width = wide * U + 2 * PAD + (34 if leds else 0)
    height = len(rows) * U + 2 * PAD
    svg.append('<?xml version="1.0" encoding="UTF-8" standalone="no"?>')
    svg.append('<!-- Copyright (c) 2026 Mikhail Revzin. Licensed under CC BY-SA 4.0: '
               'https://creativecommons.org/licenses/by-sa/4.0/ -->')
    svg.append('<!-- Made by tools/pravetz-keyboard/make_keyboards.py: edit it, not this file -->')
    svg.append('<svg xmlns="http://www.w3.org/2000/svg" width="%.0fmm" height="%.0fmm" viewBox="0 0 %.0f %.0f" '
               'version="1.1">' % (width, height, width, height))
    svg.append('<title>%s</title>' % esc(title))
    svg.append('<rect x="0" y="0" width="%.0f" height="%.0f" rx="4" fill="%s"/>' % (width, height, st['body']))
    svg.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" rx="2" fill="%s"/>'
               % (PAD - 2, PAD - 2, wide * U + 4, len(rows) * U + 4, st['well']))
    keys.append('// %s. Сделано tools/pravetz-keyboard/make_keyboards.py по %s' % (title, mapfile))
    keys.append('// id: клавиша хоста; /S - с Shift рисунка, /R - в кириллице; $XX - код как есть;')
    keys.append('// caps, cl, reset, f1, f2 - клавиши этих функций')
    for r, row in enumerate(rows):
        x = PAD
        y = PAD + r * U
        for k in row:
            x += k.gap * U
            w = k.w * U
            if k.id is None and k.label is None:
                x += w
                continue
            kc, tc, ink = st['key'], st['top'], st['ink']
            cyr = st['cyr']
            if k.color:
                kc, tc, ink = KEY_COLORS[k.color]
                cyr = ink
            body = ['<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.6" fill="%s"/>'
                    % (x + 0.6, y + 0.6, w - 1.2, U - 1.2, kc),
                    '<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.4" fill="%s"/>'
                    % (x + 2.2, y + 1.6, w - 4.4, U - 5.6, tc)]
            cx, cy = x + w / 2, y + U / 2 - 1.4
            if k.label is not None:
                parts = k.label.split('|')
                size = 3.6 if max(len(p) for p in parts) > 2 else 5.2
                if k.label in ('←', '→', '↑', '↓'):
                    size = 7
                for i, p in enumerate(parts):
                    body.append(text(cx, cy + 1.3 + (i - (len(parts) - 1) / 2) * 4.4, p, size, ink))
            elif k.plain is not None and k.plain != 'space':
                lat0, lat1, cyr0, cyr1, letter = key_labels(k, m, alias82)
                if cyr_layout == 'right':
                    # PC: латиница слева (верх - с Shift), кириллица справа
                    if letter:
                        body.append(text(x + 4.3, y + 7.6, lat0, 5.2, ink, 'start'))
                        body.append(text(x + w - 4.3, y + 13.2, cyr0, 5.2, cyr, 'end'))
                    else:
                        body.append(text(x + 4.3, y + 7.6, lat1, 4.6, ink, 'start'))
                        body.append(text(x + 4.3, y + 13.2, lat0, 4.6, ink, 'start'))
                        body.append(text(x + w - 4.3, y + 7.6, cyr1, 4.0, cyr, 'end'))
                        body.append(text(x + w - 4.3, y + 13.2, cyr0, 4.0, cyr, 'end'))
                else:
                    # Apple: кириллица сверху, латиница снизу; у цифр - Shift сверху
                    if letter:
                        body.append(text(cx, y + 7.6, cyr0, 4.8, cyr))
                        body.append(text(cx, y + 13.2, lat0, 4.8, ink))
                    else:
                        top = cyr0 or lat1
                        body.append(text(cx, y + 7.6, top, 4.8, ink))
                        body.append(text(cx, y + 13.2, lat0, 4.8, ink))
            keys.extend(key_table(k))
            if k.id:
                svg.append('<g id="%s">%s</g>' % (k.id, ''.join(body)))
            else:
                svg.append('<g>%s</g>' % ''.join(body))
            x += w
    # Лампы: id led_caps (Caps Lock) и led_cl (C/L Lock); остальные горят всегда
    lx = PAD + wide * U + 17
    for i, (lid, color, label) in enumerate(leds):
        ly = PAD + 8 + i * 15
        svg.append('<circle cx="%.1f" cy="%.1f" r="3.2" fill="#202020"/>' % (lx, ly))
        svg.append('<circle%s cx="%.1f" cy="%.1f" r="2.6" fill="%s"/>'
                   % (' id="%s"' % lid if lid else '', lx, ly, color))
        if label:
            svg.append(text(lx, ly + 7.5, label, 3.2, '#404040'))
    svg.append('</svg>')
    write_svg(name, svg)
    open(os.path.join(PRAVETZ, name + '.keys'), 'w', encoding='utf-8', newline='\n').write('\n'.join(keys) + '\n')


def write_svg(name, svg):
    open(os.path.join(PICS, name + '_font.svg'), 'w', encoding='utf-8', newline='\n').write('\n'.join(svg) + '\n')


# Чертеж 8А по KEY8A1: в миллиметрах от левого верхнего угла клавиши. Цвета
# сняты с фото: панель, колодец, скаты (верхний темнее), грань, ее нижний край
A8 = dict(body='#e4dccb', well='#2a1c14', edge='#8a7660', back='#c7a67c', top='#c2a57e', side='#d6bb95',
          face='#f0e9dd', ink='#2a2a2a')
A8_SHOW = {'0': 'Ø', '^': 'Λ', '_': '—'}    # так написано на колпачках
A8_LATIN_TOP = 'IHN'                        # на этих трех ЯВЕРТЪ латиница сверху
A8_LEFT, A8_TOP = 14.0, 12.0


def poly(pts, fill):
    return '<polygon points="%s" fill="%s"/>' % (' '.join('%.2f,%.2f' % p for p in pts), fill)


def arrow(cx, cy, dx, dy, ink):
    """Тонкая стрелка с треугольным острием, вдоль (dx, dy)"""
    def p(a, b):
        return (cx + a * dx - b * dy, cy + a * dy + b * dx)
    return poly([p(-3.6, -0.22), p(1.0, -0.22), p(1.0, -1.1), p(3.6, 0), p(1.0, 1.1), p(1.0, 0.22), p(-3.6, 0.22)], ink)


def a8_key(x, y, w, k, m, bds):
    """Клавиша 8А: широкий верхний скат, узкие боковые, грань смещена вниз"""
    h = U
    x0, y0, x1, y1 = x + 0.35, y + 0.35, x + w - 0.35, y + h - 0.35
    fx0, fx1 = x0 + (5.0 if k.label == '↵' else 1.8), x1 - (0.6 if k.label == '↵' else 1.8)
    fy0, fy1 = y0 + 4.9, y1 - 0.5
    g = ['<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="0.9" fill="%s"/>'
         % (x0, y0, x1 - x0, y1 - y0, A8['back']),
         poly([(x0, y0), (x1, y0), (fx1, fy0), (fx0, fy0)], A8['top']),
         poly([(x0, y0), (fx0, fy0), (fx0, fy1), (x0, y1)], A8['side']),
         poly([(x1, y0), (fx1, fy0), (fx1, fy1), (x1, y1)], A8['side']),
         '<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.6" fill="%s"/>'
         % (fx0, fy0, fx1 - fx0, fy1 - fy0, A8['face'])]
    ink = A8['ink']
    cx, cy = (fx0 + fx1) / 2, (fy0 + fy1) / 2
    a = {'←': (-1, 0), '→': (1, 0), '↑': (0, -1), '↓': (0, 1)}
    if k.label in a:
        g.append(arrow(cx, cy, a[k.label][0], a[k.label][1], ink))
    elif k.label == '⇧':
        g.append(poly([(cx, cy - 2.4), (cx + 2.5, cy + 0.2), (cx + 1.1, cy + 0.2), (cx + 1.1, cy + 2.2),
                       (cx - 1.1, cy + 2.2), (cx - 1.1, cy + 0.2), (cx - 2.5, cy + 0.2)], ink))
    elif k.label == '↵':
        g.append(poly([(cx - 8, cy + 1.4), (cx - 5.4, cy + 0.3), (cx - 5.4, cy + 1.15), (cx + 5.0, cy + 1.15),
                       (cx + 5.0, cy - 2.6), (cx + 1.0, cy - 2.6), (cx + 1.0, cy - 3.0), (cx + 5.4, cy - 3.0),
                       (cx + 5.4, cy + 1.6), (cx - 5.4, cy + 1.6), (cx - 5.4, cy + 2.5)], ink))
    elif k.label:
        g.append(text(cx, cy + 1.05, k.label, 3.0, ink))
    elif k.plain is not None and k.plain != 'space':
        lat0, lat1, cyr0, cyr1, letter = key_labels(k, m)
        lat0, lat1, cyr0, cyr1 = [A8_SHOW.get(s, s) for s in (lat0, lat1, cyr0, cyr1)]
        hi, lo = y0 + 9.6, y0 + 15.8
        if bds and not letter and (cyr1 or (lat1 and cyr0)):
            # Четыре надписи: слева латиница, справа кириллица, Shift сверху
            for s, tx, ty in ((lat1, cx - 3.9, hi), (lat0, cx - 3.9, lo), (cyr1, cx + 3.9, hi), (cyr0, cx + 3.9, lo)):
                if s:
                    g.append(text(tx, ty, s, 4.0, ink))
        elif not bds and k.plain in A8_LATIN_TOP:
            g.append(text(cx, hi, lat0, 4.6, ink))
            g.append(text(cx, lo, cyr0, 4.6, ink))
        else:
            if cyr0 or lat1:
                g.append(text(cx, hi, cyr0 or lat1, 4.6, ink))
            g.append(text(cx, lo, lat0, 4.6, ink))
    return g


def well_path(rows):
    """Контур колодца: ступени по правому и левому краю рядов"""
    edge = []
    for r, row in enumerate(rows):
        left = next(k.w for k in row if k.id is None and k.label is None) if row[0].id is None else 0
        edge.append((A8_LEFT + left * U, A8_LEFT + sum(k.w for k in row) * U))
    margin = 0.9
    y = [A8_TOP + r * U for r in range(len(rows) + 1)]
    y[0] -= margin
    y[-1] += margin
    pts = [(edge[0][0] - margin, y[0]), (edge[0][1] + margin, y[0])]
    for r in range(len(rows)):
        pts.append((edge[r][1] + margin, y[r]))
        pts.append((edge[r][1] + margin, y[r + 1]))
    for r in reversed(range(len(rows))):
        pts.append((edge[r][0] - margin, y[r + 1]))
        pts.append((edge[r][0] - margin, y[r]))
    return pts


def build_8a(name, title, rows, mapfile, bds):
    m = load_map(mapfile)
    width, height = 358, 119
    svg = ['<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
           '<!-- Copyright (c) 2026 Mikhail Revzin. Licensed under CC BY-SA 4.0: '
           'https://creativecommons.org/licenses/by-sa/4.0/ -->',
           '<!-- Made by tools/pravetz-keyboard/make_keyboards.py: edit it, not this file -->',
           '<svg xmlns="http://www.w3.org/2000/svg" width="%dmm" height="%dmm" viewBox="0 0 %d %d" '
           'version="1.1">' % (width, height, width, height),
           '<title>%s</title>' % esc(title),
           '<rect x="0" y="0" width="%d" height="%d" rx="4" fill="%s"/>' % (width, height, A8['body']),
           '<polygon points="%s" fill="%s" stroke="%s" stroke-width="0.4"/>'
           % (' '.join('%.2f,%.2f' % p for p in well_path(rows)), A8['well'], A8['edge'])]
    keys = ['// %s. Сделано tools/pravetz-keyboard/make_keyboards.py по %s' % (title, mapfile),
            '// id: клавиша хоста; /S - с Shift рисунка, /R - в кириллице; $XX - код как есть;',
            '// caps, cl, reset, f1, f2 - клавиши этих функций']
    for r, row in enumerate(rows):
        x = A8_LEFT
        y = A8_TOP + r * U
        for k in row:
            w = k.w * U
            if k.id is None and k.label is None:
                x += w
                continue
            body = a8_key(x, y, w, k, m, bds)
            keys.extend(key_table(k))
            svg.append(('<g id="%s">%s</g>' % (k.id, ''.join(body))) if k.id else '<g>%s</g>' % ''.join(body))
            x += w
    # Лампы как на фото: две справа от верхнего ряда, питание у нижнего
    for lid, color, lx, ly in (('led_caps', '#e0b020', 16.3, 0.45), ('led_cl', '#20b080', 17.14, 0.45),
                               (None, '#d02020', 15.68, 4.5)):
        px, py = A8_LEFT + lx * U, A8_TOP + ly * U
        svg.append('<circle cx="%.1f" cy="%.1f" r="3.4" fill="#f4eee2"/>' % (px, py))
        svg.append('<circle cx="%.1f" cy="%.1f" r="2.8" fill="#3a3028"/>' % (px, py))
        svg.append('<circle%s cx="%.1f" cy="%.1f" r="2.3" fill="%s"/>'
                   % (' id="%s"' % lid if lid else '', px, py, color))
    svg.append('</svg>')
    write_svg(name, svg)
    open(os.path.join(PRAVETZ, name + '.keys'), 'w', encoding='utf-8', newline='\n').write('\n'.join(keys) + '\n')


def host_name(h):
    # Имена, которые понимает translate_key_name()
    return {'/': 'slash', ';': 'semicolon', ':': 'colon', '\\': '\\', 'bar': 'bar', 'tilde': 'tilde',
            '-': 'minus', '+': 'plus', '^': 'circum', '_': 'under'}.get(h, h)


LEDS_SU = [(None, '#40d040', 'Power'), ('led_caps', '#f0d020', 'Caps'), ('led_cl', '#40d040', 'C/L')]
build('pravetz-8a-gost', 'Клавиатура Правец-8А (СССР, ГОСТ)', pc_keys(), 'pravetz-8a-gost.map', 'pc', 'right', LEDS_SU)
build('pravetz-8c-bds', 'Клавиатура Правец-8С (БДС)', pc_keys(), 'pravetz-8a-bds.map', 'pc', 'right', LEDS_SU)
build_8a('pravetz-8a-bds', 'Клавиатура Правец-8А (БДС)', a8_keys(True), 'pravetz-8a-bds.map', True)
build_8a('pravetz-8a-yavert', 'Клавиатура Правец-8А (ЯВЕРТЪ)', a8_keys(), 'pravetz-8a-yavert.map', False)
build('pravetz-82', 'Клавиатура Правец-82 (ранняя)', p82_keys(), 'pravetz-82.map', '82', 'top',
      [('led_cl', '#f0a020', 'ЛАТ')], alias82=True)
build('pravetz-82-late', 'Клавиатура Правец-82 (поздняя)', p82_keys(True), 'pravetz-82.map', '82', 'top',
      [('led_cl', '#f0a020', 'КИР')], alias82=True)
build('pravetz-8m', 'Клавиатура Правец-8М', p8m_keys(), 'pravetz-8a-yavert.map', '8m', 'top',
      [(None, '#f0f0f0', ''), ('led_cl', '#f0a020', 'КИР')])

# Правец-8Д (Oric Atmos): клавиатура - матрица scan-keyboard, ее таблица
# клавиш (files/pravetz-8d.keys) написана руками по матрице, отсюда только
# рисунок. Расположение и надписи - по фотографии машины (Wikimedia Commons,
# File:Pravetz-8D.jpg): цифровой ряд без ОСВ, ОСВ начинает ряд Q, МК - ряд A;
# кириллица над латиницей, нуль перечеркнут, Shift - стрелка, ввод - уголок.
# Ряды сдвинуты так, как на фото (в долях клавиши от левого края ряда ОСВ)
CYR_8D = dict(zip('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'АБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЪЗ'))


def p8d_rows():
    def L(c):
        return ('key_' + c.lower(), 1, c, CYR_8D[c])

    def S(kid, low, high='', w=1):
        return (kid, w, low, high)

    def F(kid, label, w=1):
        return (kid, w, None, label)

    def gap(w):
        return (None, w, None, '')
    return [
        [gap(0.55)] + [S('key_' + d, d, s) for d, s in zip('123456789', '!"#$%&\'()')]
        + [S('key_0', 'Ø'), S('key_colon', ':', '*'), S('key_minus', '-', '='), S('key_semi', ';', '+')],
        [F('key_esc', 'ОСВ')] + [L(c) for c in 'QWERTYUIOP']
        + [S('key_at', '@', 'Ю'), S('key_bksl', '\\', 'Э'), S('key_caret', '^', 'Ч')],
        [gap(0.5), F('key_ctrl', 'МК')] + [L(c) for c in 'ASDFGHJKL']
        + [S('key_lbr', '[', 'Ш'), S('key_rbr', ']', 'Щ'), F('key_ret', '↵', 1.5)],
        [gap(0.5), F('key_lshift', '⇧', 1.75)] + [L(c) for c in 'ZXCVBNM']
        + [S('key_comma', ',', '<'), S('key_dot', '.', '>'), S('key_slash', '/', '?'), F('key_rshift', '⇧', 1.75)],
        [gap(0.5), F('key_del', 'DEL'), F('key_left', '←'), F('key_down', '↓'), ('key_space', 7.5, None, ''),
         F('key_up', '↑'), F('key_right', '→'), F('key_cl', 'C/L')],
    ]


def build_8d():
    body, well, key, top, ink = '#e6dbbf', '#2a2620', '#d6c8a6', '#efe6cf', '#1a1a1a'
    rows = p8d_rows()
    wide = 14.0
    width = wide * U + 2 * PAD
    height = len(rows) * U + 2 * PAD
    svg = ['<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
           '<!-- Copyright (c) 2026 Mikhail Revzin. Licensed under CC BY-SA 4.0: '
           'https://creativecommons.org/licenses/by-sa/4.0/ -->',
           '<!-- Made by tools/pravetz-keyboard/make_keyboards.py: edit it, not this file -->',
           '<svg xmlns="http://www.w3.org/2000/svg" width="%.0fmm" height="%.0fmm" viewBox="0 0 %.0f %.0f" '
           'version="1.1">' % (width, height, width, height),
           '<title>Клавиатура Правец-8Д</title>',
           '<rect x="0" y="0" width="%.0f" height="%.0f" rx="4" fill="%s"/>' % (width, height, body)]
    for r, row in enumerate(rows):
        x = PAD
        y = PAD + r * U
        for kid, w, low, high in row:
            w *= U
            if kid is None:
                x += w
                continue
            g = ['<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.6" fill="%s"/>'
                 % (x - 0.2, y - 0.2, w + 0.4, U + 0.4, well),
                 '<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.6" fill="%s"/>'
                 % (x + 0.6, y + 0.6, w - 1.2, U - 1.2, key),
                 '<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f" rx="1.4" fill="%s"/>'
                 % (x + 2.2, y + 1.6, w - 4.4, U - 5.6, top)]
            cx, cy = x + w / 2, y + U / 2 - 1.4
            if low is None and high:
                size = 7 if high in ('←', '→', '↑', '↓', '⇧', '↵') else (4.2 if len(high) > 2 else 5.2)
                g.append(text(cx, cy + 1.6, high, size, ink, weight='bold'))
            elif low is not None:
                g.append(text(cx, y + 7.4, high, 4.8, ink))
                g.append(text(cx, y + 13.0, low, 4.4, ink))
            svg.append('<g id="%s">%s</g>' % (kid, ''.join(g)))
            x += w
    svg.append('</svg>')
    write_svg('pravetz-8d', svg)


build_8d()
