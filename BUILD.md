# Настройка окружения и компиляция приложения

В данный момент программа компилируется под следующие платформы:

* Windows XP+
    * Версия i386 на основе Qt 5.6.3 и mingw 4.9.2.
* Windows 7+
    * Версия i386 на основе Qt 5.15.2 и mingw 8.1.0.
* Windows 10+
    * Версия х86_64. Актуальная версия Qt 6.10.2 для mingw 13.10 и MSVC2022.
* macOS 15 (возможна совместимость с более ранними версиями)
    * Универсальная версия х86_64+arm64. Qt 6.8.2, xcode 16
* Linux Ubuntu 20.04+
    * Версия х86_64. Qt 6.8.2, gcc 9.4.0
* Браузер
    * Версия WebAssembly на основе Emscripten. Qt не используется.

## Варианты сборки

Помимо выбора рендерера у сборки есть три ключа, все с безопасными значениями
по умолчанию, так что обычный выпуск ими не затрагивается:

| Ключ | По умолчанию | Что делает |
|---|---|---|
| `ENABLE_GUI` | `ON` | Оконное приложение. При `OFF` Qt не ищется вовсе, и проект конфигурируется на машине без Qt |
| `ENABLE_HEADLESS` | `OFF` | Вторая цель `eCat3-headless`: консольное приложение из тех же исходников, без Qt. Прогоняет сценарии `.ecat` и снимает экран, но не показывает его. Требует C++17, поэтому киты для XP и Windows 7 его не собирают |
| `ENABLE_MCP` | `OFF` | Управление эмулятором снаружи по протоколу MCP, см. [MCP.md](MCP.md). Без ключа ни один файл этой возможности не компилируется. В оконном варианте поддерживается только `RENDERER_OPENGL` |

Консольная сборка, заодно с MCP-сервером:

```
cmake -S src -B build-headless -G Ninja -DCMAKE_BUILD_TYPE=Release       -DENABLE_GUI=OFF -DENABLE_HEADLESS=ON -DENABLE_MCP=ON
cmake --build build-headless
```

У скриптов выпуска для современных платформ появились аргументы `mcp` и
`headless`, см. [.build/README.md](.build/README.md).

Версии х86_64 для Windows 10+ и х86_64+arm64 для macOS используют статическую сборку. Версия для Linux использует динамическую сборку в целях лучшей совместимости с разными дистрибутивами. Компиляция происходит в Ubuntu 20.04. 

## Windows

### MINGW

#### 1. Установить программы
* https://download.qt.io/, скачать online-инсталлятор (возможно, из России понадобится зарубежный VPN) и установить следующие компоненты:
    * Qt [X.X.X]
        * MinGW YY.Y.Y
        * Sources
    * Qt Developer and Designer tools
        * Qt Creator
        * Mingw YY.Y.Y (Версия, соответствующая компилятору библиотеки в предыдущем пункте)
        * Mingw 8.1.0 (Версия для сборки i386 для Windows 7)
        * Mingw 4.9.2 (Версия для сборки i386 для Windows XP)
        * cmake
        * ninja
* Python 3
* SDL
    * Скачать SDL версии 2 с https://www.libsdl.org/ (development version)
    * Распаковать
    * Добавить путь к переменной окружения CMAKE_PREFIX_PATH

#### 2. Настройка IDEs

##### 2.1 Настройка Qt Creator
* Если нужна полная очистка, удалить файлы __CMakeLists.txt.user*__.
* Перейти в настройку для запуска (__Projects/Build & Run/Debug kit/Run__):
    * Установить __Working directory__ как __"репозиторий-приложения\deploy"__.

##### 2.2 Настройка CLion
* Settings / Build, Execution, Deployment / Toolchains:
  * Добавить тулчейны MinGW. Toolset &ndash; директория `C:\DEV\Qt\Tools\mingw1310_64` (пример).
  * Добавить тулчейны MSVC (Тип System). Environment file &ndash; `C:\DEV\MSVC\msvc\setup_x64.bat` (пример).
* Settings / Build, Execution, Deployment / CMake profiles
  * Добавить профиль для каждого тулчейна:
    * CMake options: `-DCMAKE_PREFIX_PATH=C:\DEV\Qt\6.10.2\mingw_64;C:\DEV\SDL2-2.32.10` (пример).
  * Для обновления языковых файлов (в одном из тулчейнов) добавить `-DUPDATE_TRANSLATIONS=ON`.
* Run/Debug configurations
  * Создать конфигурацию для каждого тулчейна:
    * Working directory &ndash; deploy-директория проекта.
    * Environment variables (MinGW): `PATH="C:\\DEV\\Qt\\6.10.2\\mingw_64\\bin;C:\\DEV\\SDL2-2.32.10\\x86_64-w64-mingw32\\bin;%PATH%"` (пример).
    * Environment variables (MSVC): `PATH="C:\\DEV\\Qt\\6.10.2\\msvc2022_64\\bin;C:\\DEV\\SDL2-2.32.10\\msvc2022\\lib\\x64;%PATH%";QT_PLUGIN_PATH=C:\DEV\Qt\6.10.2\msvc2022_64\plugins` (пример).

#### 3. Компиляция статической версии Qt

##### Установка portable-версии MSVC

* Скачать `portable-msvc.py` с [GitHub](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977)

```
python .\portable-msvc.py --msvc-version 14.43 --sdk-version 26100 --accept-license
```
В данном случае будут установлены MSVC2022 17.13 и Windows 11 24H2 SDK. Документацию по скрипту см. на его странице.

##### Компиляция Qt6 (актуальная версия)

https://doc.qt.io/qt-6/windows-building.html

* Отредактировать `.build/vars-mingw-latest.cmd` или `.build/vars-msvc-latest.cmd` на действительные пути.
* Настроить окружение Python 3.
* Открыть командную строку и скомпилировать Qt:

```
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-msvc-latest.cmd
C:\DEV\venv\Scripts\activate.bat
mkdir C:\Temp\qt-build
cd C:\Temp\qt-build
configure.bat -static -static-runtime -release -opensource -confirm-license -nomake examples -nomake tests -submodules qtbase,qttools,qttranslations -platform win32-msvc -prefix %_QT_PREFIX_STATIC%
cmake --build . --parallel
cmake --install .
```

##### Qt5 для Windows 7

Для Windows 7 необходима версия Qt 5.15 и mingw 8.1.0 (https://download.qt.io/archive/qt/5.15/5.15.16/single/)

~~~
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-mingw-qt5.15.cmd
cd C:\Temp
mkdir qt5.15-build
cd qt5.15-build
configure.bat -release -nomake examples -nomake tests -opensource -confirm-license -no-opengl -skip qtlocation -skip qtdeclarative -prefix %_QT_PREFIX%
mingw32-make
mingw32-make install
~~~

Примечания: 
* `-no-opengl` используется для исключения установки OpenGL SDK.
* `-skip qtdeclarative` позволяет избежать необходимости в установке Python, но отключает модули QtQuick, QtQml и некоторые другие.
* `-skip qtlocation` исключает непонятную ошибку компиляции в этом модуле

##### Qt5 для Windows XP

Для XP необходима версия Qt 5.6.3 (https://download.qt.io/new_archive/qt/5.6/5.6.3/single/) и mingw 4.9.2 (https://wiki.qt.io/MinGW)

```
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-mingw-qt5.6.cmd
mkdir C:\Temp\qt5.6-build
cd C:\Tempqt5.6-build
configure.bat -release -nomake examples -nomake tests -opensource -confirm-license -no-opengl -target xp -no-directwrite -no-compile-examples -skip qtwebengine -skip qtwebview -skip qtandroidextras -skip qt3d -skip qtcanvas3d  -skip qtlocation -skip qtscript -skip qtsensors -skip qtserialbus -skip qtwayland -skip qtdeclarative -prefix %_QT_PREFIX%
mingw32-make
mingw32-make install
```

#### 4. Обновление языковых файлов

__Вариант 1__ (предпочтительный): выполнить предварительную компиляцию с флагом cmake `-DUPDATE_TRANSLATIONS=ON`, после чего обновленные языковые файлы отредактировать утилитой Qt Linguist. Далее откомпилировать итоговый вариант. 

__Вариант 2__: выполнить bat-файл:
~~~
cd .build
update_translations.bat
~~~

* Build-директория, установленная в конфигурации проекта, передаётся аргументом: `update_translations.bat build\Desktop_Qt_6_11_2_MinGW_64_bit-Debug`. Без аргумента используется значение по умолчанию, заданное в bat-файле.
* Команду надо выполнять на той же платформе, где происходил препроцессинг CMakeLists.txt.
* Далее файлы .ts редактируются с помощью Linguist из Qt Creator.

#### 5. Сборка release-версии для Windows
* Обновить версию приложения в CMakeLists.txt, пересканировать проект (Rescan project), чтобы версия прописалась в заголовочные файлы.
* Закоммитить изменения.
* Откомпилировать приложение нужной версией Qt.
    * актуализировать значения переменных в `.build/vars-mingw-*.cmd`, `.build/vars-msvc-*.cmd`. 
    * Windows XP: `./.build/build-win-i386.bat`.
    * Windows 7: `./.build/build-win-7.bat`.
    * Windows 10+: `./.build/build-win-mingw-latest.bat` или `./.build/build-win-msvc-latest.bat`.

  Каждый скрипт собирает все свои рендереры и упаковывает по zip-архиву на рендерер в `.build/release/`. Аргумент `clean` (например `build-win-msvc-latest.bat clean`) предварительно удаляет build-директории.

---
## macOS

https://doc.qt.io/qt-6/macos.html

Далее описывается установка окружения из offline-инсталляторов, так как сетевая установка под виртуальными машинами работала нестабильно.

### 1. Установить xcode

Дистрибутив взять здесь: https://xcodereleases.com, нужен аккаунт на Apple Developer.

* Скопировать файл `.xip` в папку `/Applications` и там распаковать. Файл `.xip` удалить
* Выполнить команду `sudo xcode-select --switch /Applications/Xcode.app`
* Закомментировать `helper = osxkeychain` в файле `/Applications/Xcode.app/Contents/Developer/usr/share/git-core/gitconfig`, чтобы отключить хранение паролей Git в локальной системе.

### 2. Установить HomeBrew

https://brew.sh/

### 3. Установить утилиты

cmake, ninja, принять лицензию xcode:

```
brew install cmake
brew install ninja
sudo xcodebuild -license
```

### 4. Установить Qt

Если нужна только компиляция, то Qt Creator можно не устанавливать.

С https://download.qt.io/official_releases/qtcreator/latest/ скачать Qt Creator Offline Installer и с https://download.qt.io/official_releases/qt/ Qt Sources (qt-everywhere-src-X.X.X.tar.xz)
* Установить Qt Creator обычным образом (открыть файл `.dmg`, перетащить иконку в `/Applications`).
* qt-everywhere-src-X.X.X.tar.xz поместить в ~/Downloads

### 5. Собрать статическую версию Qt

Для сборки универсальной статической версии Qt x86_64+arm64 используйте скрипт `.build/macos_build_qt_universal.sh`. Данный скрипт исходит из следующих условий:
* Архив с исходными файлами лежит в `~/Downloads/`.
* Распаковка происходит в `/tmp`.
* Установка происходит в `~/Qt-$QT_VERSION-static-universal`.
* После установки можно отдельно скопировать Qt в `/usr/local` и установить системные пути при необходимости.

Перед запуском нужно актуализировать пути в первых строках файла.

После перезагрузки системы `/tmp` очищается, поэтому для повторного запуска надо распаковывать исходники заново.

### 6. Добавить Kit в Qt Creator 

Из папки `~/Qt-$QT_VERSION-static-universal` или `/usr/local/Qt-X.X.X-static` (включить отображение скрытых папок при необходимости).

### 7. Установка SDL2

https://www.csalmeida.com/log/how-to-install-sdl2-on-macos/

* Скачать SDL2-X.X.X.dmg по адресу https://github.com/libsdl-org/SDL/releases
* Скачать SDL2_image-Y.Y.Y.dmg https://github.com/libsdl-org/SDL_image/releases
* Поочередно открыть оба архива и перетащить парку Framework в `/Library/Frameworks`.
* После первого запуска приложения перейти в системные настройки и в разделе безопасности разрешить работу библиотеки.

### 8. Сборка приложения

Для сборки приложения используется скрипт `.build/build-macos.sh`. Перед первым запуском необходимо актуализировать следующе переменные: QT_PATH.

На выходе должен быть получен файл `.dmg`.

---
## Ubuntu 20.04

В целях совместимости, для сборки выбирается самая старая версия из текущих на поддержке, на 12.2024 это Ubuntu 20.04. В более новых версиях не запустится linuxdeployqt.

#### 1. Установить программы
* https://download.qt.io/, скачать online-инсталлятор (возможно, из России понадобится зарубежный VPN) и установить следующие компоненты:
    * Qt [X.X.X]
        * Desktop
        * Sources 
        * Plugins
            * Qt5Compatibility 
    * Qt Developer and Designer tools
        * Qt Creator
        * cmake
        * ninja
* Компилятор `sudo apt install g++`
* SDL2 `sudo apt install libsdl2-dev`
* Скачать linuxdeployqt: https://github.com/probonopd/linuxdeployqt/releases и разместить в `~/Downloads`.

Добавить в `~/.profile` пути к cmake и ninja:
```
PATH="~/Qt/Tools/Cmake/bin:~/Qt/Tools/Ninja:${PATH}"
```

Если cmake выводит ошибку вида `Qt6Gui could not be found because dependency WrapOpenGL could not be found.`, поставить библиотеку:

```
sudo apt install libgl1-mesa-dev
```

#### 2. Настроить Kit в Qt Creator
* Если нужна полная очистка, удалить файлы __CMakeLists.txt.user*__.

#### 3. Сборка приложения

Для сборки приложения используется скрипт `.build/build-linux.sh`. Перед первым запуском необходимо актуализировать следующе переменные: QT_PATH и LINUXDEPLOYQT.

cmake и ninja должны быть в PATH (см. п. 1).

На выходе должен быть получен файл `.AppImage`.


---
## WebAssembly (браузер)

Сборка для браузера не пользуется Qt вовсе: в неё идёт только Qt-независимое
ядро эмулятора (`src/emulator/`) плюс страница и склейка из `src/wasm/`.
Поэтому и цепочка тут своя — Emscripten, — и результат не выпуск, а **пакет
для выкладки**: каталог со статикой, который кладётся в корень сайта как
есть.

### 1. Установить Emscripten

```
git clone https://github.com/emscripten-core/emsdk.git C:\DEV\emsdk
cd C:\DEV\emsdk
emsdk install latest
emsdk activate latest
```

Свой Python и node emsdk ставит сам, отдельно их не нужно. CMake и Ninja
берутся из поставки Qt, как и в остальных скриптах.

Путь к emsdk прописан в `.build/vars-emsdk.cmd` — если каталог другой,
править надо только там. Под Unix достаточно `source emsdk_env.sh` перед
запуском.

### 2. Сборка

```
cd .build
build-wasm.cmd          # под Unix: ./build-wasm.sh
```

Аргумент `clean` предварительно удаляет build-директорию.

На выходе — `.build/release/ecat-<версия>-web/` и тот же каталог в zip
рядом. Конфигурации машин, ПЗУ и образы дисков из `deploy/` упаковываются
в отдельный `.bundle` на каждую машину (`src/wasm/package_machines.py`), и
страница тянет только тот, который выбрали. Чтобы добавить машину, править
страницу или скрипт сборки не нужно — достаточно положить `.cfg` в
`deploy/computers/`.

### 3. Выкладка

**Двух заголовков не миновать.** Эмулятор собран с потоками (`-pthread`), а
`SharedArrayBuffer` доступен только на изолированном происхождении:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Без них страница загрузится и молча встанет на инициализации модуля. Из
файловой системы (`file://`) она не заведётся вовсе. Готовый пример
настройки nginx с пояснениями лежит в самом пакете
(`nginx.conf.example`), исходники — в `.build/web/`.

Для пробы на своей машине:

```
python src/wasm/serve_wasm.py .build/release/ecat-<версия>-web 8080
```

Обычный `python -m http.server` не годится: он этих заголовков не ставит.


# Полезные ссылки

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online disassembler: https://86rk.ru/disassm/
