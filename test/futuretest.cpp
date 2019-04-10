#include "catch2/catch.hpp"

#include <future>
#include "future.hpp"

TEST_CASE("FutureTest, testSharedState")
{
    tclib::SharedState<std::int32_t> sharedState;

    std::promise<void> go, pushReady, popReady;
    std::shared_future<void> ready(go.get_future());

    std::future<void> pushDone;
    std::future<std::int32_t> popDone;

    pushDone = std::async(std::launch::async, [&sharedState, &pushReady, ready]()
    {
        pushReady.set_value();
        ready.wait();

        sharedState.setValue(42);
    });

    popDone = std::async(std::launch::async, [&sharedState, &popReady, ready]()
    {
        popReady.set_value();
        ready.wait();

        return sharedState.getValue();
    });

    pushReady.get_future().wait();
    popReady.get_future().wait();
    go.set_value();

    pushDone.wait();
    REQUIRE(42 == popDone.get());
}

TEST_CASE("FutureTest, testPromiseSetAndFutureGet")
{
    tclib::Promise<std::int32_t> promise;

    tclib::Future<std::int32_t> future = promise.getFuture();

    std::promise<void> go, pushReady, popReady;
    std::shared_future<void> ready(go.get_future());

    std::future<void> pushDone;
    std::future<std::int32_t> popDone;

    pushDone = std::async(std::launch::async, [&promise, &pushReady, ready]()
    {
        pushReady.set_value();
        ready.wait();

        promise.setValue(42);
    });

    popDone = std::async(std::launch::async, [&future, &popReady, ready]()
    {
        popReady.set_value();
        ready.wait();

        return future.get();
    });

    pushReady.get_future().wait();
    popReady.get_future().wait();
    go.set_value();

    pushDone.wait();
    REQUIRE(42 == popDone.get());
}

TEST_CASE("FutureTest, testPromiseSetAndFutureWait")
{
    tclib::Promise<std::int32_t> promise;

    tclib::Future<std::int32_t> future = promise.getFuture();

    std::promise<void> go, pushReady, popReady;
    std::shared_future<void> ready(go.get_future());

    std::future<void> pushDone;
    std::future<void> popDone;

    pushDone = std::async(std::launch::async, [&promise, &pushReady, ready]()
    {
        pushReady.set_value();
        ready.wait();

        promise.setValue(42);
    });

    popDone = std::async(std::launch::async, [&future, &popReady, ready]()
    {
        popReady.set_value();
        ready.wait();

        future.wait();
    });

    pushReady.get_future().wait();
    popReady.get_future().wait();
    go.set_value();

    pushDone.wait();
    popDone.wait();

    REQUIRE(42 == future.get());
}

TEST_CASE("FutureTest, testPromiseSetThrowException")
{
    tclib::Promise<std::int32_t> promise;
    promise.setValue(42);

    REQUIRE_THROWS_AS(promise.setValue(42), tclib::FutureError);
}

TEST_CASE("FutureTest, testPromiseGetFutureThrowException")
{
    tclib::Promise<std::int32_t> promise;
    promise.getFuture();

    REQUIRE_THROWS_AS(promise.getFuture(), tclib::FutureError);
}

TEST_CASE("FutureTest, testFutureGetThrowException")
{
    tclib::Future<std::int32_t> future;

    REQUIRE_THROWS_AS(future.get(), tclib::FutureError);
}

TEST_CASE("FutureTest, testPromiseSetExceptionAndFutureGet")
{
    tclib::Promise<std::int32_t> promise;

    tclib::Future<std::int32_t> future = promise.getFuture();

    std::promise<void> go, pushReady;
    std::future<void> ready(go.get_future());

    std::future<void> pushDone;

    pushDone = std::async(std::launch::async, [&promise, &pushReady, &ready]()
    {
        pushReady.set_value();
        ready.wait();

        try
        {
            throw std::logic_error("Task failed!");
        }
        catch (...)
        {
            promise.setException(std::current_exception());
        }
    });

    pushReady.get_future().wait();
    go.set_value();

    pushDone.wait();

    REQUIRE_THROWS_AS(future.get(), std::logic_error);
}

TEST_CASE("FutureTest, testPromiseSetThenFutureGet")
{
    tclib::Promise<std::int32_t> promise;
    promise.setValue(42);

    tclib::Future<std::int32_t> future = promise.getFuture();

    REQUIRE(42 == future.get());
}

TEST_CASE("FutureTest, testFutureShare")
{
    tclib::Promise<std::int32_t> promise;
    tclib::Future<std::int32_t> future = promise.getFuture();
    tclib::SharedFuture<std::int32_t> sharedFuture = future.share();

    REQUIRE_FALSE(future.valid());

    REQUIRE(sharedFuture.valid());

    promise.setValue(42);

    REQUIRE(42 == sharedFuture.get());

    tclib::SharedFuture<std::int32_t> sharedFuture2 = sharedFuture;
    REQUIRE(42 == sharedFuture2.get());
}

TEST_CASE("FutureTest, testSharedFutureCreationFromFuture")
{
    tclib::Promise<std::int32_t> promise;
    tclib::SharedFuture<std::int32_t> sharedFuture(promise.getFuture());

    REQUIRE(sharedFuture.valid());
}

TEST_CASE("FutureTest, testPromiseSetAndSharedFutureGet")
{
    tclib::Promise<std::int32_t> go;
    tclib::SharedFuture<std::int32_t> ready(go.getFuture());

    std::future<std::int32_t> pushDone;
    std::future<std::int32_t> popDone;

    pushDone = std::async(std::launch::async, [ready]() mutable
    {
        return ready.get();
    });

    popDone = std::async(std::launch::async, [ready]() mutable
    {
        return ready.get();
    });

    go.setValue(42);

    REQUIRE(42 == pushDone.get());
    REQUIRE(42 == popDone.get());
}

std::int32_t f(tclib::Future<std::int32_t> future)
{
    return future.get()*2;
}

TEST_CASE("FutureTest, testFutureThen")
{
    tclib::Promise<std::int32_t> promise;

    tclib::Future<std::int32_t> future = promise.getFuture();
    auto thenFuture = future.then(f);

    std::promise<void> go, pushReady, popReady;
    std::shared_future<void> ready(go.get_future());

    std::future<void> pushDone;
    std::future<std::int32_t> popDone;

    pushDone = std::async(std::launch::async, [&promise, &pushReady, ready]()
    {
        pushReady.set_value();
        ready.wait();
        promise.setValue(42);
    });

    popDone = std::async(std::launch::async, [&thenFuture, &popReady, ready]()
    {
        popReady.set_value();
        ready.wait();
        return thenFuture.get();
    });

    pushReady.get_future().wait();
    popReady.get_future().wait();
    go.set_value();

    REQUIRE(42 * 2 == popDone.get());
}

TEST_CASE("FutureTest, testFutureThenThen")
{
    tclib::Promise<std::int32_t> promise;
    tclib::Future<std::int32_t> future = promise.getFuture();
    auto then = future.then(f).then(f);

    promise.setValue(42);

    REQUIRE(42 * 2* 2 == then.get());
}

TEST_CASE("FutureTest, testPromiseAndFutureVoid")
{
    tclib::Promise<void> promise;

    tclib::Future<void> future = promise.getFuture();

    std::promise<void> go, pushReady, popReady;
    std::shared_future<void> ready(go.get_future());

    std::future<void> pushDone;
    std::future<void> popDone;

    pushDone = std::async(std::launch::async, [&promise, &pushReady, ready]()
    {
        pushReady.set_value();
        ready.wait();

        promise.setValue();
    });

    popDone = std::async(std::launch::async, [&future, &popReady, ready]()
    {
        popReady.set_value();
        ready.wait();

        return future.get();
    });

    pushReady.get_future().wait();
    popReady.get_future().wait();
    go.set_value();

    popDone.get();
    REQUIRE_FALSE(future.valid());
}
