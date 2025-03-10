# Руководство пользователя

* [Установка программы](#установка-программы)
    * [Windows](#windows)
    * [macOS](#macos)
    * [Linux](#linux)
* [Главное окно](#главное-окно)
* Основные операции
    * [Выбор компьютера](#выбор-компьютера)
    * [Перезагрузка](#перезагрузка)
* Работа с файлами
    * [Прямая загрузка в память](#прямая-загрузка-в-память)
    * [Дисководы](#дисководы)
    * [Загрузка с магнитофона](#загрузка-с-магнитофона)
* Средства отладки
    * [Состояние устройств](#состояние-устройств)
    * [Основной отладчик](#основной-отладчик)


## Установка программы

### Windows
* Скачайте архив из раздела [Releases](../../releases). Для Windows 7 используйте файл `ecat-3.X.X-windows-i386.zip`, для Windows 10+ &ndash; `ecat-3.X.X-windows-x86_64.zip`.
* Распакуйте архив и запустите ecat3.exe.

### macOS

Для правильной работы клавиатуры необходимо оставлять фокус на окне с меню и кнопками.

* Скачайте файл dmg из раздела [Releases](../../releases).
* Скачайте библиотеку SDL2 из её [репозитория](https://github.com/libsdl-org/SDL/releases). Используйте файл dmg.
* Откройте dmg-файл библиотеки и перетащите иконку SDL2.framework в системную папку `/Library/Frameworks`.
* Откройте dmg-файл программы. При первом запуске система сообщит, что запретила выполнение библиотеки SDL. Перейдите в системные настройки ОС, раздел Безопасность, и разрешите использование библиотеки.
* Запустите программу еще раз. При необходимости подтвердите разрешение в отрывшемся окне.

### Linux

Для правильной работы клавиатуры необходимо оставлять фокус на окне с меню и кнопками.

* Скачайте файл `ecat-3.X.X-linux-x86_64.AppImage` из раздела [Releases](../../releases), сделайте его исполняемым и запустите.

## Главное окно
<p align="center">
<img src="screenshots/main_window.png" width="600">
</p>

## Основные операции

### Выбор компьютера

<p align="center">
<img src="screenshots/choose_machine.png" width="600">
</p>

* Вариант 1: с помощью кнопки <img src="src/resources/icons/tv.png" width="30"> на панели главного окна.
* Вариант 2: С помощью пункта меню __&laquo;Файл/Выбор компьютера...&raquo;__.

Если перед нажатием __&laquo;ОК&raquo;__ отметить флажок __&laquo;Установить по умолчанию&raquo;__, данная машина будет открываться по умолчанию при следующем запуске эмулятора.

### Перезагрузка

* Холодный перезапуск (с очисткой памяти) с помощью кнопки <img src="src/resources/icons/power.png" width="30"> или соответствующего пункта меню &laquo;Эмуляция&raquo;.
* Теплый перезапуск (без очистки памяти) с помощью кнопки <img src="src/resources/icons/reload.png" width="30"> или соответствующего пункта меню &laquo;Эмуляция&raquo;.

## Работа с файлами

### Прямая загрузка в память

Результат будет отличаться в зависимости от выбранного компьютера. В общем случае файл будет напрямую загружен в память, минуя все программные механизмы эмулируемого компьютера.

Подробности смотрите в описании компьютера при его [выборе в соответствующем окне](#выбор-компьютера).

* Вариант 1: с помощью кнопки <img src="src/resources/icons/cdrom_mount.png" width="30"> на панели главного окна.
* Вариант 2: С помощью пункта меню __&laquo;Файл/Открыть файл...&raquo;__.

### Дисководы

<p align="center">
<img src="screenshots/fdd_menu.png" width="400">
</p>

Если в конфигурации компьютера есть дисководы, то в панели управления появляется одна или несколько кнопок <img src="src/resources/icons/5floppy_mount.png" width="30">. У каждой кнопки есть дополнительное меню, с помощью которого можно посмотреть, какой образ загружен, загрузить другой файл, сохранить измененный образ на диск. 

Нажатие на саму иконку позволяет быстро выбрать образ, не открывая меню.

В моменты обращения компьютера к диску стрелка на иконке становится красной: <img src="src/resources/icons/5floppy_access.png" width="30">

Примечание: возможность сохранения образа в различные форматы зависит от выбранного компьютера. В частности, для компьютера &laquo;Агат&raquo; пока есть возможность сохранения только в физическом формате MFM.

### Загрузка с магнитофона

<p align="center">
<img src="screenshots/Electronica-302.png" width="600"><br/>
<small>Автор изображения &ndash; <a href="http://www.mcclaud.ru">McClaud</a></small>
</p>

Иконка магнитофона появляется в панели, если магнитофон есть в конфигурации компьютера.

1. Открыть окно эмулятора магнитофона кнопкой <img src="src/resources/icons/kdat.png" width="30"> в главном окне программы;
2. Открыть файл кнопкой &laquo;СТОП/ВЫБРОС&raquo; (самая правая с синим индикатором);
3. Запустить воспроизведение кнопкой &laquo;ПУСК&raquo;;
4. Действия в эмулируемой системе описаны в документации к конкретной машине.
5. Другие действия:
    * Кнопка &laquo;ОСТАНОВ&raquo; &ndash; пауза;
    * Кнопка &laquo;СТОП/ВЫБРОС&raquo; &ndash; при воспроизведении &ndash; остановка, второе нажатие &ndash; открыть файл;
    * Кнопка &laquo;ПЕРЕМОТКА ВЛЕВО&raquo; &ndash; сброс текущей позиции файла на начало.
    * Регулятор &laquo;ГРОМКОСТЬ&raquo; &ndash; заглушить звук.

## Средства отладки

### Состояние устройств
<p align="center">
<img src="screenshots/menu_devices.png" width="600">
</p>

Все доступные устройства перечислены в меню &laquo;Эмуляция/Устройства&raquo;. Те устройства, для которых доступно отладочное окно, выводятся черным цветом. Если отладочное окно недоступно &ndash; серым.

Отладочное окно процессора является одновременно основным отладчиком в системе.

### Основной отладчик

<p align="center">
<img src="screenshots/debug_z80.png" width="600"><br/>
<small>Окно отладчика для процессора Z80</small>
</p>

Основной отладчик может быть вызван тремя способами:

1. С помощью кнопки <img src="src/resources/icons/terminal.png" width="30"> на панели главного окна.
2. С помощью пункта меню __&laquo;Эмуляция/Отладчик&raquo;__.
3. Через меню &laquo;Эмуляция/Устройства&raquo;, если открыть окно отладки устройства CPU.

Окно отладчика содержит следующие элементы:

* Панель состояния с кнопками управления;
* Панель дизассемблера;
* Две панели состояния регистров и флагов процессора;
* Панель дампа памяти (так, как память видит сам процессор с учетом диспетчера).

#### Панель состояния

Верхняя панель состоит из четырех блоков:

* Символ режима работы процессора:
    * <img src="src/resources/icons/player_play.png" width="30"> &ndash; процессор запущен без отладки;
    * <img src="src/resources/icons/player_pause.png" width="30"> &ndash; процессор остановлен;
    * <img src="src/resources/icons/debug.png" width="30"> &ndash; процессор запущен под отладчиком, отслеживаются точки останова и состяние регистров;
    
* Блок управления выполнением:
    * <img src="src/resources/icons/undo.png" width="30"> &ndash; возврат курсора панели дизассемблера на текущую точку исполнения;
    * <img src="src/resources/icons/forward.png" width="30"> &ndash; шаг внутрь (с заходом в подпрограмму), короткая клавиша F7;
    * <img src="src/resources/icons/step_over.png" width="30"> &ndash; шаг поверх (без захода в подпрограмму), короткая клавиша F8;
    * <img src="src/resources/icons/finish.png" width="30"> &ndash; выполнить до курсора, короткая клавиша F4;
    * <img src="src/resources/icons/player_play.png" width="30"> &ndash; запустить под отладчиком (с отслеживанием точек останова), короткая клавиша F9;
    * <img src="src/resources/icons/player_pause.png" width="30"> &ndash; останов процессора;
    * <img src="src/resources/icons/player_fwd.png" width="30"> &ndash; запуск без отладки.
    
* Блок управления панелью дизассемблера:
    * Строка ввода адреса или значения регистра; 
    * <img src="src/resources/icons/next.png" width="30"> &ndash; переключение панели дизассемблера на введенный адрес (установка курсора);
    * <img src="src/resources/icons/edit_add.png" width="30"> &ndash; установка точки останова на адрес под курсором. (Для установки точки сначала необходимо нажать кнопку установки курсора на нужный адрес!);
    * <img src="src/resources/icons/edit_remove.png" width="30"> &ndash; удаление точки останова с адреса под курсором. (Курсор должен стоять на нужном адресе!);
    * Выпадающий список с именами регистров;
    * <img src="src/resources/icons/button_accept.png" width="30"> &ndash; запись значения в выбранный регистр.

* Блок управления панелью дампа памяти:
    * Строка ввода адреса;
    * <img src="src/resources/icons/next.png" width="30"> &ndash; переключение панели дампа на введенный адрес;
    * <img src="src/resources/icons/2downarrow.png" width="30"> &ndash; переход на страницу вниз;
    * <img src="src/resources/icons/2uparrow.png" width="30"> &ndash; переход на страницу вверх.
