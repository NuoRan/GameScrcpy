#pragma once

#include <tl/expected.hpp>

#include <type_traits>
#include <utility>

namespace gs {

template <typename T, typename E>
using Expected = tl::expected<T, E>;

template <typename E>
using Unexpected = tl::unexpected<E>;

template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> makeUnexpected(E&& error)
{
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

}
