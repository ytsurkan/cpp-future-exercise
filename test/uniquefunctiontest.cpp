#include "catch2/catch.hpp"

#include <memory>

#include "uniquefunction.hpp"


TEST_CASE("UniqueFunctionTest, testEmptyFunction")
{
    tclib::UniqueFunction<void(int)> f;
    REQUIRE_FALSE(f);

    f = nullptr;
    REQUIRE_FALSE(f);

    tclib::UniqueFunction<void(int)> fNull(nullptr);
    REQUIRE_FALSE(fNull);

    //Note: calls UniqueFunction::operator=(std::nullptr_t) - why???
    f = {};
    REQUIRE_FALSE(f);

    f = tclib::UniqueFunction<void(int)>{};
    REQUIRE_FALSE(f);

    tclib::UniqueFunction<void(int)> fEmpty({});
    REQUIRE_FALSE(fEmpty);

    REQUIRE_FALSE(nullptr != fEmpty);

    REQUIRE(nullptr == fEmpty);
}

TEST_CASE("UniqueFunctionTest, testFunctionCallOperator")
{
    tclib::UniqueFunction<int(int)> f([](int x){ return x*x; });
    REQUIRE(25 == f(5));

    tclib::UniqueFunction<void(int&)> fRef([](int& x){ x*=x; });
    int arg = 3;
    fRef(arg);
    REQUIRE(3*3 == arg);
}

static int Addition(int x)
{
    return x + x;
}

TEST_CASE("UniqueFunctionTest, testFunctionPointer")
{
    using FreeFunctionType = decltype(Addition);
    tclib::UniqueFunction<FreeFunctionType> f(Addition);

    REQUIRE(f);

    //Note:  it works, because of the implicit conversion from function to function-pointer
    REQUIRE(f(5) == (*****Addition)(5));
}

TEST_CASE("UniqueFunctionTest, testMove")
{
    auto sp = std::make_shared<int>(42);

    auto lambda = [sp](){ return *sp; };

    tclib::UniqueFunction<int()> f1(std::move(lambda));
    tclib::UniqueFunction<int()> f2({});
    REQUIRE(f1);

    REQUIRE(2 == sp.use_count());

    f2 = std::move(f1);

    REQUIRE(f2);
    REQUIRE_FALSE(f1);
    REQUIRE(2 == sp.use_count());
}

TEST_CASE("UniqueFunctionTest, testSwap")
{
    auto sp = std::make_shared<int>(42);

    auto lambda = [sp](){ return *sp; };

    tclib::UniqueFunction<int()> f1(std::move(lambda));
    tclib::UniqueFunction<int()> f2({});

    REQUIRE(2 == sp.use_count());

    swap(f1, f2);
    REQUIRE_FALSE(f1);
    REQUIRE(2 == sp.use_count());

    REQUIRE(f2);
    REQUIRE(42 == f2());
}

TEST_CASE("UniqueFunctionTest, testDestructorCall")
{
    auto sp = std::make_shared<int>(42);
    {
        auto lambda = [sp](){ return *sp; };
        tclib::UniqueFunction<int()> f1(std::move(lambda));

        REQUIRE(2 == sp.use_count());
    }
    REQUIRE(1 == sp.use_count());
}

TEST_CASE("UniqueFunctionTest, testFunctionSize")
{
    auto data1 = std::string();
    auto data2 = std::string();
    auto data3 = std::string();
    auto bigCallable = [data1, data2, data3]() { };
    tclib::UniqueFunction<void()> f(std::move(bigCallable));

    REQUIRE(sizeof(f) <= 8 * sizeof (void*));
    REQUIRE(sizeof(tclib::UniqueFunction<void()>) <= 8 * sizeof (void*));
}
