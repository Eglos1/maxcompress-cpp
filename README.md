# MaxCompress (C++ / GTK3)

A native C++17 re-implementation of the **MaxCompress** compression engine the
Python [`maxcompress`](https://github.com/Eglos1/maxcompress) provides, with a
GTK3 desktop GUI.

## Build
```sh
sudo apt-get install -y libgtk-3-dev cmake g++      # Debian/Ubuntu
cmake -S . -B build
cmake --build build --config Release
./build/maxcompress
```
macOS: `brew install gtk+3` then the same cmake steps. (GTK3 is awkward on
Windows; Linux/macOS are the supported targets.)

## Layout
- `src/main.cpp` — entry point
- `src/engine.h` — compression engine
- `src/gui.h` — GTK3 GUI
- `src/algorithms/` — codecs & transforms

## License
MIT
