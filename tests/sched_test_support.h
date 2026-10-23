// tests/sched_test_support.h -- the minimal assertion harness shared by the
// core/sched tests.  No third-party framework, matching tests/field_table_tests.cpp.
#pragma once

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

#include "date.h"

namespace sched_test {

namespace detail {

template <typename T>
std::string show(const T& value) {
    if constexpr (std::is_integral_v<T> && sizeof(T) < sizeof(int)) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(value);
    } else {
        std::ostringstream out;
        out << value;
        return out.str();
    }
}

}  // namespace detail

class TestContext {
  public:
    void check(bool condition, const std::string& what) {
        if (condition) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << '\n';
    }

    template <typename A, typename B>
    void equal(const A& actual, const B& expected, const std::string& what) {
        bool same = false;
        if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
            using common = std::common_type_t<A, B>;
            same = static_cast<common>(actual) == static_cast<common>(expected);
        } else {
            same = actual == expected;
        }
        if (same) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << detail::show(actual)
                  << ", expected=" << detail::show(expected) << ")\n";
    }

    // date32 prints as a packed number by default; render it as a civil time.
    void equal_date(core::sched::date32 actual, core::sched::date32 expected,
                    const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << core::sched::to_string(actual)
                  << ", expected=" << core::sched::to_string(expected) << ")\n";
    }

    void equal_string(const std::string& actual, const std::string& expected,
                      const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=\"" << actual << "\", expected=\""
                  << expected << "\")\n";
    }

    // Wave 23 (A-7a): a caller that writes committed data must be able to ask
    // whether anything has already failed, so it never rewrites a golden after
    // a failed assertion.
    bool failed() const noexcept { return failures_ != 0; }

    int finish(const std::string& name) const {
        if (failures_ == 0) {
            std::cout << name << ": " << checks_ << " checks passed\n";
            return 0;
        }
        std::cerr << name << ": " << failures_ << " of " << (checks_ + failures_)
                  << " checks FAILED\n";
        return 1;
    }

  private:
    int checks_ = 0;
    int failures_ = 0;
};

}  // namespace sched_test
