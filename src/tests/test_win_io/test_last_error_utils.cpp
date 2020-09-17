#include <gtest/gtest.h>
#include <win_io/detail/last_error_utils.h>

#include <Windows.h>

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
