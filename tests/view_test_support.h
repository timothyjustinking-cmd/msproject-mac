// tests/view_test_support.h -- assertion harness for the view/io tests.
//
// No third-party framework, matching tests/field_table_tests.cpp and
// tests/sched_test_support.h.  One binary prints "PASS: N checks" or the
// failures and exits non-zero.
#pragma once

#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace view_test {

namespace detail {

template <typename T>
std::string show(const T& value) {
    if constexpr (std::is_integral_v<T> && sizeof(T) < sizeof(int)) {
        return std::to_string(static_cast<int>(value));
    } else if constexpr (std::is_integral_v<T>) {
        return std::to_string(value);
    } else if constexpr (requires(const T& value, std::ostream& stream) {
                             stream << value;
                         }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<value>";
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
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (actual=" << detail::show(actual)
                  << ", expected=" << detail::show(expected) << ")\n";
    }

    void equal_string(const std::string& actual, const std::string& expected,
                      const std::string& what) {
        if (actual == expected) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << "\n  actual:   \"" << actual
                  << "\"\n  expected: \"" << expected << "\"\n";
    }

    void contains(const std::string& haystack, const std::string& needle,
                  const std::string& what) {
        if (haystack.find(needle) != std::string::npos) {
            ++checks_;
            return;
        }
        ++failures_;
        std::cerr << "FAIL: " << what << " (missing \"" << needle << "\" in \"" << haystack
                  << "\")\n";
    }

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

inline std::string read_text_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

inline bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(stream);
}

}  // namespace view_test
