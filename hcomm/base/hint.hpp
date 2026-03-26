// SPDX-License-Identifier: MulanPSL-2.0

#ifndef HCOMM_BASE_HINT_HPP_
#define HCOMM_BASE_HINT_HPP_

namespace hcomm {
/// Mimic rust's hint::spin_loop
/// https://doc.rust-lang.org/std/hint/fn.spin_loop.html
inline void spin_loop_hint() {
#ifdef __x86_64__
    __asm__ __volatile__("pause");
#elifdef __aarch64__
    __asm__ __volatile__("isb" ::: "memory");
#endif
}
} // namespace hcomm

#endif // HCOMM_BASE_HINT_HPP_
