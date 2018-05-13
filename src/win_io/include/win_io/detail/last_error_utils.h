#pragma once
#include <system_error>
#include <type_traits>

#include <cstdint>

namespace wi
{
	namespace detail
	{
		std::uint32_t GetLastWinError();

		// Using std::uint32_t to be compatible with DWORD without leak of Windows.h.
		inline std::error_code make_last_error_code(std::uint32_t last_error = GetLastWinError())
		{
			// Using `system_category` with implicit assumption that
			// MSVC's implementation will add proper error code message for free
			// if using together with `std::system_error`
			return std::error_code(static_cast<int>(last_error), std::system_category());
		}

		template<typename E>
		using ModelsSystemError = std::is_base_of<std::system_error, E>;

		template<typename E
			, typename = std::enable_if_t<ModelsSystemError<E>::value>>
		[[noreturn]] void throw_last_error(std::uint32_t last_error = GetLastWinError())
		{
			throw E(make_last_error_code(last_error));
		}

		template<typename E
			, typename = std::enable_if_t<ModelsSystemError<E>::value>>
		[[noreturn]] void throw_last_error(const char* message
			, std::uint32_t last_error = GetLastWinError())
		{
			throw E(make_last_error_code(last_error), message);
		}

	} //namespace detail
} // namespace wi

