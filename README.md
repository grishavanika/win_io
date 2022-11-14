# Dependencies (for 'examples' folder and tests).

 - RxCpp (<https://github.com/Reactive-Extensions/RxCpp>): `vcpkg install rxcpp:x64-windows`
 - libunifex (<https://github.com/facebookexperimental/libunifex>): `git submodule update --init --recursive`
 - gtest (<https://github.com/google/googletest>): `vcpkg install gtest:x64-windows`

# How to build.

```
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=C:/libs/vcpkg/scripts/buildsystems/vcpkg.cmake -A x64 ..
cmake --build . --config Debug
```
