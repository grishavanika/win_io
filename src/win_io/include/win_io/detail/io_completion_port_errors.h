#pragma once
#include <win_io/errors.h>
#include <win_io/detail/io_completion_port_data.h>

#include <optional>

namespace wi
{
	namespace detail
	{

		class IoCompletionPortError : public Error
		{
		public:
			using Error::Error;
		};

		class IoCompletionPortQueryError : public IoCompletionPortError
		{
		public:
			IoCompletionPortQueryError(std::error_code ec,
				std::optional<PortData> opt_data);
			IoCompletionPortQueryError(std::error_code ec
				, const char* message, std::optional<PortData> opt_data);

		public:
			std::optional<PortData> data;
		};

	} // namespace detail
} // namespace wi

namespace wi
{
	namespace detail
	{
		inline IoCompletionPortQueryError::IoCompletionPortQueryError(std::error_code ec,
			std::optional<PortData> opt_data)
			: IoCompletionPortError(ec)
			, data(std::move(opt_data))
		{
		}

		inline IoCompletionPortQueryError::IoCompletionPortQueryError(std::error_code ec
			, const char* message, std::optional<PortData> opt_data)
			: IoCompletionPortError(ec, message)
			, data(std::move(opt_data))
		{
		}

	} // namespace detail
} // namespace wi
