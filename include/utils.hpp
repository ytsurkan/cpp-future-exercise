#ifndef UTILS_HPP
#define UTILS_HPP

#include <stdexcept>

#include "future_errorcodes.hpp"

namespace tclib
{

inline const char* toString(FutureErrorCode code) noexcept
{
    switch (code)
    {
    case FutureErrorCode::broken_promise:
        return "broken_promise";
    case FutureErrorCode::future_already_retrieved:
        return "future_already_retrieved";
    case FutureErrorCode::promise_already_satisfied:
        return "promise_already_satisfied";
    case FutureErrorCode::no_state:
        return "no_state";
    }
}

}



#endif // UTILS_HPP
