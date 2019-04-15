#ifndef FUTURE_HPP
#define FUTURE_HPP

#include <mutex>
#include <condition_variable>
#include <optional>

#include <stdexcept>
#include <functional>
#include <type_traits>
#include <atomic>

#include "./utils.hpp"
#include "./uniquefunction.hpp"

namespace tclib
{

class FutureError : public std::logic_error
{
public:
    explicit FutureError(FutureErrorCode code) : std::logic_error{toString(code)} {}
};

template<typename Result>
class SharedState
{
public:
    SharedState() = default;

    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    SharedState(SharedState&&) = default;
    SharedState& operator=(SharedState&&) = default;

    void setValue(Result result)
    {
        checkState();
        m_result = std::move(result);
        setStateDoneAndNotify();
    }

    void setException(std::exception_ptr exc)
    {
        checkState();
        m_exception = std::move(exc);
        setStateDoneAndNotify();
    }

    void setContinuation(UniqueFunction<void()> continuation)
    {
        auto done = false;
        {
            auto _(std::unique_lock<std::mutex>(m_mutex));
            done = m_done;
            if (!done)
            {
                m_then.swap(continuation);
            }
        }
        if (done && continuation)
        {
            continuation();
        }
    }

    void resetContinuation()
    {
        decltype(m_then) continuation;
        std::unique_lock<std::mutex> lock(m_mutex);
        m_then.swap(continuation);
    }

    void setAndThrowIfRetrieved()
    {
        if (m_retrieved.test_and_set())
        {
            throw FutureError{FutureErrorCode::future_already_retrieved};
        }
    }

    auto getValue()
    {
        wait();
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }
        return  m_result.value();
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this](){ return m_done.load();});
    }

private:
    void checkState()
    {
        if (m_done)
        {
            throw FutureError{FutureErrorCode::promise_already_satisfied};
        }
    }

    void setStateDoneAndNotify()
    {
        decltype(m_then) then;
        {
            auto _(std::lock_guard<std::mutex>(m_mutex));
            m_done = true;
            then.swap(m_then);
        }
        m_cv.notify_all();

        if (then)
        {
            then();
        }
    }

private:
    std::atomic<bool> m_done{false};
    std::atomic_flag m_retrieved = ATOMIC_FLAG_INIT;
    std::optional<Result> m_result;
    UniqueFunction<void()> m_then;
    std::exception_ptr m_exception;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
};

/// Explicit specialization for SharedState<void>
template<>
class SharedState<void>
{
public:
    SharedState() = default;

    SharedState(const SharedState&) = delete;
    SharedState& operator=(const SharedState&) = delete;

    SharedState(SharedState&&) = default;
    SharedState& operator=(SharedState&&) = default;

    void setValue()
    {
        checkState();
        setStateDoneAndNotify();
    }

    void setException(std::exception_ptr exc)
    {
        checkState();
        m_exception = std::move(exc);
        setStateDoneAndNotify();
    }

    void setContinuation(UniqueFunction<void()> continuation)
    {
        auto done = false;
        {
            auto _(std::unique_lock<std::mutex>(m_mutex));
            done = m_done;
            if (!done)
            {
                m_then.swap(continuation);
            }
        }
        if (done && continuation)
        {
            continuation();
        }
    }

    void resetContinuation()
    {
        decltype(m_then) continuation;
        auto _(std::unique_lock<std::mutex>(m_mutex));
        m_then.swap(continuation);
    }

    void setAndThrowIfRetrieved()
    {
        if (m_retrieved.test_and_set())
        {
            throw FutureError{FutureErrorCode::future_already_retrieved};
        }
    }

    void getValue()
    {
        wait();
        if (m_exception)
        {
            std::rethrow_exception(m_exception);
        }
    }

    void wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this](){ return m_done.load();});
    }

private:
    void checkState()
    {
        if (m_done)
        {
            throw FutureError{FutureErrorCode::promise_already_satisfied};
        }
    }

    void setStateDoneAndNotify()
    {
        decltype(m_then) then;
        {
            auto _(std::lock_guard<std::mutex>(m_mutex));
            m_done = true;
            then.swap(m_then);
        }
        m_cv.notify_all();

        if (then)
        {
            then();
        }
    }

private:
    std::atomic<bool> m_done{false};
    std::atomic_flag m_retrieved = ATOMIC_FLAG_INIT;
    UniqueFunction<void()> m_then;
    std::exception_ptr m_exception;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
};


template <typename T> class Future;
template <typename T> class SharedFuture;

template <typename T>
class Promise
{
public:
    Promise()
        : m_statePtr{std::make_shared<SharedState<T>>()}
    {}

    ~Promise()
    {
        if (m_statePtr)
        {
            //remove cyclic dependency between shared states,
            //case when {Promise<int> p; auto f = p.then(...); and no call to p.setValue();
            m_statePtr->resetContinuation();
        }
    }

   Promise(const Promise&) = delete;
   Promise& operator=(const Promise&) = delete;

   Promise(Promise&&) = default;
   Promise& operator=(Promise&&) = default;

    void setValue(T value)
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setValue(std::move(value));
    }

    Future<T> getFuture()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setAndThrowIfRetrieved();

        return Future<T>(m_statePtr);
    }

    void setException(std::exception_ptr exc)
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setException(exc);
    }

private:
    std::shared_ptr<SharedState<T>> m_statePtr;
};

template <typename T>
class Future
{
private:
    friend class Promise<T>;

    Future(std::shared_ptr<SharedState<T>> sharedStatePtr)
        : m_statePtr{std::move(sharedStatePtr)}
    {}

public:
    Future() = default;

    ~Future() = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    T get()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        auto statePtr = std::move(m_statePtr);
        return statePtr->getValue();
    }

    void wait()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->wait();
    }

    bool valid() const noexcept
    {
        return (nullptr != m_statePtr);
    }

    SharedFuture<T> share() noexcept;

    /// @brief Creates a continuation on the current thread.
    /// @return a future of type of the result type of passed function object
    /// @details Creates new promise/future pair and the new future is returned.
    /// Creates a continuation that holds a shared pointer to shared state object
    /// and the new promise object. Assign continuation to shared state object.
    /// Reset the shared pointer that is referring to shared state of this future.
    /// Finally the shared state object is referenced by promise and continuation objects.
    /// This structure is similar to a linked list, the shared state object has a continuation
    /// that points to next promise object (next node in the linked list).
    /// Continuation is executed in the tread context of promise object.
    template<typename F>
    auto then(F f)
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        using R = decltype(f(Future<T>()));

        Promise<R> promise;
        Future<R> future = promise.getFuture();
        UniqueFunction<void()> continuation =
        [state = m_statePtr, p = std::move(promise), f = std::move(f)]() mutable
        {
            try
            {
                Future<T> futureContinuation(std::move(state));
                p.setValue(f(std::move(futureContinuation)));
            }
            catch (...)
            {
                p.setException(std::current_exception());
            }
        };
        auto state = std::move(m_statePtr);
        state->setContinuation(std::move(continuation));
        return future;
    }

private:
    std::shared_ptr<SharedState<T>> m_statePtr;
};

template <typename T>
class SharedFuture
{
private:
    friend class Future<T>;

    explicit SharedFuture(std::shared_ptr<SharedState<T>> sharedStatePtr) noexcept
        : m_statePtr{std::move(sharedStatePtr)}
    {}

public:
    SharedFuture() = default;

    explicit SharedFuture(Future<T>&& other) noexcept
        : SharedFuture(other.share())
    { }

    SharedFuture(const SharedFuture&) = default;
    SharedFuture& operator=(const SharedFuture&) = default;

    SharedFuture(SharedFuture&&) = default;
    SharedFuture& operator=(SharedFuture&&) = default;

    T get()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->getValue();
    }

    void wait() const
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->wait();
    }

    bool valid() const noexcept
    {
        return (nullptr != m_statePtr);
    }

private:
    std::shared_ptr<SharedState<T>> m_statePtr;
};

template <> class Future<void>;
template <> class SharedFuture<void>;

/// Explicit specialization for Future<void>
template <>
class Future<void>
{
private:
    friend class Promise<void>;

    Future(std::shared_ptr<SharedState<void>> sharedStatePtr)
        : m_statePtr{std::move(sharedStatePtr)}
    {}

public:
    Future() = default;

    ~Future() = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    void get()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        auto statePtr = std::move(m_statePtr);
        return statePtr->getValue();
    }

    void wait()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->wait();
    }

    bool valid() const noexcept
    {
        return (nullptr != m_statePtr);
    }

    SharedFuture<void> share() noexcept;

    /// @brief Creates a continuation on the current thread.
    /// @return a future of type of the result type of passed function object
    /// @details Creates new promise/future pair and the new future is returned.
    /// Creates a continuation that holds a shared pointer to shared state object
    /// and the new promise object. Assign continuation to shared state object.
    /// Reset the shared pointer that is referring to shared state of this future.
    /// Finally the shared state object is referenced by promise and continuation objects.
    /// This structure is similar to a linked list, the shared state object has a continuation
    /// that points to next promise object (next node in the linked list).
    /// Continuation is executed in the tread context of promise object.
    template<typename F>
    auto then(F f)
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        using R = decltype(f(Future<void>()));

        Promise<R> promise;
        Future<R> future = promise.getFuture();
        UniqueFunction<void()> continuation =
        [state = m_statePtr, p = std::move(promise), f = std::move(f)]() mutable
        {
            try
            {
                Future<void> futureContinuation(std::move(state));
                p.setValue(f(std::move(futureContinuation)));
            }
            catch (...)
            {
                p.setException(std::current_exception());
            }
        };
        auto state = std::move(m_statePtr);
        state->setContinuation(std::move(continuation));
        return future;
    }

private:
    std::shared_ptr<SharedState<void>> m_statePtr;
};

/// Explicit specialization for SharedFuture<void>
template <>
class SharedFuture<void>
{
private:
    friend class Future<void>;

    explicit SharedFuture(std::shared_ptr<SharedState<void>> sharedStatePtr) noexcept
        : m_statePtr{std::move(sharedStatePtr)}
    {}

public:
    SharedFuture() = default;

    explicit SharedFuture(Future<void>&& other) noexcept
        : SharedFuture(other.share())
    { }

    SharedFuture(const SharedFuture&) = default;
    SharedFuture& operator=(const SharedFuture&) = default;

    SharedFuture(SharedFuture&&) = default;
    SharedFuture& operator=(SharedFuture&&) = default;

    void get()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->getValue();
    }

    void wait() const
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        return m_statePtr->wait();
    }

    bool valid() const noexcept
    {
        return (nullptr != m_statePtr);
    }

private:
    std::shared_ptr<SharedState<void>> m_statePtr;
};

/// Explicit specialization for Promise<void>
template <>
class Promise<void>
{
public:
    Promise()
        : m_statePtr{std::make_shared<SharedState<void>>()}
    {}

    ~Promise()
    {
        if (m_statePtr)
        {
            //remove cyclic dependency between shared states,
            //case when {Promise<int> p; auto f = p.then(...); and no call to p.setValue();
            m_statePtr->resetContinuation();
        }
    }

   Promise(const Promise&) = delete;
   Promise& operator=(const Promise&) = delete;

   Promise(Promise&&) = default;
   Promise& operator=(Promise&&) = default;

    void setValue()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setValue();
    }

    Future<void> getFuture()
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setAndThrowIfRetrieved();

        return Future<void>(m_statePtr);
    }

    void setException(std::exception_ptr exc)
    {
        if (!m_statePtr)
        {
            throw FutureError{FutureErrorCode::no_state};
        }
        m_statePtr->setException(exc);
    }

private:
    std::shared_ptr<SharedState<void>> m_statePtr;
};

template<typename T>
inline SharedFuture<T> Future<T>::share() noexcept
{
    return SharedFuture<T>(std::move(m_statePtr));
}

//should be defined at the poin where the definition of SharedFuture is seen
inline
SharedFuture<void> Future<void>::share() noexcept
{
    return SharedFuture<void>(std::move(m_statePtr));
}

}

#endif // FUTURE_HPP
