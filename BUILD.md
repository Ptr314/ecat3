# Настройка окружения и компиляция приложения

## Windows

### MINGW

#### 1. Установить программы
* https://download.qt.io/, скачать online-инсталятор (возможно, из России понадобится зарубежный VPN) и установить следующие компоненты:
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

#### 2. Настроить debug-версию в Qt Creator
* Если нужна полная очистка, удалить файлы __CMakeLists.txt.user*__.
* Перейти в настройку для сборки (__Projects/Build & Run/Debug kit/Build__):
    * Выбрать вариант __"Build configuration"__ &ndash; __"Debug"__. 
    * Установить целевую директорию для компиляции __"репозиторий-приложения\build-debug"__ (__Build directory__).
* Перейти в настройку для запуска (__Projects/Build & Run/Debug kit/Run__):
    * Установить __Working directory__ как __"репозиторий-приложения\deploy"__.

#### 3. Скомпилировать Qt из исходников для статической сборки приложения

https://doc.qt.io/qt-6/windows-building.html

* Отредактировать __.build/mingw-vars.cmd__ на действительные пути.
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

* Запомнить, куда библиотека установилась (либо разобраться, как это изменить).

#### 4. Настроить Qt Creator для использования статической сборки
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

### MSVC
<не завершено>
* Скачать "Build Tools for Visual Studio XXXX" https://visualstudio.microsoft.com/downloads/


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

sudo apt-get install libsdl2-dev
~~~


# Полезные ссылки

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online diassembler: https://86rk.ru/disassm/