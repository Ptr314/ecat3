# Настройка окружения и компиляция приложения

В данный момент программа компилируется под следующие платформы:

* Windows XP+
    * Версия i386 на основе Qt 5.6.3 и mingw 4.9.2.
* Windows 10+
    * Версия х86_64. Актуальная версия Qt 6.8.3 и mingw 13.10.
* macOS 15 (возможна совместимость с более ранними версиями)
    * Универсальная версия х86_64+arm64. Qt 6.8.2, xcode 16
* Linux Ubuntu 20.04+
    * Версия х86_64. Qt 6.8.2, gcc 9.4.0

Версии х86_64 для Windows и х86_64+arm64 для macOS используют статическую сборку. Версия для Linux использует динамическую сборку в целях лучшей совместимости с разными дистрибутивами. Компиляция происходит в Ubuntu 20.04. 

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
    * Скачать https://www.libsdl.org/ (development version)
    * Распаковать
    * Добавить путь к переменной окружения CMAKE_PREFIX_PATH

#### 2. Настроить debug-версию в Qt Creator
* Если нужна полная очистка, удалить файлы __CMakeLists.txt.user*__.
* Перейти в настройку для запуска (__Projects/Build & Run/Debug kit/Run__):
    * Установить __Working directory__ как __"репозиторий-приложения\deploy"__.

#### 3. Компиляция статической версии Qt

##### Qt6 (актуальная версия)

https://doc.qt.io/qt-6/windows-building.html

* Отредактировать `.build/vars-mingw-latest.cmd` на действительные пути.
* Открыть командную строку и скомпилировать Qt:

```
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-mingw-latest.cmd
cd C:\Temp
mkdir qt-build
cd qt-build
configure.bat -static -static-runtime -release -opensource -confirm-license -nomake examples -nomake tests -prefix c:\DEV\Qt\%_QT_VERSION%-static
cmake --build . --parallel
cmake --install .
```

##### Qt5 для Windows XP

Для XP необходима версия Qt 5.6.3 и mingw 4.9.2 (https://download.qt.io/new_archive/qt/5.6/5.6.3/single/)

~~~
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-mingw-qt5.6.cmd
cd C:\Temp
mkdir qt5.6-build
cd qt5.6-build
configure.bat -release -nomake examples -nomake tests -opensource -confirm-license -no-opengl -target xp -no-directwrite -no-compile-examples -prefix c:\DEV\Qt\%_QT_VERSION%
mingw32-make
mingw32-make install
~~~

Примечания: 
* `-target xp` необходимо для компиляции в формат .exe Windows XP.
* Собрать статическую версию не удается, поэтому в дальнейшем необходимо в папку программы помещать следующие файлы:
    * `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll` из `Qt/5.6.3/bin/`
    * `platforms/qwindows.dll` из `Qt/5.6.3/plugins`
    * `libgcc_s_dw2-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` из `Qt/Tools/mingw492_32/bin`

#### 4. Обновление языковых файлов

~~~
cd .build
update_translations.bat
~~~

* Переменная BUILD_DIR в bat-файле должна указывать на build-директорию, установленную в конфигурации проекта.
* Команду надо выполнять на той же платформе, где происходил препроцессинг CMakeLists.txt.
* Далее файлы .ts редактируются с помощью Linguist из Qt Creator.

#### 5. Сборка release-версии
* Проверить, что в Qt Creator/Kits/Compilers есть компилятор, который использовался для сборки Qt.
* Добавить собранную версию Qt в Qt Creator/Kits/Versions и Qt Creator/Kits/Kits. Версия компилятора должна соответствовать версии, с которой происходила сборка Qt.
* Обновить версию приложения в CMakeLists.txt, пересканировать проект (Rescan project), чтобы версия прописалась в заголовочные файлы.
* Закоммитить изменения.
* Откомпилировать приложение нужной версией Qt.
    * актуализировать значения переменных в `/build/vars-mingw-*.cmd`. 
    * i386: `./build/build-win-i386.bat`.
    * x86_64: `./build/build-win-latest.bat`.
* Упаковать директории в `./build/release` в zip.
* Загрузить как релиз на GitHub, добавив последнему коммиту тег с номером версии.

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



# Полезные ссылки

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online disassembler: https://86rk.ru/disassm/
