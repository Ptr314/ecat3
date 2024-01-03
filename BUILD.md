# Настройка окружения для компиляции приложения

## Windows

### MINGW

#### 1. Установить программы
* https://www.qt.io/download-qt-installer (прямая ссылка https://download.qt.io/) и установить следующие компоненты:
    * Qt Creator
    * Mingw 11.2
    * cmake
    * ninja
    * Qt sources
* Python 3
* SDL
    * Скачать https://www.libsdl.org/ (development version)
    * Распаковать
    * Добавить путь к переменной окружения CMAKE_PREFIX_PATH

#### 2. Скомпилировать Qt из исходников для статической сборки приложения

https://doc.qt.io/qt-6/windows-building.html

* Отредактировать .build/mingw-vars.cmd на действительные пути.
* Открыть командную строку и скомпилировать Qt: 

~~~
cd репозиторий-приложения\.build
%SystemRoot%\system32\cmd.exe /E:ON /V:ON /k mingw-vars.cmd
cd C:\Temp
mkdir qt6-build
cd qt6-build
configure.bat -static
cmake --build . --parallel
cmake --install .
~~~

#### 3. Настроить Qt Creator для использования статической сборки
* __Edit/Preferences/Kits/Qt versions/Add__ &ndash; выбрать qmake.exe из статической сборки, Name: Qt %{Qt:Version} Static;
* __Edit/Preferences/Kits/Kits/Add__ &ndash; Qt version из предыдущего пункта.

#### 4. Собрать release-версию приложения
* Скомпилировать приложение с помощью Kit из предыдущего пункта по варианту Release.
* Поместить в одну директорию exe и файлы данных.
* Перенести туда же файлы:
    * SDL2.dll (из SDL2)
    * libgcc_s_seh-1.dll (из Qt-mingw)
    * libstdc++-6.dll
    * libwinpthread-1.dll

### MSVC
* Скачать "Build Tools for Visual Studio XXXX" https://visualstudio.microsoft.com/downloads/

Обновление языковых файлов
~~~
/путь-к-программе/cmake.exe --build ../build --target update_translations
~~~

# Linux

https://web.stanford.edu/dept/cs_edu/resources/qt/install-linux

~~~
sudo apt-get update && sudo apt-get upgrade
sudo apt-get -y install build-essential openssl libssl-dev libssl1.0 libgl1-mesa-dev libqt5x11extras5 '^libxcb.*-dev' libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-dev
~~~

https://www.qt.io/download-qt-installer

~~~
cd ~/Downloads
chmod +x qt*.run
./qt*.run

sudo apt-get install libsdl2-dev
~~~


# Полезные ссылки

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online diassembler: https://86rk.ru/disassm/