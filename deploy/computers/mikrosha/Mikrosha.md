# Микроша
## Заводской вариант Радио-86РК

От оригинала отличался, в основном, наличием генератора звука на основе таймера КР580ВИ53, а также измененной схемой подключения внутренних устройств, в том числе клавиатуры. Из-за этого большинство программ Радио-86РК без доработки на Микроше не работают.

Поддерживаются файлы: *.rkm

## Работа на компьютере

### Загрузка файлов:

Вариант 1 (быстро): Прямая загрузка в память.
1. Загрузить файл через меню эмулятора Файл/Открыть файл;
2. Запустить на выполнение директивой монитора G:<br/>
-->G<ввод><br/>
или, если программа загружается не с адреса 0000:<br/>
-->G<адрес><ввод>

Адрес загрузки можно посмотреть в файле &ndash; это первые два байта в файлах .rkm.

<!--
Вариант 2 (для ценителей): Загрузка через магнитофон.

НЕ РАБОТАЕТ. Нужно разобраться с форматом вывода, он отличается от РК-86.
-->

### Полезные директивы монитора:
* G<адрес> &ndash; запуск программы по адресу <адрес>;
* D<начало>,<конец> &ndash; просмотр содержимого блока памяти;
* М<адрес> &ndash; просмотр и изменение содержимого ячейки памяти. Выход  из реадктирования - символ "." (точка).

## Прочее

Подробнее о компьютере можно прочитать:

* [Статья в Википедии](http://ru.wikipedia.org/wiki/Микроша)

Техническая документация и программы:

* [На сайте Emuverse.ru](http://www.emuverse.ru/wiki/Микроша)