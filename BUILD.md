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

## Windows

* MSVC
    * Скачать "Build Tools for Visual Studio XXXX" https://visualstudio.microsoft.com/downloads/
* SDL
    * Скачать https://www.libsdl.org/ (development version)
    * Распаковать
    * Добавить путь к переменной окружения CMAKE_PREFIX_PATH 
* https://www.qt.io/download-qt-installer

Обновление языковых файлов
~~~
/путь-к-программе/cmake.exe --build ../build --target update_translations
~~~

# Полезные ссылки

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online diassembler: https://86rk.ru/disassm/