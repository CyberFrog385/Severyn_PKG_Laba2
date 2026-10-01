# Интеграция FLTK 1.4.5 в Laba2

## Текущее состояние проекта

- `CMakeLists.txt` собирает один минимальный исполняемый файл `Laba2` из `main.cpp`.
- `main.cpp` пока не использует FLTK и сразу завершается.
- `third_party/fltk-1.4.5/` содержит полные исходники FLTK и его CMake-проект.
- Фактическая версия библиотеки — **FLTK 1.4.5**: это указано в `third_party/fltk-1.4.5/CMakeLists.txt` и `third_party/fltk-1.4.5/CHANGES.txt`. Некоторые README внутри библиотеки всё ещё называют 1.4.4.
- Корневой CMake-проект пока не подключает FLTK, а каталог `cmake-build-debug/` создан до его подключения.

Core-библиотека FLTK 1.4.5 уже проверена на текущей машине: конфигурация и target `fltk` успешно собираются через CMake 4.3.4, Ninja и Apple Clang 17 для arm64.

## Карта каталогов

- `FL/` — публичные заголовки API, которые подключаются как `<FL/Fl.H>`.
- `src/` — реализация основной библиотеки и её CMake target `fltk`.
- `CMake/` — внутренняя CMake-логика FLTK: опции, поиск зависимостей и link/export правила.
- `fluid/` — визуальный редактор FLUID и его генератор кода.
- `test/` и `examples/` — demo/test-примеры FLTK; для `Laba2` они не нужны.
- `jpeg/`, `png/`, `zlib/` — bundled-библиотеки для функций работы с изображениями.
- `GL/` — публичные OpenGL-заголовки; отдельный target `fltk::gl` создаётся при `FLTK_BUILD_GL=ON`.
- `documentation/` — документация FLTK; сборку документации для приложения следует отключить.
- `help/` — guides и заметки этого проекта для пользователя и будущих сессий.

Прикладной код `Laba2` должен оставаться в корне проекта или в отдельном новом каталоге, а не внутри `third_party/`.

## Рекомендуемый способ интеграции

Для vendored-исходников используй `add_subdirectory()`. Пока FLTK не установлен в систему, `find_package(FLTK)` и отдельная сборка библиотеки не нужны.

Все опции FLTK нужно задать **до** `add_subdirectory()`. Базовый безопасный вариант:

```cmake
cmake_minimum_required(VERSION 4.1)
project(Laba2)

set(FLTK_BUILD_TEST OFF CACHE BOOL "Build FLTK test programs" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "Build FLTK examples" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "Build FLUID" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "Build fltk-options" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "Build XForms compatibility" FORCE)
set(FLTK_BUILD_GL OFF CACHE BOOL "Build OpenGL support" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared FLTK libraries" FORCE)
set(FLTK_BUILD_HTML_DOCS OFF CACHE BOOL "Build HTML documentation" FORCE)
set(FLTK_BUILD_PDF_DOCS OFF CACHE BOOL "Build PDF documentation" FORCE)
set(FLTK_BUILD_FLUID_DOCS OFF CACHE BOOL "Build FLUID documentation" FORCE)

if(APPLE)
    set(FLTK_BACKEND_X11 OFF CACHE BOOL "Use X11 backend" FORCE)
endif()

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fltk-1.4.5"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/fltk-1.4.5"
    EXCLUDE_FROM_ALL
)

add_executable(Laba2 WIN32 MACOSX_BUNDLE main.cpp)

target_compile_features(Laba2 PRIVATE cxx_std_20)
target_link_libraries(Laba2 PRIVATE fltk::fltk)
```

`fltk::fltk` — рекомендуемый namespaced alias, который сам передаёт include directories, Cocoa frameworks и остальные зависимости. Вручную добавлять `-I`, `-lCocoa` или `-lfltk` не нужно.

`WIN32 MACOSX_BUNDLE` нужен для нормального macOS application bundle. На Windows он включает GUI-режим, на других платформах эти свойства игнорируются.

## Минимальное окно FLTK

Для первой проверки `main.cpp` можно временно заменить таким кодом:

```cpp
#include <FL/Fl.H>
#include <FL/Fl_Window.H>

int main() {
    Fl_Window window(420, 240, "Laba2");
    window.end();
    window.show();
    return Fl::run();
}
```

После запуска должно появиться пустое окно. `Fl::run()` блокирует программу до закрытия окна.

## Сборка и запуск

Старый `cmake-build-debug/` может сохранить CMake cache без FLTK. Для первой проверки используй новый каталог:

```bash
cmake -S . -B build-fltk -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fltk --target Laba2
open build-fltk/Laba2.app
```

Запуск бинарника напрямую:

```bash
./build-fltk/Laba2.app/Contents/MacOS/Laba2
```

Если сборка не запускается или нужны подробности компилятора:

```bash
cmake --build build-fltk --target Laba2 --verbose
cmake --build build-fltk --target help
```

После успешной проверки можно использовать обычный каталог `build/` или перенастроить CLion на `build-fltk/`.

## Какие targets подключать

| Задача | Target | Нужная опция |
|---|---|---|
| Базовые окна и виджеты | `fltk::fltk` | Уже включена |
| PNG/JPEG и расширенные image helpers | `fltk::images` | Дополнительного target нет |
| OpenGL | `fltk::gl` | `FLTK_BUILD_GL=ON` |
| XForms compatibility | `fltk::forms` | `FLTK_BUILD_FORMS=ON` |
| Shared FLTK | `fltk::fltk-shared` | `FLTK_BUILD_SHARED_LIBS=ON` |

Например, для изображений:

```cmake
target_link_libraries(Laba2 PRIVATE
    fltk::fltk
    fltk::images
)
```

`fltk::gl`, `fltk::forms` и их зависимости лучше не добавлять заранее. Включай их только при реальной необходимости, иначе сборка станет больше и сложнее.

## Важные особенности

- На macOS FLTK по умолчанию использует native Cocoa backend. XQuartz и `FLTK_BACKEND_X11` не нужны.
- На macOS JPEG, PNG и zlib по умолчанию берутся из bundled-исходников FLTK; системные библиотеки устанавливать не нужно.
- `FLTK_BUILD_SHARED_LIBS=ON` добавляет shared-библиотеки, но не заменяет статические. Для обычного лабораторного приложения достаточно `fltk::fltk`.
- `FLTK_BUILD_FLUID=OFF` отключает визуальный редактор FLUID, а не сами FLTK-виджеты.
- Сообщение `Warning: fluid not found on the build system!` безвредно, если FLUID не используется.
- `FLTK_BUILD_TEST` включает исполняемые demo/test-программы FLTK, а не CTest-тесты самого `Laba2`.
- Не редактируй `third_party/fltk-1.4.5/` без необходимости: прикладной код и точки подключения должны оставаться в корне проекта.
- `EXCLUDE_FROM_ALL` не отменяет install rules самого FLTK; пока `cmake --install` не используется, это не влияет на обычную сборку.

## Рекомендуемые следующие шаги

1. Заменить корневой `CMakeLists.txt` на приведённую конфигурацию.
2. Добавить минимальное окно в `main.cpp` и проверить запуск.
3. После успешного smoke test перейти от одного файла к небольшим исходникам приложения, перечисляя их в `add_executable()`.
4. Подключать `fltk::images`, `fltk::gl` и другие targets только по требованиям интерфейса.
5. Не добавлять тесты ради формальности: сначала нужен работающий GUI, затем можно выделить логику и добавить CTest.

Непосредственная установка FLTK через `sudo cmake --install` сейчас не рекомендуется: она усложнит сборку и создаст глобальные зависимости, хотя vendored-вариант уже содержит все необходимые исходники.
