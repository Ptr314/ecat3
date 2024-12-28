# Настройка окружения и компиляция приложения

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
        * cmake
        * ninja
* Python 3
* SDL
    * Скачать https://www.libsdl.org/ (development version)
    * Распаковать
    * Добавить путь к переменной окружения CMAKE_PREFIX_PATH

#### 2. Настроить debug-версию в Qt Creator
* Если нужна полная очистка, удалить файлы __CMakeLists.txt.user*__.
* Перейти в настройку для сборки (__Projects/Build & Run/Debug kit/Build__):
    * Выбрать вариант __"Build configuration"__ &ndash; __"Debug"__. 
    * Установить целевую директорию для компиляции __"репозиторий-приложения\build-debug"__ (__Build directory__).
* Перейти в настройку для запуска (__Projects/Build & Run/Debug kit/Run__):
    * Установить __Working directory__ как __"репозиторий-приложения\deploy"__.

#### 3. Скомпилировать Qt из исходников для статической сборки приложения

https://doc.qt.io/qt-6/windows-building.html

* Отредактировать __.build/vars-mingw.cmd__ на действительные пути.
* Открыть командную строку и скомпилировать Qt: 

~~~
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-mingw.cmd
cd C:\Temp
mkdir qt6-build
cd qt6-build
configure.bat -static -release -nomake examples -nomake tests -prefix c:\DEV\Qt-static\%_QT_VERSION%
cmake --build . --parallel
cmake --install .
~~~

#### 4. Настроить Qt Creator для использования статической сборки
Если не получается добавить Kit из-за отсутствующих библиотек или падения qmake, нужно проверить системные пути, чтобы они указывали на bin-директорию актуальной версии mingw.

* __Edit/Preferences/Kits/Qt versions/Add__ &ndash; выбрать qmake.exe из статической сборки, Name: __"Qt %{Qt:Version} Static"__;
* __Edit/Preferences/Kits/Kits/Add__ &ndash; __Qt version__ из предыдущего пункта.


#### 5. Собрать release-версию приложения
* Перейти в настройку компиляции из предыдущего пункта (__Projects/Build & Run/Static Kit/Build__)
* Выбрать вариант __"Build configuration"__ &ndash; __"Release"__. 
* Установить целевую директорию для компиляции __"репозиторий-приложения\build-release"__ (__Build directory__).
* Скомпилировать приложение с помощью Kit из предыдущего пункта по варианту __Release__.
* Перейти в директорию __build__, проверить пути в файле __build-win.bat__, затем очистить директорию __releases/windows__ и запустить bat-файл.
* Убедиться, что в директории __releases/windows__ появился zip-архив с полным содержимым.

#### 6. Обновление языковых файлов
~~~
cd репозиторий-приложения\src\
\путь-к-программе\cmake.exe --build ../build-debug --target update_translations
~~~
* Путь после __--build__ должен совпадать с установленным в п. 2.
* Команду надо выполнять на той же платформе, где происходил препроцессинг CMakeLists.txt. 

Далее открываем обновленные файлы __*.ts__ в Qt Linguist и редактируем переводы.

### MSVC
Актуально для версии Qt 6.7.2.

* Скачать [Build Tools for Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)
    * Выбрать вариант "Разработка классических приложений"
* Открыть консоль с соответствующим окружением:
    * Вариант 1: Найти указанный bat-файл и запустить его:
~~~  
    > "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" amd64
~~~
* 
    * Вариант 2: Запустить через поиск Windows:
~~~
x64 Native Tools Command Prompt for VS 2022
~~~

* Отредактировать __.build/vars-msvc.cmd__ на действительные пути.
* Открыть командную строку и скомпилировать Qt: 
~~~
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k vars-msvc.cmd
cd C:\Temp
mkdir qt6-build-vc
cd qt6-build-vc

Для dev-сборки:
configure.bat -debug -nomake examples -skip qtspeech -prefix c:\DEV\Qt-vc-dev\%_QT_VERSION%

Для release-сбоки:
configure.bat -static -release -nomake examples -skip qtspeech -prefix c:\DEV\Qt-vc-static\%_QT_VERSION%

cmake --build . --parallel
cmake --install .
~~~

Далее аналогично mingw &ndash; установить соответствующие окружения для dev- и release-сборок.

# Linux

https://web.stanford.edu/dept/cs_edu/resources/qt/install-linux

~~~
sudo apt update && sudo apt upgrade -y
sudo apt -y install build-essential openssl libssl-dev libssl1.0 libgl1-mesa-dev libqt5x11extras5 '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-dev
~~~

https://www.qt.io/download-qt-installer

~~~
cd ~/Downloads
chmod +x qt*.run
./qt*.run

sudo apt install libsdl2-dev
~~~

#### 3. Скомпилировать Qt из исходников для статической сборки приложения

https://doc.qt.io/qt-6/linux-building.html

Установить необходимые программы (при необходимости):
~~~
sudo apt install cmake ninja--build python3
~~~

Скачать архив с исходными кодами (обязательно tar, в zip неверные окончания строк) и скомпилировать release-версию:

~~~
cd /tmp
mkdir qt-src
cd qt-src
wget https://download.qt.io/official_releases/qt/6.8/6.8.0/single/qt-everywhere-src-6.8.0.tar.xz
tar xf qt-everywhere-src-6.8.0.tar.xz
mkdir qt-build
cd qt-build
../qt-everywhere-src-6.8.0/configure -static -release
cmake --build . --parallel
cmake --install .
~~~

---
## macOS

https://doc.qt.io/qt-6/macos.html

Далее описывается установка окружения из offline-инсталляторов, так как сетевая установка под виртуальными машинами работала нестабильно.

### 1. Установить xcode

Дистрибутив взять здесь: https://xcodereleases.com, нужен аккаунт на Apple Developer.

* Скопировать файл `.xip` в папку `/Applications` и там распаковать. Файл `.xip` удалить
* Выполнить команду `sudo xcode-select --switch /Applications/Xcode.app`

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

С https://download.qt.io/official_releases/qtcreator/latest/ скачать Qt Creator Offline Installer и с https://download.qt.io/official_releases/qt/ Qt Sources (qt-everywhere-src-X.X.X.tar.xz)
* Установить Qt Creator обычным образом (открыть файл `.dmg`, перетащить иконку в `/Applications`).
* qt-everywhere-src-X.X.X.tar.xz поместить в ~/Downloads

### 5. Собрать статическую версию Qt

https://doc.qt.io/qt-6/macos-building.html

```
cd /tmp
tar xf ~/Downloads/qt-everywhere-src-6.8.1.tar.xz
mkdir -p ~/dev/qt-build
cd ~/dev/qt-build
/tmp/qt-everywhere-src-6.8.1/configure -static -static-runtime -release -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" -make libs -prefix /usr/local/Qt-6.8.1-static
cmake --build . --parallel
sudo cmake --install .
```

После перезагрузки системы `/tmp` очищается, поэтому для повторного запуска надо распаковывать исходники заново.

### 6. Добавить Kit в Qt Creator 

Из папки `/usr/local/Qt-X.X.X-static` (включить отображение скрытых папок при необходимости).

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
