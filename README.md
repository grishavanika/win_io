
# How to build

vcpkg is used for package management. Just:

```
cmake -S . -B build ^
	-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```

Old and specific version of libunifex is used via FetchContent since breaking
API changes are present since those samples were initially written.
