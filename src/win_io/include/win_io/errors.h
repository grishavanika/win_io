#pragma once
#include <system_error>

namespace wi
{
	
	class Error	: public std::system_error
	{
	public:
		using std::system_error::system_error;
	};
	
} // namespace wi
