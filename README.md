# Samples for Windows I/O Completion Ports

[![Build Status (Windows)](https://ci.appveyor.com/api/projects/status/l8ii5sgufhfu8ojx?svg=true
)](https://ci.appveyor.com/project/grishavanika/win-io)

# Use-cases

- Directory changes ([::ReadDirectoryChangesW()] wrapper)

[::ReadDirectoryChangesW()]: https://msdn.microsoft.com/en-us/library/windows/desktop/aa365465(v=vs.85).aspx

# Tested/supported compilers

Library C++17 `string_view` and `optional` is in use.

Tested on Windows with GCC 7.3.0, MSVC 2017, Clang 7.0.0
