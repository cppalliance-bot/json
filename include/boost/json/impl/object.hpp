//
// Copyright (c) 2018-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/json
//

#ifndef BOOST_JSON_IMPL_OBJECT_HPP
#define BOOST_JSON_IMPL_OBJECT_HPP

#include <boost/json/value.hpp>
#include <boost/json/detail/except.hpp>
#include <boost/json/detail/exchange.hpp>
#include <boost/json/detail/string.hpp>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace boost {
namespace json {

//----------------------------------------------------------

unchecked_object::
~unchecked_object()
{
    if(data_)
        object::value_type::destroy(
            data_, size_);
}

void
unchecked_object::
relocate(object::value_type* dest) noexcept
{
    std::memcpy(
        reinterpret_cast<void*>(dest),
        data_, sizeof(object::value_type) * size_);
    data_ = nullptr;
}

//----------------------------------------------------------

void
object::
impl_type::
remove(
    value_type*& head,
    value_type* p) noexcept
{
    if(head == p)
    {
        head = head->next_;
        return;
    }
    auto prev = head;
    while(prev->next_ != p)
        prev = prev->next_;
    prev->next_ = p->next_;
}

auto
object::
impl_type::
bucket(string_view key) const noexcept ->
    value_type*&
{
    auto const hash = digest(key);
    auto const i = hash % buckets();
    return bucket_begin()[i];
}

auto
object::
impl_type::
bucket(std::size_t hash) const noexcept ->
    value_type*&
{
    return bucket_begin()[hash % buckets()];
}

auto
object::
impl_type::
begin() const noexcept ->
    value_type*
{
    if(! tab_)
        return nullptr;
    return reinterpret_cast<
        value_type*>(tab_ + 1);
}

auto
object::
impl_type::
end() const noexcept ->
    value_type*
{
    return begin() + size();
}

auto
object::
impl_type::
bucket_begin() const noexcept ->
    value_type**
{
    return reinterpret_cast<
        value_type**>(
            begin() + capacity());
}

auto
object::
impl_type::
bucket_end() const noexcept ->
    value_type**
{
    return bucket_begin() + buckets();
}

//----------------------------------------------------------

class object::undo_construct
{
    object& self_;

public:
    bool commit = false;

    ~undo_construct()
    {
        if(! commit)
            self_.impl_.destroy(
                self_.sp_);
    }

    explicit
    undo_construct(
        object& self) noexcept
        : self_(self)
    {
    }
};

//----------------------------------------------------------

class object::undo_insert
{
    object& self_;

public:
    value_type* const first;
    value_type* last;
    bool commit = false;

    ~undo_insert()
    {
        if(commit)
        {
            self_.impl_.grow(last - first);
        }
        else
        {
            for(auto it = first; it != last; ++it)
                self_.impl_.remove(
                    self_.impl_.bucket(it->key()), it);
            value_type::destroy(
                first, static_cast<
                    std::size_t>(last - first));
        }
    }

    explicit
    undo_insert(
        object& self) noexcept
        : self_(self)
        , first(self_.impl_.end())
        , last(first)
    {
    }
};

//----------------------------------------------------------
//
// object
//
//----------------------------------------------------------

template<class InputIt, class>
object::
object(
    InputIt first,
    InputIt last,
    std::size_t min_capacity,
    storage_ptr sp)
    : sp_(std::move(sp))
{
    undo_construct u(*this);
    insert_range(
        first, last,
        min_capacity);
    u.commit = true;
}

//----------------------------------------------------------
//
// Iterators
//
//----------------------------------------------------------

auto
object::
begin() noexcept ->
    iterator
{
    return impl_.begin();
}

auto
object::
begin() const noexcept ->
    const_iterator
{
    return impl_.begin();
}

auto
object::
cbegin() const noexcept ->
    const_iterator
{
    return impl_.begin();
}

auto
object::
end() noexcept ->
    iterator
{
    return impl_.end();
}

auto
object::
end() const noexcept ->
    const_iterator
{
    return impl_.end();
}

auto
object::
cend() const noexcept ->
    const_iterator
{
    return impl_.end();
}

auto
object::
rbegin() noexcept ->
    reverse_iterator
{
    return reverse_iterator(end());
}

auto
object::
rbegin() const noexcept ->
    const_reverse_iterator
{
    return const_reverse_iterator(end());
}

auto
object::
crbegin() const noexcept ->
    const_reverse_iterator
{
    return const_reverse_iterator(end());
}

auto
object::
rend() noexcept ->
    reverse_iterator
{
    return reverse_iterator(begin());
}

auto
object::
rend() const noexcept ->
    const_reverse_iterator
{
    return const_reverse_iterator(begin());
}

auto
object::
crend() const noexcept ->
    const_reverse_iterator
{
    return const_reverse_iterator(begin());
}

//----------------------------------------------------------
//
// Capacity
//
//----------------------------------------------------------

bool
object::
empty() const noexcept
{
    return impl_.size() == 0;
}

auto
object::
size() const noexcept ->
    std::size_t
{
    return impl_.size();
}

auto
object::
capacity() const noexcept ->
    std::size_t
{
    return impl_.capacity();
}

void
object::
reserve(std::size_t new_capacity)
{
    if(new_capacity <= capacity())
        return;
    rehash(new_capacity);
}

//----------------------------------------------------------

template<class P, class>
auto
object::
insert(P&& p) ->
    std::pair<iterator, bool>
{
    reserve(size() + 1);
    auto& e = *::new(
        impl_.end()) value_type(
            std::forward<P>(p), sp_);
    auto const result = find_impl(e.key());
    if(result.first)
    {
        e.~value_type();
        return { result.first, false };
    }
    auto& head =
        impl_.bucket(result.second);
    e.next_ = head;
    head = &e;
    impl_.grow(1);
    return { &e, true };
}

template<class M>
auto
object::
insert_or_assign(
    key_type key, M&& m) ->
        std::pair<iterator, bool>
{
    auto const result = find_impl(key);
    if(result.first)
    {
        result.first->value() =
            std::forward<M>(m);
        return { result.first, false };
    }
    reserve(size() + 1);
    auto& e = *::new(
        impl_.end()) value_type(
            key, std::forward<M>(m), sp_);
    auto& head =
        impl_.bucket(result.second);
    e.next_ = head;
    head = &e;
    impl_.grow(1);
    return { &e, true };
}

template<class Arg>
auto
object::
emplace(
    key_type key,
    Arg&& arg) ->
        std::pair<iterator, bool>
{
    auto const result = find_impl(key);
    if(result.first)
        return { result.first, false };
    reserve(size() + 1);
    auto p = ::new(impl_.end()) value_type(
        key, std::forward<Arg>(arg), sp_);
    auto& head = impl_.bucket(result.second);
    p->next_ = head;
    head = p;
    impl_.grow(1);
    return { p, true };
}

//----------------------------------------------------------

inline
void
swap(object& lhs, object& rhs)
{
    lhs.swap(rhs);
}

//----------------------------------------------------------
//
// (implementation)
//
//----------------------------------------------------------

std::uint32_t
object::
digest(
    key_type key,
    std::false_type) noexcept
{
    std::uint32_t prime = 0x01000193UL;
    std::uint32_t hash  = 0x811C9DC5UL;
    for(auto p = key.begin(),
        end = key.end(); p != end; ++p)
        hash = (*p ^ hash) * prime;
    return hash;
}

std::uint64_t
object::
digest(
    key_type key,
    std::true_type) noexcept
{
    std::uint64_t prime = 0x100000001B3ULL;
    std::uint64_t hash  = 0xcbf29ce484222325ULL;
    for(auto p = key.begin(),
        end = key.end(); p != end; ++p)
        hash = (*p ^ hash) * prime;
    return hash;
}

std::size_t
object::
digest(key_type key) noexcept
{
    return digest(key,
        std::integral_constant<bool,
            sizeof(std::size_t) ==
            sizeof(std::uint64_t)>{});
}

template<class InputIt>
void
object::
insert_range(
    InputIt first,
    InputIt last,
    std::size_t min_capacity,
    std::input_iterator_tag)
{
    reserve(min_capacity);
    undo_insert u(*this);
    while(first != last)
    {
        reserve(size() + 1);
        auto& e = *::new(
            u.last) value_type(*first++, sp_);
        auto& head = impl_.bucket(e.key());
        for(auto it = head;;
            it = it->next_)
        {
            if(it)
            {
                if(it->key() != e.key())
                    continue;
                e.~value_type();
            }
            else
            {
                e.next_ = head;
                head = &e;
                ++u.last;
            }
            break;
        }
    }
    u.commit = true;
}

template<class InputIt>
void
object::
insert_range(
    InputIt first,
    InputIt last,
    std::size_t min_capacity,
    std::random_access_iterator_tag)
{
    auto n = static_cast<
        std::size_t>(last - first);
    auto const n0 = size();
    if(n > max_size() - n0)
        BOOST_JSON_THROW(
            detail::object_too_large_exception());
    if( min_capacity < n0 + n)
        min_capacity = n0 + n;
    reserve(min_capacity);
    undo_insert u(*this);
    while(n--)
    {
        auto& e = *::new(
            u.last) value_type(*first++, sp_);
        auto& head = impl_.bucket(e.key());
        for(auto it = head;;
            it = it->next_)
        {
            if(it)
            {
                if(it->key() != e.key())
                    continue;
                e.~value_type();
            }
            else
            {
                e.next_ = head;
                head = &e;
                ++u.last;
            }
            break;
        }
    }
    u.commit = true;
}

} // json
} // boost

#endif
