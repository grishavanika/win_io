#include <gtest/gtest.h>
#include <win_io/detail/last_error_utils.h>

#include <Windows.h>

namespace
{
	struct TestError : std::system_error
	{
		using std::system_error::system_error;
	};
} // namespace

TEST(LastError, GetLastWinError_Returns_Proper_Last_Error)
{
	::SetLastError(5);
	ASSERT_EQ(::GetLastError(), wi::detail::GetLastWinError());
}

TEST(LastError,
	Make_Last_Error_Code_Creates_Proper_Error_Code_Instance_With_System_Category)
{
	const std::error_code ec = wi::detail::make_last_error_code(5);
	ASSERT_EQ(5, ec.value());
	ASSERT_EQ(std::system_category(), ec.category());
}

TEST(LastError, Throw_Last_Error_Throws_Given_Exception)
{
	try
	{
		wi::detail::throw_last_error<TestError>(5);
		// #TODO: turn off "unreachable code" warning
		// FAIL() << "throw_last_error() does not throw";
	}
	catch (const TestError& e)
	{
		ASSERT_EQ(5, e.code().value());
		ASSERT_EQ(std::system_category(), e.code().category());
	}
}

TEST(LastError, Throw_Last_Error_Without_Args_Throws_Latest_Win_Last_Error)
{
	try
	{
		::SetLastError(2);
		wi::detail::throw_last_error<TestError>();
	}
	catch (const TestError& e)
	{
		ASSERT_EQ(2u, ::GetLastError());
		ASSERT_EQ(2, e.code().value());
		ASSERT_EQ(std::system_category(), e.code().category());
	}
}
