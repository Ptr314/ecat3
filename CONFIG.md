# CONFIG.md

В данном документе описывается формат файлов конфигурации эмулируемых устройств.

# Оглавление

* [Общие замечания](#общие-замечания)
* [Синтаксис описания устройств](#синтаксис-описания-устройств)
* [Устройства](#Устройства)
    * [Специальные](#Специальные)
        * [system](#system) (Описание системы)
        * [memory-mapper](#memory-mapper) (Основной диспетчер памяти)
    * [CPU](#CPU)
        * [Общие параметры](#Общие-параметры)
        * [i8080](#i8080)
        * [Z80](#Z80)
    * [Базовые устройства](#Базовые-устройства) 
        * [ram](#ram) (Блок ОЗУ)
        * [rom](#rom) (Блок ПЗУ)
        * [port](#port) (Адресуемый порт)
        * [port-address](#port-address) (Адресуемый порт, запоминающий адрес)
        * [register](#register) (Регистр-защелка)
        * [generator](#generator) (Генератор импульсов)
        * [page-mapper](#page-mapper) (Страничный диспетчер)
    * [Периферия](#Периферия)
        * [speaker](#speaker) (Однобитный динамик)
        * [scan-keyboard](#scan-keyboard) (Сканирующая клавиатура)
        * [map-keyboard](#map-keyboard) (Клавиатура с записью в регистр)
        * [taperecorder](#taperecorder) (Магнитофон)
        * [i8253](#i8253) (Таймер i8253, КР580ВИ53)
        * [i8255](#i8255) (Порт ввода вывода i8255, КР580ВВ55)
        * [i8257](#i8257) (Контроллер ПДП i8257, КР580ВТ57)
        * [i8275](#i8275) (Видеоконтроллер i8275, КР580ВГ75)
        * [wd1793](#wd1793) (Контроллер дисковода WD1793, КР1818ВГ93)
        * [fdd](#fdd) (Дисковод)
    * [Машино-зависимые устройства](#Машино-зависимые-устройства)
        * [orion-128-display](#orion-128-display) (Дисплей Орион-128)
        * [agat-display](#agat-display) (Дисплей Агат-7)
        * [agat-fdc140](#agat-fdc140) (Контроллер дисковода Агат 140 Кб).
        * [i8275-display](#i8275-display) (Дисплей на i8275)

# Общие замечания

* Кодировка для всех файлов &ndash; UTF-8.
* Описание конфигураций компьютеров содержится в файлах с расширением __.cfg__. Файлы должны находиться в любом подкаталоге ниже каталога __computers__.
* Эмулятор обходит все подкаталоги __computers__ и собирает общий список доступных систем. 
* Системные файлы, загружаемые из конфигурации, должны находиться в том же каталоге, что и .cfg.
* Файлы, загружаемые через интерфейс эмулятора, по умолчанию будут искаться в каталоге __software__.
* Текстовое описание конфигурации должно находиться в файле, имя которого совпадает с файлом конфигурации, но с расширением __.md__ в формате [Markdown](https://ru.wikipedia.org/wiki/Markdown).
* Файл cfg состоит из описаний устройств:
    * Первым устройством в файле должно быть устройство [__system__](#system), задающее общие параметры и описание системы.
    * Вторым устройством должен быть [CPU](#cpu), так как его частота устанавливается как основная системная.
    * Далее идут описания остальных устройств и их конфигураций. Список допустимых устройств и их параметров см. далее.
    * Также обязательным является наличие устройства [memory-mapper](#memory-mapper), задающее отображение остальных устройств на адресное пространство процессора.

# Синтаксис описания устройств

~~~
device_id: device_type {
    parameter1 = value
    parameter2 = value
    ~interface1 = other_device.interface3
    ~interface2[bits] = !other_device.interface4[bits]
}
~~~

* Устройства соединяются друг с другом с помощью интерфейсов.
* Набор интерфейсов специфичен для каждого типа устройств.
* Если ширина интерфейса у двух устройств различна, нужно указывать диапазон подключаемых битов.
* Обычные параметры задаются как ___имя = значение___. Имена параметров и форматы значений специфичны для каждого устройства.
    * Цифровые значения можно дополнять суффиксами:
        * k: значение будет умножено на 1024.
    * Или префиксами:
        * $: число будет рассматриваться как шестнадцатеричное. 
        * #: число будет рассматриваться как двоичное. 
* Соединения интерфейсов задаются в следующем формате:
    * __~address = cpu.address__
        * Простое соединение интерфейсов одинаковой ширины.
    * __~page = portFB.value[2-3]__
        * Биты [0-1] интерфейса __page__ подключаются к битам [2-3] интерфейса __value__ устройства __portFB__;
        * Ширина интерфейса берется из правой части.
    * Если в начале правой части стоит знак «!», интерфейсы будут соединены с инверсией.

## Пример

~~~
port-user : i8255 {}

latch : register {
	~in[0-3] = port-user.B[0-3]
	~c = port-user.CH[3]
}

romdisk : rom {
	size = 512k
	image = rom512.rom
	~address[0-7] = port-user.B
	~address[8-11] = port-user.CL
	~address[12-14] = port-user.CH[0-2]
	~address[15-18] = latch.out[0-3]
	~data = port-user.A
}
~~~
* К порту на основе i8255 подключается rom-диск размером 512 Кб. Номер выбранной страницы (размером 32 Кб) хранится в регистре-защелке, которая срабатывает на установку старшего бита адреса.

# Устройства

## Специальные

### system
Основные параметры системы, раздел должен быть первым в файле.
~~~
system { 
	type = orion-128
	name = Орион-128
	version = КР580ВМ80 / Монитор-3
	charmap= radio-86rk 
	files = "Orion-128 (*.bru *.ord *.rko);;All files (*.*)"
	debug = 0
}
~~~

* __type*__: Идентификатор типа компьютера. Используется для сбора конфигураций в группы в диалоге выбора.
* __name*__: Эквивалент type, отображается в интерфейсе.
* __version*__: Вариант исполнения. Выводится вторым уровнем в дереве выбора конфигурации.
* __charmap*__: Файл с отображением кодировки на UTF-8. Должен состоять из 256 символов UTF-8, где номер символа &ndash; его код в исходном компьютере.
    * Таблица используется в окнах дампов и отладчике.
    * Символы можно делить на группы переводами строки, которые при обработке игнорируются.
    * Файлы кодировок находятся в каталоге __data__ и имеют расширение __.chr__.
* __files__: Фильтр, который будет использоваться в диалоге выбора файлов для загрузки.
* __debug__ (0/1): Используется для отладочных конфигураций, которые по умолчанию не показываются в общем списке. Включить вывод таких конфигураций можно параметром __show_debug_versions__ в ini-файле.

### Пример

~~~
system { 
	type = orion-128
	name = Орион-128
	version = Z80 Card II / Тест
	charmap = radio-86rk 
	screenratio = 1
	screenscale = 2
	files = "Файлы Орион-128 (*.bru; *.ord; *.rko)|*.bru; *.ord; *.rko"
}
~~~

### memory-mapper

Главный диспетчер адресного пространства процессора.

~~~
mapper : memory-mapper {
	portstomemory = 0
	cancelinit = $8000
	config = port1

	@memory[*][$0000-$07FF] = bios {mode = r}
	@memory[value:mask][addr1-addr2] = device
	@memory[addr1-addr2] = device[start_addr]

	@port[number] = device
}
~~~

В конфигурации Memory Mapper три основных раздела:

* Основные параметры:
    * __portstomemory__ (0/1): Отображение портов на память. По умолчанию &ndash; 0;
    * __wideports__ (0/1): Разрядность номеров портов (8/16). По умолчанию &ndash; 8;
    * __cancelinit__: При получении какого адреса отменится запись ___@memory[*]___. Обычно используется для отображения системного ПЗУ на начальные адреса памяти после сброса.
    * __config__: Устройство (порт или регистр), содержащее регистр конфигурации диспетчера. Значения этого регистра используются в записях ___@memory___. Устройство должно иметь один из интерфейсов __~value__ или __~out__.
* Отображение адресов памяти. Поиск подходящего условия происходи сверху вниз, поэтому первые строки имеют приоритет. Примеры записей:
    * __@memory[$0000-$7FFF] = ram__
        * Диапазон памяти $0000-$7FFF безусловно отображается на устройство __ram__;
    * __@memory[*][$0000-$07FF] =  bios {mode = r}__
        * Диапазон памяти $0000-$07FF после сброса отображается на устройство __bios__ при операциях чтения;
        * После получения адреса с маской __cancelinit__ эта запись блокируется;
        * Данная запись должна быть первой в списке, чтобы иметь приоритет над остальными.
    * __@memory[$E000-$FFFF] = dma {mode = w}__
        * Диапазон памяти $E000-$FFFF отображается на устройство __dma__ при операциях записи;
    * __@memory[$F700-$F72F] = fdc {mode = w, addr_mask=$24, addr_value=0}__
        * Диапазон $F700-$F72F отображается на устройство __fdc__ при операциях записи, дополнительно адрес при наложении AND-маски $24 должен давать 0.
    * __@memory[0][$0000-$EFFF] = ram0__
	* __@memory[1][$0000-$EFFF] = ram1__
        * В зависимости от значения порта __config__ выбирается одна из страниц памяти.
    * __@memory[$00:$20][$F000-$F3FF] = ram0[$F000]__
	* __@memory[$20:$20][$F000-$F3FF] = mm3[$F000]__
        * В зависимости от значения отдельного бита config [value:mask] диапазон отображается либо на страницу памяти, либо на страничный менеджер. Начальный адрес на устройствах &ndash; $F000; 
* Распределение портов (при отключенном отображении на память).
    * __@port[$F4] = port-keyboard__
        * Порт $F4 отображается на устройство __port-keyboard__.

* Интерфейсы
    * __~config__{?, in}: Может использоваться вместо параметра __config__ для более гибкого подключения управляющих устройств. 

## CPU
### Общие параметры
~~~
cpu : z80 {
	clock = 5000000
	stopped = 0
}
~~~
* __clock*__: Частота процессора, Гц. Также становится первичной частотой для всей системы.
* __stopped__ {0/1}: Останов процессора при запуске машины.

### i8080

### z80

### 6502 / 65c02
~~~
cpu : 65c02 {
	clock = 1000000
	~irq = !port-irq.value[0]
	~nmi = !port-irq.value[1]
}
~~~
* Интерфейсы
    * __~irq{1, in}__: Внешнее маскируемое прерывание. Работает по низкому уровню.
    * __~nmi{1, in}__: Внешнее немаскируемое прерывание. Работает по отрицательному фронту.

## Базовые устройства

### ram
Блок ОЗУ
~~~
ram0 : ram {
	size = 64k
	fill = 0
}
~~~
* __size*__: Размер в байтах.
* __fill__ {0-255|random}: Байт для заполнения при холодном сбросе. По умолчанию &ndash; 0. При значении _random_ при холодном сбросе память будет заполняться случайными значениями.

### rom
Блок ПЗУ
~~~
bios : rom {
	size = 2k
	image = TestZ80.bin
}

ram-mapper : rom {
	data = {0,0,2,3,1,1,2,3}
}

~~~
* __size*__: Размер в байтах. Обязателен при загрузке файла через параметр __image__.
* __fill__: Байт для заполнения пустой области. По умолчанию &ndash; $FF.
* __image*__: Имя файла образа:
    * Поддерживаются форматы [Intel Hex](https://ru.wikipedia.org/wiki/Intel_HEX) (*.hex), двоичный (другие расширения).
    * Файл *.hex будет загружен с начального адреса на устройстве, независимо от указанного в самом файле.
    * Размер файла должен быть меньше или равен __size__. Остаток будет заполнен байтом __fill__.
    * Файл должен находиться в одном из следующих мест: 
        * В том же каталоге, что и __.cfg__;
        * В подкаталоге __files__ рядом с .cfg;
        * В подкаталоге __software__, совпадающем с подкаталогом __.cfg__.
* __data*__: Содержимое ПЗУ в виде строки чисел через запятую. 

Должен присутствовать хотя бы один из параметров &mdash; __image__ или __data__.

### port
Единичный адресуемый регистр/порт
~~~
port-videomode: port {
    size = 8
    default = $02
}
~~~
* Параметры:
    * __size__: Ширина регистра в битах. По умолчанию &ndash; 8;
    * __flipmask__: Маска для инверсии значения по отрицательному фронту интерфейса __flip__. По умолчанию &ndash; $FFFFFFFF.
    * __default__: Значение после сброса. По умолчанию &ndash; 0.
    * __mask__: AND-маска для записываемых значений. Совместно с __default__ позволяет задать неизменяемые биты. По умолчанию &ndash; $FFFFFFFF.
    * __setmask__: OR-маска для записываемых значений. По умолчанию &ndash; 0.
    * __constant_return__: Возвращаемое значение, если у порта нет функции чтения. По умолчанию не используется, порт при чтении возвращает записанное в него ранее значение.


* Интерфейсы:
    * __~data{=size=, in}__: Входные данные.
    * __~value{=size=, out}__: Выходные данные.
    * __~access{1, out}__: Выдает импульс отрицательной полярности в момент записи значения.
    * __~flip{1, in}__: При отрицательном фронте по этому интерфейсу происходит инверсия значения порта по маске __flipmask__. 
    * __~reset{1, in}__: При отрицательном фронте по этому интерфейсу происходит запись в порт значения __default__. 

### port-address
Единичный адресуемый регистр/порт, запоминающий адрес обращения, а не данные.
~~~
port-video: port-address {
    store_on_read = 1
}
~~~
Основные параметры и интерфейсы аналогично __port__.
* Дополнительные параметры:
    * __store_on_read__ (0, 1): Запоминать ли значение при чтении. По умолчанию - выключено.

### register
Регистр-защелка или буфер. Запоминает входное значение по стробу на входе записи или транслирует входы на выходы.
~~~
latch : register {
    type = latch-pos
	~in[0-3] = port-user.B[0-3]
	~c = port-user.CH[3]
}
~~~
* Параметры:
    * __type__: Тип регистра:
        * __buffer__: Входы транслируются на выходы. Интерфейс __~c__ не используется.
        * __flip-flop-pos__: Запись по положительному фронту на входе __c__.
        * __flip-flop-neg__: Запись по отрицательному фронту на входе __c__.
        * __latch-pos__: Передача при __c__=0 и хранение при __c__=1.
        * __latch-neg__: Передача при __c__=1 и хранение при __c__=0.
        * По умолчанию &ndash; __flip-flop-pos__;

Интерфейсы:
* __~in{16, in}__: Входные данные.
* __~c{1, in}__: Строб записи.
* __~out{16, out}__: Выход записанных данных. 

### generator
Генератор импульсов заданной частоты
~~~
gen50hz: generator {
	frequency = 50
	polarity = positive
	length = 1
}
~~~
* __frequency*__: Частота импульсов, Гц. 
* __polarity__: Полярность импульсов. Значения: _positive_, _pos_, _p_, _1_, _negative_, _neg_, _n_, _0_). Значение по умолчанию: _positive_.
* __length__: Длина импульса в отсчетах системного генератора. Значение по умолчанию: 1.
    * В связи с особенностями эмуляции, минимально возможная длина импульса фактически будет равна длине команды CPU, на которую он попадет.

### page-mapper
Страничный диспетчер
~~~
mm2 : page-mapper {
	frame = 16k
	@page[0] = ram0
	@page[1] = ram1
	@page[2] = ram2
	@page[3] = ram3
	~page    = portFB.value[2-3]
	~segment = portFB.value[0-1]
}
~~~
Параметры:
* __frame__: Размер сегмента страницы памяти, а также выходного окна. По умолчанию равен размеру страницы.
* __@page[]__: Перечень устройств-страниц.

Интерфейсы:
* __~page{?, in}*__: Номер выбранной страницы.
* __~segment{?, in}__: Номер сегмента в странице.

Если сегментирование страниц не используется, __frame__ и __~segment__ не указываются. Если используется только одна страница, необходимо указать интерфейс __~page=0__.

Количество страниц и сегментов должно соответствовать ширине соответствующих интерфейсов. В данном примере 4 страницы размером по 64кб, в каждой 4 сегмента по 16к, что соответствует двум двухбитным интерфейсам.

## Периферия

### speaker
Однобитный спикер
~~~
sound : speaker {
    mode = level
    shorts = 1
	~input = cpu.inte
	~mixer[0-2] = timer.output[0-2]
}
~~~

Параметры:
* __mode__ (level/flip): Режим работы входа __input__:
    * __level__: Используется текущий уровень сигнала.
    * __flip__: Значение бита инвертируется по отрицательному фронту входного сигнала.
* __shorts__ (0/1): Детектор коротких пиков на входе __input__. Изменения звука, имеющие длину короче периода семплирования, растягиваются по времени. По умолчанию &ndash; выключен.

Интерфейсы:
* __~input{1, in}__: Вход однобитного спикера.
* __~mixer{8, in}__: Вход для нескольких однобитных каналов.

__Примечания__:
* Должно быть создано как минимум одно соединение интерфейса __input__ либо __mixer__. Все использованные входы микшируются пропорционально.
* Чтобы работал регулятор громкости, устройство должно называться __sound__.

### scan-keyboard
Клавиатура с матрицей сканирования
~~~
keyboard : scan-keyboard {
	~scan = port-keyboard.A
	~output = port-keyboard.B
	~shift = port-keyboard.CH[5]
	~ctrl = port-keyboard.CH[6]
	~ruslat = port-keyboard.CH[7]
	~ruslat_led = !port-keyboard.CL[3]
	map = rk.kbd
	ctrl = ctrl
	shift = shift
	ruslat = caps
}
~~~
Параметры:
* __map__: Файл с раскладкой клавиатуры (см. ниже).
* __ctrl__: Назначение кнопки Control.
* __shift__: Назначение кнопки Shift.
* __ruslat__: Назначение кнопки переключения Рус-Лат.

Интерфейсы:
* __~scan{?, in}__: Линии сканирования. Ширина устанавливается автоматически по количеству колонок в @layout.
* __~output{?, out}__: Выходные линии. Ширина устанавливается автоматически по количеству строк в @layout.
* __~shift{1, out}__: Линия Shift;
* __~ctrl{1, out}__: Линия Control;
* __~ruslat{1, out}__: Линия Рус-Лат;
* __~ruslat_led{1, in}__: Индикатор Рус-Лат;

Раскладка задается с помощью файла, который представляет собой таблицу соответствия клавиш хоста и позиций матрицы сканирования. Строки соответствует линиям сканирования, столбцы &ndash; выходным линиям.

Несколько клавиш можно указывать через знак "|". Позиции матрицы разделяются пробелом или табуляцией.

После символа клавиши может быть один из двух модификаторов:
* &laquo;_&raquo; &ndash; Shift принудительно отключается; 
* &laquo;^&raquo; &ndash; Shift принудительно включается; 

Модификаторы используются, когда состояние Shift на эмулируемом компьютере не совпадает с нужным для ввода символа на хосте. Например, при вводе &laquo;:&raquo; Shift нажимается, но на эмулируемой машине этот символ вводится без Shift &ndash; тогда в раскладке на нужной позиции будет &laquo;:_&raquo;, т.е. эта позиция в матрице соответствует двоеточию, но Shift нужно отпустить.

Режим РУС определяется по состоянию входного интерфейса __ruslat_led__. В этом режиме происходит переназначение некоторых клавиш для конвертации раскладки QWERTY в JCUKEN. 

Доступные клавиши:
* 0..9, A..Z, f1..f12
* pgup, pgdown, end, home, left, right, up, down
* ret, ret2, caps, esc, space, back, ins, del, del2
* mult, plus, minus, div, num, scroll 
* ~ - = \ [ ] : ; " / , .
* _ (подчеркивание, синоним &ndash; under)
* ^ (синоним &ndash; circum)
* __ (двойное подчеркивание) &ndash; клавиша в этой позиции отсутствует.

Синонимы under и circum желательно использовать вместо основных символов, чтобы не путать их с модификаторами Shift.

Файл может находиться вместе с другими файлами компьютера, либо в директории __data__.

Пример файла:
~~~
home    tab     0       8|(     @_  H   P   X
del     ret2    1|!     9|)     A   I   Q   Y
esc 	ret     2|"     :_|*    B   J   R   Z
F1  	back    3|#     ;|+     C   K   S   [
F2      left    4|$     ,|<     D   L   T   \
F3      up      5|%     -|=^    E   M   U   ]
F4      right   6|&     .|>     F   N   V   ^_
__      down    7|'^    /|?     G   O   W   space
~~~

### map-keyboard
Клавиатура, записывающая код клавиши в регистр. Соответствие клавиш и полученного кода задается во внешнем файле. 
~~~
keyboard : map-keyboard {
	map = agat.map
	port-value = port-kbd
	ruslat = caps
	port-ruslat = port-kbd-ruslat
	rus-on = 0
	rus-bit = 7
}
~~~

Параметры:
* __map*__: Файл мэппинга клавиш. Должен находиться в каталоге с cfg.
* __port-value*__: Устройство-порт, в которое записывается значение.
* __ruslat__: Кнопка, переключающая раскладку.
* __port-ruslat__: Устройство-порт, в который записывается статус раскладки.
* __rus-on__ (0/1): Значение бита, соответствующее русской раскладке.
* __rus-bit__: Номер используемого бита порта ruslat.

Интерфейсы:
* __~ruslat{1, out}__: Линия Рус-Лат;

Пример файла мэппинга:
~~~
f1:         $84
f2:         $85
f3:         $86

A:          $C1
A/C:        $81
A/S:        $E1
~~~

Формат строки:
~~~
клавиша: код
клавиша/модификаторы: код
~~~

* Модификаторы: C &ndash; Control, S &ndash; Shift. Модификаторы могут комбинироваться.
* Доступные коды клавиш приведены в предыдущем разделе.

### taperecorder

Магнитофон

~~~
tape : taperecorder {
	baudrate = 1200
	files = "Orion-128 tape files (*.rko)"
	~input  = port-keyboard.CL[0]
	~output = port-keyboard.CH[4]
}
~~~

* Параметры
    * __baudrate*__: Скорость работы по умолчанию. 
    * __files__: Фильтр для окна открытия файла. Если не указан, то значение берется из раздела __system__.
* Интерфейсы
    * __~input{1, in}__: Подключение входа.
    * __~output{1, out}__: Подключение выхода.

### i8253

Трехканальный таймер/счетчик Intel 8253, КР580ВИ53. [Документация](https://emuverse.ru/wiki/%D0%9A%D0%A0580%D0%92%D0%9853)

~~~
timer : i8253 {
	clock = 1/1
}
~~~

* Параметры
    * __clock*__: тактовая частота. Может указываться как независимо, так и в долях от системной частоты в формате "множитель/делитель". 
* Интерфейсы
    * __~address{2, in}__: Адрес внутреннего регистра при записи данных.
    * __~data{8, in/out}__: Данные для записи во внутренний регистр.
    * __~output{3, out}__: Выходные линии счетчиков.
    * __~gate{3, in}__: Входы разрешения счета. Если не подключены, то счет всегда разрешен.

Пример использования в качестве трехканального звукового генератора:

~~~
sound : speaker {
	~mixer[0-2] = timer.output[0-2]
}

mapper : memory-mapper {
	portstomemory = 1
	...
	@memory[$EC00-$ECFF] = timer
}
~~~

### i8255 

Контроллер параллельного интерфейса Intel 8255, КР580ВВ55.  [Документация](https://emuverse.ru/wiki/Intel_8255).

~~~
port-user : i8255 {}
~~~

* Интерфейсы
    * __~address{2, in}__: Адрес внутреннего регистра при записи данных.
    * __~data{8, in/out}__: Данные для записи во внутренний регистр.
    * __~A{8, in/out}__: Внешний порт А.
    * __~B{8, in/out}__: Внешний порт B.
    * __~CH{4, in/out}__: Внешний порт CH.
    * __~CL{4, in/out}__: Внешний порт CL.

Пример использования для подключения простого ROM-диска на 64к по адресу $F500:

~~~
romdisk : rom {
	size = 64k
	image = RomDisk1.rom
	~address[0-7]   = port-user.B
	~address[8-11]  = port-user.CL
	~address[12-15] = port-user.CH
	~data = port-user.A
}

mapper : memory-mapper {
	portstomemory = 1
	...
	@memory[$F500-$F5FF] = port-user
}
~~~

### i8257
Контроллер ПДП Intel 8257, КР580ВТ57.  [Документация](https://emuverse.ru/wiki/Intel_8257).

На данный момент функционал ограничен запоминанием данных в регистрах. Записанные данные используются напрямую в устройстве [дисплея на основе i8275](#i8275-display).

~~~
dma : i8257 {}
~~~
* Параметры
    * __clock*__: тактовая частота. Может указываться как независимо, так и в долях от системной частоты в формате "множитель/делитель". 

* Интерфейсы
    * __~address{2, in}__: Адрес внутреннего регистра при записи данных.
    * __~data{8, in/out}__: Данные для записи во внутренний регистр.

### i8275

Видеоконтроллер ПДП Intel 8275, КР580ВГ75. [Документация](https://emuverse.ru/wiki/Intel_8275).

На данный момент функционал ограничен запоминанием данных в регистрах. Записанные данные используются напрямую в устройстве [дисплея на основе i8275](#i8275-display).

~~~
dma : i8275 {}
~~~

* Интерфейсы
    * __~address{2, in}__: Адрес внутреннего регистра при записи данных.
    * __~data{8, in/out}__: Данные для записи во внутренний регистр.

### wd1793

Контроллер дисковода WD1793, КР1818ВГ93. [Документация](https://emuverse.ru/wiki/FDC_1793).

Должен работать в паре с одним или более устройствами fdd. На данный момент эмулируется большинство основных команд, кроме блочных.

~~~
fdc : wd1793 {
	drives = fdd0|fdd1
}
~~~

* Параметры
    * __drives*__: Список подключенных устройств fdd.  

* Интерфейсы
    * __~address{2, in}__: Адрес внутреннего регистра при записи данных.
    * __~data{8, in/out}__: Данные для записи во внутренний регистр.
    * __~intrq{1, out}__: Выход запроса на прерывание.
    * __~drq{1, out}__: Выход готовности данных.
    * __~hld{1, out}__: Выход HLD (обычно используется для включения мотора дисковода). Активный сигнал &ndash; высокий.

### fdd

Дисковод. Должен использоваться совместно с контроллером.

В данный момент поддерживаются только простые посекторные образы.

~~~
fdd0 : fdd {
    sides = 2
    tracks = 80 
    sectors = 5 
    sector_size = 1k
    selector_value = 0
    files = "Образы дисков Орион-128 (*.odi)|*.odi"
    image= orion-128/Disk2.ODI
    ~select  = port-fdc.value[0-1]
    ~side    = port-fdc.value[4]
    ~density = port-fdc.value[6]
    ~motor_on = !fdc.hld
}
~~~

* Параметры
    * __sides*__: Количество рабочих сторон (1/2).
    * __tracks*__: Количество дорожек.
    * __sectors*__: Количество секторов.
    * __sector_size*__: Размер сектора.
    * __selector_value*__: Значение интерфейса __~select__ для выбора устройства (0-3).
    * __files*__: Фильтр для окна выбора файлов образов.
    * __image__: Файл образа, загруженный при старте системы. 
        * Файл должен находиться в одном из следующих мест: 
            * В том же каталоге, что и __.cfg__;
            * В подкаталоге __files__ рядом с .cfg;
            * В подкаталоге __software__, совпадающем с подкаталогом __.cfg__.

* Интерфейсы
    * __~select{2, in}__: Выбор устройства.
    * __~side{1, in}__: Выбор стороны.
    * __~density{1, in}__: Выбор плотности.
    * __~motor_on{1, in}__: Включение мотора. Активный уровень &ndash; низкий.

## Машино-зависимые устройства

### orion-128-display

Дисплейный контроллер Орион-128.

~~~
display : orion-128-display {
	mode = port-videomode
	screen = port-screen
	rmain = ram0
	color = ram1
}
~~~

* Параметры
    * __mode*__: Порт видеорежима.
    * __screen*__: Порт выбора видеостраницы.
    * __rmain*__: Устройство ram основного экрана.
    * __color*__: Устройство ram атрибутов цвета.

### agat-display

Дисплейный контроллер Агат-7.

~~~
display : agat-display {
	ram = ram0
	mode = port-video
	font = font 
}
~~~

* Параметры
    * __ram*__: Устройство ram видеопамяти.
    * __mode*__: Порт видеорежима.
    * __font*__: Устройство типа rom со знакогенератором.

### agat-fdc140

Контроллер дисковода Агат 140 Кб.

~~~
fdc : agat-fdc140 {
	drives = fdd0
}
~~~

* Параметры
    * __drives*__: Список подключенных устройств fdd.  

* Интерфейсы
    * __~motor_on{1, out}__: Выход включения мотора дисковода. Активный сигнал &ndash; высокий.

### i8275-display

Универсальный дисплейный контроллер на основе i8275. Поддерживаются различные варианты генерации цвета.

~~~
display : i8275-display {
	ram  = ram
	i8275 = vg75
	dma = dma
	channel = 2
	font = font
	attr_delay = 1
	rgb = ^032
	~high = cpu.inte
}
~~~

* Параметры
    * __ram*__: Устройство ram видеопамяти.
    * __i8275*__: Устройство i8275, из которого берутся параметры изображения.
    * __dma*__: Устройство i8257, из которого берутся параметры изображения.
    * __channel*__: Номер используемого канала DMA i8257.
    * __font*__: Устройство типа rom со знакогенератором.
    * __attr_delay__: Задержка установки атрибута символа (0/1).
    * __rgb__: Форма преобразования байта атрибутов в цвет RGB. Цифры означают номера битов, символ "^" в начале инвертирует конечное значение.


* Интерфейсы
    * __~high{1, in}__: Выбор секции знакогенератора. _Да, пример взят из Апогея, и там знакогенератор переключается выходом INTE процессора!_ 🤦‍♂️
