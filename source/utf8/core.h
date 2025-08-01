// Copyright 2006 Nemanja Trifunovic

/*
Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/


#ifndef UTF8_FOR_CPP_CORE_H_2675DCD0_9480_4c0c_B92A_CC14C027B731
#define UTF8_FOR_CPP_CORE_H_2675DCD0_9480_4c0c_B92A_CC14C027B731

#include <iterator>
#include <cstring>
#include <string>

// Determine the C++ standard version.
// If the user defines UTF_CPP_CPLUSPLUS, use that.
// Otherwise, trust the unreliable predefined macro __cplusplus

#if !defined UTF_CPP_CPLUSPLUS
    #define UTF_CPP_CPLUSPLUS __cplusplus
#endif

#if UTF_CPP_CPLUSPLUS >= 201103L // C++ 11 or later
    #define UTF_CPP_OVERRIDE override
    #define UTF_CPP_NOEXCEPT noexcept
    #define UTF_CPP_STATIC_ASSERT(condition) static_assert(condition, "UTFCPP static assert");
#else // C++ 98/03
    #define UTF_CPP_OVERRIDE
    #define UTF_CPP_NOEXCEPT throw()
    // Simulate static_assert:
    template <bool Condition> struct StaticAssert {static void assert() {int static_assert_impl[(Condition ? 1 : -1)];} };
    template <> struct StaticAssert<true> {static void assert() {}};
    #define UTF_CPP_STATIC_ASSERT(condition) StaticAssert<condition>::assert();
#endif // C++ 11 or later


namespace utf8
{
// The typedefs for 8-bit, 16-bit and 32-bit code units
#if UTF_CPP_CPLUSPLUS >= 201103L // C++ 11 or later
    #if UTF_CPP_CPLUSPLUS >= 202002L // C++ 20 or later
        typedef char8_t         utfchar8_t;
    #else // C++ 11/14/17
        typedef unsigned char   utfchar8_t;
    #endif
    typedef char16_t        utfchar16_t;
    typedef char32_t        utfchar32_t;
#else // C++ 98/03
    typedef unsigned char   utfchar8_t;
    typedef unsigned short  utfchar16_t;
    typedef unsigned int    utfchar32_t;
#endif // C++ 11 or later

// Helper code - not intended to be directly called by the library users. May be changed at any time
namespace internal
{
    // Unicode constants
    // Leading (high) surrogates: 0xd800 - 0xdbff
    // Trailing (low) surrogates: 0xdc00 - 0xdfff
    const utfchar16_t LEAD_SURROGATE_MIN  = 0xd800u;
    const utfchar16_t LEAD_SURROGATE_MAX  = 0xdbffu;
    const utfchar16_t TRAIL_SURROGATE_MIN = 0xdc00u;
    const utfchar16_t TRAIL_SURROGATE_MAX = 0xdfffu;
    const utfchar16_t LEAD_OFFSET         = 0xd7c0u;       // LEAD_SURROGATE_MIN - (0x10000 >> 10)
    const utfchar32_t SURROGATE_OFFSET    = 0xfca02400u;   // 0x10000u - (LEAD_SURROGATE_MIN << 10) - TRAIL_SURROGATE_MIN

    // Maximum valid value for a Unicode code point
    const utfchar32_t CODE_POINT_MAX      = 0x0010ffffu;

    template<typename octet_type>
    inline utfchar8_t mask8(octet_type oc)
    {
        return static_cast<utfchar8_t>(0xff & oc);
    }
    template<typename u16_type>
    inline utfchar16_t mask16(u16_type oc)
    {
        return static_cast<utfchar16_t>(0xffff & oc);
    }

    template<typename octet_type>
    inline bool is_trail(octet_type oc)
    {
        return ((utf8::internal::mask8(oc) >> 6) == 0x2);
    }

    inline bool is_lead_surrogate(utfchar32_t cp)
    {
        return (cp >= static_cast<utfchar32_t>(LEAD_SURROGATE_MIN) && cp <= static_cast<utfchar32_t>(LEAD_SURROGATE_MAX));
    }

    inline bool is_trail_surrogate(utfchar32_t cp)
    {
        return (cp >= static_cast<utfchar32_t>(TRAIL_SURROGATE_MIN) && cp <= static_cast<utfchar32_t>(TRAIL_SURROGATE_MAX));
    }

    inline bool is_surrogate(utfchar32_t cp)
    {
        return (cp >= static_cast<utfchar32_t>(LEAD_SURROGATE_MIN) && cp <= static_cast<utfchar32_t>(TRAIL_SURROGATE_MAX));
    }

    inline bool is_code_point_valid(utfchar32_t cp)
    {
        return (cp <= CODE_POINT_MAX && !utf8::internal::is_surrogate(cp));
    }

    inline bool is_in_bmp(utfchar32_t cp)
    {
        return cp < utfchar32_t(0x10000);
    }

    template <typename octet_iterator>
    int sequence_length(octet_iterator lead_it)
    {
        const utfchar8_t lead = utf8::internal::mask8(*lead_it);
        if (lead < 0x80)
            return 1;
        else if ((lead >> 5) == 0x6)
            return 2;
        else if ((lead >> 4) == 0xe)
            return 3;
        else if ((lead >> 3) == 0x1e)
            return 4;
        else
            return 0;
    }

    inline bool is_overlong_sequence(utfchar32_t cp, int length)
    {
        if (cp < 0x80) {
            if (length != 1)
                return true;
        }
        else if (cp < 0x800) {
            if (length != 2)
                return true;
        }
        else if (cp < 0x10000) {
            if (length != 3)
                return true;
        }
        return false;
    }

    enum utf_error {UTF8_OK, NOT_ENOUGH_ROOM, INVALID_LEAD, INCOMPLETE_SEQUENCE, OVERLONG_SEQUENCE, INVALID_CODE_POINT};

    /// Helper for get_sequence_x
    template <typename octet_iterator>
    utf_error increase_safely(octet_iterator& it, const octet_iterator end)
    {
        if (++it == end)
            return NOT_ENOUGH_ROOM;

        if (!utf8::internal::is_trail(*it))
            return INCOMPLETE_SEQUENCE;

        return UTF8_OK;
    }

    #define UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(IT, END) {utf_error ret = increase_safely(IT, END); if (ret != UTF8_OK) return ret;}

    /// get_sequence_x functions decode utf-8 sequences of the length x
    template <typename octet_iterator>
    utf_error get_sequence_1(octet_iterator& it, octet_iterator end, utfchar32_t& code_point)
    {
        if (it == end)
            return NOT_ENOUGH_ROOM;

        code_point = utf8::internal::mask8(*it);

        return UTF8_OK;
    }

    template <typename octet_iterator>
    utf_error get_sequence_2(octet_iterator& it, octet_iterator end, utfchar32_t& code_point)
    {
        if (it == end)
            return NOT_ENOUGH_ROOM;

        code_point = utf8::internal::mask8(*it);

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = ((code_point << 6) & 0x7ff) + ((*it) & 0x3f);

        return UTF8_OK;
    }

    template <typename octet_iterator>
    utf_error get_sequence_3(octet_iterator& it, octet_iterator end, utfchar32_t& code_point)
    {
        if (it == end)
            return NOT_ENOUGH_ROOM;

        code_point = utf8::internal::mask8(*it);

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = ((code_point << 12) & 0xffff) + ((utf8::internal::mask8(*it) << 6) & 0xfff);

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = static_cast<utfchar32_t>(code_point + ((*it) & 0x3f));

        return UTF8_OK;
    }

    template <typename octet_iterator>
    utf_error get_sequence_4(octet_iterator& it, octet_iterator end, utfchar32_t& code_point)
    {
        if (it == end)
           return NOT_ENOUGH_ROOM;

        code_point = utf8::internal::mask8(*it);

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = ((code_point << 18) & 0x1fffff) + ((utf8::internal::mask8(*it) << 12) & 0x3ffff);

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = static_cast<utfchar32_t>(code_point + ((utf8::internal::mask8(*it) << 6) & 0xfff));

        UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR(it, end)

        code_point = static_cast<utfchar32_t>(code_point + ((*it) & 0x3f));

        return UTF8_OK;
    }

    #undef UTF8_CPP_INCREASE_AND_RETURN_ON_ERROR

    template <typename octet_iterator>
    utf_error validate_next(octet_iterator& it, octet_iterator end, utfchar32_t& code_point)
    {
        if (it == end)
            return NOT_ENOUGH_ROOM;

        // Save the original value of it so we can go back in case of failure
        // Of course, it does not make much sense with i.e. stream iterators
        octet_iterator original_it = it;

        utfchar32_t cp = 0;
        // Determine the sequence length based on the lead octet
        const int length = utf8::internal::sequence_length(it);

        // Get trail octets and calculate the code point
        utf_error err = UTF8_OK;
        switch (length) {
            case 0:
                return INVALID_LEAD;
            case 1:
                err = utf8::internal::get_sequence_1(it, end, cp);
                break;
            case 2:
                err = utf8::internal::get_sequence_2(it, end, cp);
            break;
            case 3:
                err = utf8::internal::get_sequence_3(it, end, cp);
            break;
            case 4:
                err = utf8::internal::get_sequence_4(it, end, cp);
            break;
        }

        if (err == UTF8_OK) {
            // Decoding succeeded. Now, security checks...
            if (utf8::internal::is_code_point_valid(cp)) {
                if (!utf8::internal::is_overlong_sequence(cp, length)){
                    // Passed! Return here.
                    code_point = cp;
                    ++it;
                    return UTF8_OK;
                }
                else
                    err = OVERLONG_SEQUENCE;
            }
            else
                err = INVALID_CODE_POINT;
        }

        // Failure branch - restore the original value of the iterator
        it = original_it;
        return err;
    }

    template <typename octet_iterator>
    inline utf_error validate_next(octet_iterator& it, octet_iterator end) {
        utfchar32_t ignored;
        return utf8::internal::validate_next(it, end, ignored);
    }

    template <typename word_iterator>
    utf_error validate_next16(word_iterator& it, word_iterator end, utfchar32_t& code_point)
    {
        // Make sure the iterator dereferences a large enough type
        typedef typename std::iterator_traits<word_iterator>::value_type word_type;
        UTF_CPP_STATIC_ASSERT(sizeof(word_type) >= sizeof(utfchar16_t));
        // Check the edge case:
        if (it == end)
            return NOT_ENOUGH_ROOM;
        // Save the original value of it so we can go back in case of failure
        // Of course, it does not make much sense with i.e. stream iterators
        word_iterator original_it = it;

        utf_error err = UTF8_OK;

        const utfchar16_t first_word = *it++;
        if (!is_surrogate(first_word)) {
            code_point = first_word;
            return UTF8_OK;
        }
        else {
            if (it == end)
                err = NOT_ENOUGH_ROOM;
            else if (is_lead_surrogate(first_word)) {
                const utfchar16_t second_word = *it++;
                if (is_trail_surrogate(static_cast<utfchar32_t>(second_word))) {
                    code_point = static_cast<utfchar32_t>(first_word << 10) +  static_cast<utfchar32_t>(second_word) + SURROGATE_OFFSET;
                    return UTF8_OK;
                } else
                    err = INCOMPLETE_SEQUENCE;

            } else {
                err = INVALID_LEAD;
            }
        }
        // error branch
        it = original_it;
        return err;
    }

    // Internal implementation of both checked and unchecked append() function
    // This function will be invoked by the overloads below, as they will know
    // the octet_type.
    template <typename octet_iterator, typename octet_type>
    octet_iterator append(utfchar32_t cp, octet_iterator result) {
        if (cp < 0x80)                        // one octet
            *(result++) = static_cast<octet_type>(cp);
        else if (cp < 0x800) {                // two octets
            *(result++) = static_cast<octet_type>((cp >> 6)          | 0xc0);
            *(result++) = static_cast<octet_type>((cp & 0x3f)        | 0x80);
        }
        else if (cp < 0x10000) {              // three octets
            *(result++) = static_cast<octet_type>((cp >> 12)         | 0xe0);
            *(result++) = static_cast<octet_type>(((cp >> 6) & 0x3f) | 0x80);
            *(result++) = static_cast<octet_type>((cp & 0x3f)        | 0x80);
        }
        else {                                // four octets
            *(result++) = static_cast<octet_type>((cp >> 18)         | 0xf0);
            *(result++) = static_cast<octet_type>(((cp >> 12) & 0x3f)| 0x80);
            *(result++) = static_cast<octet_type>(((cp >> 6) & 0x3f) | 0x80);
            *(result++) = static_cast<octet_type>((cp & 0x3f)        | 0x80);
        }
        return result;
    }

    // One of the following overloads will be invoked from the API calls

    // A simple (but dangerous) case: the caller appends byte(s) to a char array
    inline char* append(utfchar32_t cp, char* result) {
        return append<char*, char>(cp, result);
    }

    // Hopefully, most common case: the caller uses back_inserter
    // i.e. append(cp, std::back_inserter(str));
    template<typename container_type>
    std::back_insert_iterator<container_type> append
            (utfchar32_t cp, std::back_insert_iterator<container_type> result) {
        return append<std::back_insert_iterator<container_type>,
            typename container_type::value_type>(cp, result);
    }

    // The caller uses some other kind of output operator - not covered above
    // Note that in this case we are not able to determine octet_type
    // so we assume it's utfchar8_t; that can cause a conversion warning if we are wrong.
    template <typename octet_iterator>
    octet_iterator append(utfchar32_t cp, octet_iterator result) {
        return append<octet_iterator, utfchar8_t>(cp, result);
    }

    // Internal implementation of both checked and unchecked append16() function
    // This function will be invoked by the overloads below, as they will know
    // the word_type.
    template <typename word_iterator, typename word_type>
    word_iterator append16(utfchar32_t cp, word_iterator result) {
        UTF_CPP_STATIC_ASSERT(sizeof(word_type) >= sizeof(utfchar16_t));
        if (is_in_bmp(cp))
            *(result++) = static_cast<word_type>(cp);
        else {
            // Code points from the supplementary planes are encoded via surrogate pairs
            *(result++) = static_cast<word_type>(LEAD_OFFSET + (cp >> 10));
            *(result++) = static_cast<word_type>(TRAIL_SURROGATE_MIN + (cp & 0x3FF));
        }
        return result;
    }

    // Hopefully, most common case: the caller uses back_inserter
    // i.e. append16(cp, std::back_inserter(str));
    template<typename container_type>
    std::back_insert_iterator<container_type> append16
            (utfchar32_t cp, std::back_insert_iterator<container_type> result) {
        return append16<std::back_insert_iterator<container_type>,
            typename container_type::value_type>(cp, result);
    }

    // The caller uses some other kind of output operator - not covered above
    // Note that in this case we are not able to determine word_type
    // so we assume it's utfchar16_t; that can cause a conversion warning if we are wrong.
    template <typename word_iterator>
    word_iterator append16(utfchar32_t cp, word_iterator result) {
        return append16<word_iterator, utfchar16_t>(cp, result);
    }

} // namespace internal

    /// The library API - functions intended to be called by the users

    // Byte order mark
    const utfchar8_t bom[] = {0xef, 0xbb, 0xbf};

    template <typename octet_iterator>
    octet_iterator find_invalid(octet_iterator start, octet_iterator end)
    {
        octet_iterator result = start;
        while (result != end) {
            utf8::internal::utf_error err_code = utf8::internal::validate_next(result, end);
            if (err_code != internal::UTF8_OK)
                return result;
        }
        return result;
    }

    inline const char* find_invalid(const char* str)
    {
        const char* end = str + std::strlen(str);
        return find_invalid(str, end);
    }

    inline std::size_t find_invalid(const std::string& s)
    {
        std::string::const_iterator invalid = find_invalid(s.begin(), s.end());
        return (invalid == s.end()) ? std::string::npos : static_cast<std::size_t>(invalid - s.begin());
    }

    template <typename octet_iterator>
    inline bool is_valid(octet_iterator start, octet_iterator end)
    {
        return (utf8::find_invalid(start, end) == end);
    }

    inline bool is_valid(const char* str)
    {
        return (*(utf8::find_invalid(str)) == '\0');
    }

    inline bool is_valid(const std::string& s)
    {
        return is_valid(s.begin(), s.end());
    }



    template <typename octet_iterator>
    inline bool starts_with_bom (octet_iterator it, octet_iterator end)
    {
        return (
            ((it != end) && (utf8::internal::mask8(*it++)) == bom[0]) &&
            ((it != end) && (utf8::internal::mask8(*it++)) == bom[1]) &&
            ((it != end) && (utf8::internal::mask8(*it))   == bom[2])
           );
    }

    inline bool starts_with_bom(const std::string& s)
    {
        return starts_with_bom(s.begin(), s.end());
    }
} // namespace utf8

#endif // header guard

