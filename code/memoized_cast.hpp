///
/// \file memoized_cast.hpp
///
/// This file defines function memoized_cast<T>(U) that behaves as dynamic_cast
/// but is much faster when multiple invokations are involved because of caching
///
/// \autor Yuriy Solodkyy <yuriy.solodkyy@gmail.com>
///
/// This file is a part of the XTL framework (http://parasol.tamu.edu/xtl/).
/// Copyright (C) 2005-2011 Texas A&M University.
/// All rights reserved.
///

#include "vtblmap.hpp"
#include <vector>

// TODO: 
// 1. vtbl with pointers directly to table instead of indecies - slows
// 2. store type index inside match_members
// 3. try smaller type int instead of size_t or ptrdiff_t
// 4. Problem: different amounts of virtual functions in the base class 
//             change the irrelevant bits. 
//    - This shouldn't be a problem in compiler implementation of pattern
//      matching as compiler will know this value
//    - For now we can make it a parameter of switch as well
//    - Try using Pearson hash to avoid dependence on irrelevant bits
// 5. Preallocate vtbl map - improves sequential but slows down random, 
//    probably because of locality.

//------------------------------------------------------------------------------

/// A class that keeps a run-time instantiation counter of different types. 
/// The counter is one per each type U
template <typename U>
struct specific_to
{
    /// Returns the type index of a given type T among those instantiated in association with U
    template <typename T>
    static inline size_t type_index_of()
    {
        static const size_t ti = ++type_counter; // will be executed once upone first entry
        return ti;
    }

private:

    static size_t type_counter; ///< Actual counter of instantiated types

};

template <typename U> size_t specific_to<U>::type_counter = 0;

//------------------------------------------------------------------------------

template <typename T> struct cast_target;
template <typename T> struct cast_target<      T*> { typedef T type; };
template <typename T> struct cast_target<const T*> { typedef T type; };

//------------------------------------------------------------------------------

/// Appendix B of the C++ standard lists implementation quantities, among which 
/// there is: Size of an object [262 144]. This is only a minimum guideline and 
/// a concrete implementation might have it set larger, but it gives us some 
/// reassurance that the following designated constants will be unlikely to 
/// represent a valid offset within an object.
static const std::ptrdiff_t no_cast_exists = 0x0FF1C1A1; // A dedicated constant marking impossible offset
static const std::ptrdiff_t unknown_offset = 0x0FF1C1A0; // A dedicated constant marking an offset that hasn't been computed yet

//------------------------------------------------------------------------------

/// Allocates one vtblmap per target type.
/// Elements of vtblmap are offsets of target type from p.
/// \note Typically we will have more target types than source types as source 
///       types represent static type of an object while target types - its 
///       dynamic type.
template <typename T>
inline std::ptrdiff_t& per_target_offset_of(const void* p)
{
    /// The only purpose of this class is to have @unknown_offset be default
    /// value of otherwise a std::ptrdiff_t variable. 
    struct dyn_cast_info
    {
        dyn_cast_info() : offset(unknown_offset) {}
        std::ptrdiff_t offset;
    };

    static vtblmap<dyn_cast_info&> offset_map;
    return offset_map.get(p).offset;
}

//------------------------------------------------------------------------------

/// Allocates one vtblmap per source type.
/// Elements of vtblmap are arrays indexed by target type.
/// \note Typically we will have more target types than source types as source 
///       types represent static type of an object while target types - its 
///       dynamic type.
template <typename S>
inline std::ptrdiff_t& per_source_offset_of(const void* p, size_t ti)
{
    // FIX: vector here might create problems later when we implement 
    //      multi-threaded solution because of reference invalidation
    typedef std::vector<std::ptrdiff_t> dyn_cast_info;
    static vtblmap<dyn_cast_info&> offset_map;
    dyn_cast_info& sdci = offset_map.get(p);

    if (XTL_UNLIKELY(ti >= sdci.size()))
        sdci.resize(ti+1,unknown_offset);

    return sdci[ti];
}

//------------------------------------------------------------------------------

/// Version of memoized_cast that assumes that argument is non-null.
/// Used under the hood in the rest of the library to avoid repeated checking.
template <typename T, typename S>
inline T memoized_cast_non_null(const S* p)
{
    typedef typename cast_target<T >::type target_type;
    typedef typename cast_target<S*>::type source_type;
#if 1
    // Per source version is much more efficient in the amount of used memory
    // and size of generated executable.
    size_t ti = specific_to<source_type>::template type_index_of<target_type>();
    std::ptrdiff_t& offset = per_source_offset_of<source_type>(p,ti);
#else
    // Per target version is simpler and straightforward, but very inefficient
    // in the size of generated code.
    std::ptrdiff_t& offset = per_target_offset_of<target_type>(p);
#endif

    if (XTL_UNLIKELY(offset == unknown_offset))
    {
        T t = dynamic_cast<T>(p);
        offset = t 
                 ? reinterpret_cast<const char*>(t)-reinterpret_cast<const char*>(p) 
                 : no_cast_exists;
        return t;
    }
    else
        return offset == no_cast_exists ? 0 : adjust_ptr<target_type>(p, offset);
}

//------------------------------------------------------------------------------

/// Non-const version of the above.
template <typename T, typename S>
inline T memoized_cast_non_null(S* p)
{
    typedef typename cast_target<T >::type target_type;
    typedef typename cast_target<S*>::type source_type;
    return const_cast<T>(memoized_cast_non_null<target_type const*>(const_cast<source_type const*>(p)));
}

//------------------------------------------------------------------------------

/// Actual implementation of memoized_cast on pointers.
/// \note Type T here is assumed to be of pointer type!
template <typename T, typename S>
inline T memoized_cast(S* p)
{
    if (XTL_LIKELY(p))
        return memoized_cast_non_null<T>(p);
    else
        return 0;    
}

//------------------------------------------------------------------------------
