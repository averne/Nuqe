#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <algorithm>

#include "mtp_codes.hpp"
#include "utils.hpp"

namespace nq::mtp {

template <typename T>
struct Array {
    using Type = std::remove_cv_t<std::remove_reference_t<T>>;

    std::uint32_t     num_elements = 0;
    std::vector<Type> elements;

    inline Array() = default;

    template <std::size_t N>
    inline Array(const std::array<Type, N> &elements): num_elements(elements.size()) {
        this->elements.reserve(this->num_elements);
        std::copy(elements.begin(), elements.end(), std::back_inserter(this->elements));
    }

    inline Array(const std::vector<T> &elements): num_elements(elements.size()), elements(std::move(elements)) { }

    inline Array(const std::uint32_t *data) {
        this->num_elements += data[0];
        this->elements.reserve(this->num_elements);
        std::copy_n(reinterpret_cast<const Type *>(data + 1), data[0], std::back_inserter(this->elements));
    }

    inline void add(Type element) {
        this->elements.push_back(element);
        ++this->num_elements;
    }

    template <std::size_t N>
    inline void add(const std::array<Type, N> &elements) {
        this->num_elements += elements.size();
        this->elements.reserve(this->num_elements);
        std::copy(elements.begin(), elements.end(), std::back_inserter(this->elements));
    }

    inline void add(const std::vector<Type> &elements) {
        this->num_elements += elements.size();
        this->elements.reserve(this->num_elements);
        std::copy(elements.begin(), elements.end(), std::back_inserter(this->elements));
    }

    inline std::size_t size() const {
        return this->num_elements * sizeof(Type) + sizeof(num_elements);
    }
};

struct String {
    std::uint8_t   num_chars = 0;
    std::u16string chars;

    inline String() = default;

    inline String(const char16_t *data): String(std::u16string(data)) { }
    inline String(const std::u16string &chars): num_chars(chars.size() + 1), chars(chars) { }
    inline String(const char *data): String(std::string(data)) { }
    inline String(const std::string &chars): num_chars(chars.size() + 1), chars(to_utf16(chars)) { }

    inline String(const std::uint8_t *data) {
        this->num_chars = data[0];
        // Ignore null terminator, std::basic_string already manages it
        this->chars.reserve(this->num_chars);
        std::copy_n(reinterpret_cast<const char16_t *>(data + 1), this->num_chars - 1, std::back_inserter(this->chars));
    }

    inline std::size_t size() const {
        return this->num_chars * sizeof(decltype(this->chars)::value_type) + sizeof(this->num_chars);
    }
};

struct DateTime {
    String str;

    inline DateTime() = default;

    inline DateTime(std::uint64_t timestamp) {
        this->format(timestamp);
    }

    void format(std::uint64_t timestamp) {
        auto tmp = std::string(16, 0);
        TimeCalendarTime t;
        R_TRY(timeToCalendarTimeWithMyRule(timestamp, &t, nullptr), return);
        std::snprintf(tmp.data(), tmp.capacity(), "%04d%02d%02dT%02d%02d%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
        this->str = tmp;
    }

    inline operator String() const {
        return this->str;
    }
};

namespace traits {

template <typename T>
struct is_string: public std::false_type { };

template <>
struct is_string<String>: public std::true_type { };

template <>
struct is_string<DateTime>: public std::true_type { };

template <>
struct is_string<std::u16string>: public std::true_type { };

template <>
struct is_string<char16_t *>: public std::true_type { };

template <typename T>
constexpr inline bool is_string_v = is_string<T>::value;

template <typename T>
struct is_array: public std::false_type { };

template <typename T>
struct is_array<Array<T>>: public std::true_type { };

template <typename T>
constexpr inline bool is_array_v = is_array<T>::value;

template <typename T>
constexpr inline bool is_mtp_type_v = traits::is_string_v<T> || traits::is_array_v<T>;

} // namespace traits

} // namespace nq::mtp
