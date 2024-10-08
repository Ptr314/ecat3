# Орион-128
## Исходный вариант из журнала c Монитором-1

Вариант из журнала &laquo;Радио&raquo; за 1990-й год. Данная конфигурация добавлена в исторических целях, работать с ней неудобно, многие поздние программы несовместимы с М1.

Поддерживаются файлы: *.rko, *.bru, *.ord.

## Работа на компьютере

### Загрузка файлов:

Вариант 1 (единственный): Прямая загрузка в память.

1. Загрузить файл через меню эмулятора __Файл/Открыть файл__;
2. Запустить на выполнение директивой монитора G:<br/>
<b>--&gt;G</b><br/>
или, если программа загружается не с адреса 0000:<br/>
<b>--&gt;G[адрес]</b>

Адрес загрузки можно посмотреть в файле &ndash; это два байта по смещению $55 в файлах .rko, либо по смещению 8 в файле .bru.

О работе в Мониторе-1 можно прочитать в соответствующей [статье в журнале &laquo;Радио&raquo;](https://emuverse.ru/wiki/Орион-128/Радио_02-90/Программное_обеспечение).

Теоретически, можно загрузить ORDOS с ROM-диска директивой R, но данный вариант сейчас работает некорректно. Причина &ndash; ошибки эмуляции или несовместимость программ с М1 &ndash; пока неизвестна.

## Прочее

Подробнее о компьютере можно прочитать:

* [Статья в Википедии](https://ru.wikipedia.org/wiki/Орион-128)

Техническая документация и программы:

* [На сайте Emuverse.ru](https://emuverse.ru/wiki/Орион-128)
