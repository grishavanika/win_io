
# How to build.

```
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=C:/libs/vcpkg/scripts/buildsystems/vcpkg.cmake -A x64 ..
cmake --build . --config Debug
```
