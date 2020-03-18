#pragma once

#include <cstdint>

#include "utils.hpp"

namespace nq::err {

constexpr static std::uint32_t module  = 0x99;

constexpr static inline nq::Result FailedUsbXfer       = Result(module, 0);
constexpr static inline nq::Result FailedUsbReceive    = Result(module, 1);
constexpr static inline nq::Result FailedUsbSend       = Result(module, 2);

constexpr static inline nq::Result KernelTimedOut      = Result(1, 117);
constexpr static inline nq::Result FsPathAlreadyExists = Result(2, 2);

} // namespace nq::err
