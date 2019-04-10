#ifndef FUTURE_ERRORCODES_HPP
#define FUTURE_ERRORCODES_HPP

namespace tclib
{

enum class FutureErrorCode
{

    broken_promise             = 0,
    future_already_retrieved   = 1,
    promise_already_satisfied  = 2,
    no_state                   = 3
};

}

#endif // FUTURE_ERRORCODES_HPP
