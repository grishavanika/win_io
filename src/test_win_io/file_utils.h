#pragma once

#include <string>
#include <system_error>

#include <Windows.h>

namespace utils
{
    inline std::wstring GetTemporaryDir()
    {
        WCHAR path[MAX_PATH + 1]{};
        const DWORD status = ::GetTempPathW(MAX_PATH + 1, path);
        if (status == 0)
        {
            throw std::system_error(static_cast<int>(::GetLastError())
                , std::system_category());
        }
        return path;
    }

    inline std::wstring CreateTemporaryFile(std::wstring dir = GetTemporaryDir())
    {
        WCHAR name[MAX_PATH + 1]{};
        const UINT status = ::GetTempFileNameW(dir.c_str(), L"", 0, name);
        if (status == 0)
        {
            throw std::system_error(static_cast<int>(::GetLastError())
                , std::system_category());
        }
        return name;
    }

    inline std::wstring CreateTemporaryDir()
    {
        const std::wstring dir = CreateTemporaryFile();
        if (!::DeleteFileW(dir.c_str()))
        {
            throw std::system_error(static_cast<int>(::GetLastError())
                , std::system_category());
        }
        if (!::CreateDirectoryW(dir.c_str(), nullptr))
        {
            throw std::system_error(static_cast<int>(::GetLastError())
                , std::system_category());
        }
        return dir;
    }
} // namespace utils

