call cmake -S . -B build ^
	-DCMAKE_TOOLCHAIN_FILE=C:/_dev/vcpkg/scripts/buildsystems/vcpkg.cmake

:: call cmake --build build --config Debug
