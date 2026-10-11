#include "CppUnitTest.h"
#include "info_format.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(info_format_tests)
{
public:
    TEST_METHOD(durations_use_a_unit_that_suits_their_size)
    {
        Assert::AreEqual(std::wstring(L"2.5 ms"), format_duration(0.0025));
        Assert::AreEqual(std::wstring(L"1.00 s"), format_duration(1.0));
        Assert::AreEqual(std::wstring(L"59.99 s"), format_duration(59.99));
        Assert::AreEqual(std::wstring(L"1.00 min"), format_duration(60.0));
        Assert::AreEqual(std::wstring(L"1.50 h"), format_duration(5400.0));
        Assert::AreEqual(std::wstring(L"3.00 d"), format_duration(3.0 * 86400.0));
        Assert::AreEqual(std::wstring(L"2.00 yr"), format_duration(2.0 * 365.25 * 86400.0));
        Assert::AreEqual(std::wstring(L"1.50 kyr"), format_duration(1500.0 * 365.25 * 86400.0));
        Assert::AreEqual(std::wstring(L"5.26 Myr"), format_duration(5.26e6 * 365.25 * 86400.0));
        Assert::AreEqual(std::wstring(L"13.80 Gyr"), format_duration(13.8e9 * 365.25 * 86400.0));
    }

    TEST_METHOD(speeds_show_the_multiple_of_real_time)
    {
        Assert::AreEqual(std::wstring(L"0.01x"), format_speed(0.01));
        Assert::AreEqual(std::wstring(L"0.5x"), format_speed(0.5));
        Assert::AreEqual(std::wstring(L"1x"), format_speed(1.0));
        Assert::AreEqual(std::wstring(L"12.5x"), format_speed(12.5));
        Assert::AreEqual(std::wstring(L"100x"), format_speed(100.0));
        Assert::AreEqual(std::wstring(L"1000x"), format_speed(1000.0));
        Assert::AreEqual(std::wstring(L"604800x"), format_speed(604800.0));
        Assert::AreEqual(std::wstring(L"1e6x"), format_speed(1e6));
        Assert::AreEqual(std::wstring(L"7.9e13x"), format_speed(7.9e13));
        Assert::AreEqual(std::wstring(L"1.5e100x"), format_speed(1.5e100));
        Assert::AreEqual(std::wstring(L"0x"), format_speed(0.0));
    }

    TEST_METHOD(zero_negative_and_invalid_times_show_as_zero)
    {
        Assert::AreEqual(std::wstring(L"0.0 ms"), format_duration(0.0));
        Assert::AreEqual(std::wstring(L"0.0 ms"), format_duration(-5.0));
        Assert::AreEqual(std::wstring(L"0.0 ms"), format_duration(std::nan("")));
    }
};
