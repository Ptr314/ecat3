# eCat v3

eCat &ndash; универсальный эмулятор 8-разрядных компьютеров. 

# Tools

* Online assembler: https://www.asm80.com/onepage/asm8080.html
* Online diassembler: https://86rk.ru/disassm/

# Compilation

~~~
cmake.exe --build ../build --target update_translations
~~~

## Windows

* MSVC
    * Download installer for "Build Tools for Visual Studio XXXX" here: https://visualstudio.microsoft.com/downloads/

* SDL
    * Download SDL library from there: https://www.libsdl.org/ (development version)
    * Extract it somethere
    * Add this path to CMAKE_PREFIX_PATH environment variable
