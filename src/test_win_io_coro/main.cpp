#include <gmock/gmock.h>

// https://github.com/google/googletest/issues/2157
int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
