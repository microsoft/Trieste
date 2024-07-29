#pragma once

#include "snmalloc/ds_core/defines.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <ostream>
#include <utility>

namespace trieste
{
  namespace detail
  {
    // In principle, std::atomic should not be copied.
    // It should be a single object that is pointer-to and manipulated by
    // multiple threads. For refcounts however, it should be possible to copy a
    // refcounted object. The catch is that _everything but the refcount should
    // be copied_. The copy constructors here will just set the new refcount to
    // 0, as if the object was constructed from scratch, so different
    // intrusive_ptr can take ownership of the new object.
    struct copyable_refcount final
    {
    private:
      std::atomic<size_t> value = 0;

    public:
      constexpr copyable_refcount(size_t value_) : value{value_} {}

      constexpr copyable_refcount() = default;
      constexpr copyable_refcount(const copyable_refcount&) : value{0} {}
      constexpr copyable_refcount(copyable_refcount&&) : value{0} {}

      operator size_t() const
      {
        return value;
      }

      copyable_refcount operator+=(size_t inc)
      {
        value += inc;
        return *this;
      }

      size_t fetch_sub(size_t dec)
      {
        return value.fetch_sub(dec);
      }
    };
  }

  template<typename T>
  struct intrusive_refcounted_traits
  {
    static constexpr void intrusive_inc_ref(T* ptr)
    {
      ptr->intrusive_inc_ref();
    }

    static constexpr void intrusive_dec_ref(T* ptr)
    {
      ptr->intrusive_dec_ref();
    }
  };

  template<typename T>
  struct intrusive_ptr final
  {
  private:
    T* ptr;

    constexpr void inc_ref()
    {
      if (ptr)
      {
        intrusive_refcounted_traits<T>::intrusive_inc_ref(ptr);
      }
    }

    constexpr void dec_ref()
    {
      if (ptr)
      {
        intrusive_refcounted_traits<T>::intrusive_dec_ref(ptr);
        ptr = nullptr;
      }
    }

  public:
    constexpr intrusive_ptr() : ptr{nullptr} {}

    constexpr intrusive_ptr(std::nullptr_t) : ptr{nullptr} {}

    constexpr explicit intrusive_ptr(T* ptr_) : ptr{ptr_}
    {
      inc_ref();
    }

    template<typename U>
    constexpr intrusive_ptr(const intrusive_ptr<U>& other) : ptr{other.ptr}
    {
      inc_ref();
    }

    template<typename U>
    constexpr intrusive_ptr(intrusive_ptr<U>&& other) : ptr{other.release()}
    {}

    constexpr intrusive_ptr(const intrusive_ptr<T>& other) : ptr{other.ptr}
    {
      inc_ref();
    }

    constexpr intrusive_ptr(intrusive_ptr<T>&& other) : ptr{other.release()} {}

    constexpr intrusive_ptr<T>& operator=(const intrusive_ptr<T>& other)
    {
      // Self-assignment case (skip the code below that would destroy the object
      // otherwise)
      if (ptr == other.ptr)
      {
        return *this;
      }

      dec_ref();
      ptr = other.ptr;
      inc_ref();
      return *this;
    }

    constexpr intrusive_ptr<T>& operator=(intrusive_ptr<T>&& other)
    {
      std::swap(ptr, other.ptr);
      return *this;
    }

    constexpr void swap(intrusive_ptr<T>& other)
    {
      std::swap(ptr, other.ptr);
    }

    constexpr void reset()
    {
      dec_ref();
    }

    constexpr T* get() const
    {
      return ptr;
    }

    constexpr T* operator->() const
    {
      return get();
    }

    constexpr T& operator*() const
    {
      return *get();
    }

    constexpr operator bool() const
    {
      return ptr;
    }

    constexpr T* release()
    {
      auto p = get();
      ptr = nullptr;
      return p;
    }

    ~intrusive_ptr()
    {
      dec_ref();
    }

    friend std::hash<intrusive_ptr<T>>;
  };

  template<typename T>
  struct intrusive_refcounted
  {
  private:
    detail::copyable_refcount intrusive_refcount = 0;

    constexpr void intrusive_inc_ref()
    {
      intrusive_refcount += 1;
    }

    // It's better to have the non-null case dec_ref code all in one place,
    // because it's long for something that might be pasted over 10x
    // into functions that use intrusive_ptr a lot.
    SNMALLOC_SLOW_PATH
    constexpr void intrusive_dec_ref()
    {
      // Atomically subtract 1 from refcount and get the _old value_.
      size_t prev_rc = intrusive_refcount.fetch_sub(1);
      // If the value _was_ 0, we just did a negative wrap-around to
      // max(size_t). We should stop now and think about how we got here.
      assert(prev_rc > 0);

      // If the value was 1, it is now 0 and we can clean up.
      if (prev_rc == 1)
      {
        delete static_cast<T*>(this);
      }
    }

  public:
    template<typename>
    friend struct intrusive_refcounted_traits;

    constexpr intrusive_ptr<T> intrusive_ptr_from_this()
    {
      return intrusive_ptr{static_cast<T*>(this)};
    }

    ~intrusive_refcounted()
    {
      assert(intrusive_refcount == 0);
    }
  };

  template<typename T, typename U>
  constexpr intrusive_ptr<U> static_pointer_cast(const intrusive_ptr<T>& ptr)
  {
    return intrusive_ptr(static_cast<U*>(ptr.get()));
  }

  template<typename T, typename U>
  constexpr intrusive_ptr<U> dynamic_pointer_cast(const intrusive_ptr<T>& ptr)
  {
    // nullptr dynamic_cast case handled: constructor tolerates it anyway
    return intrusive_ptr(dynamic_cast<U*>(ptr.get()));
  }

  template<typename T, typename U>
  constexpr intrusive_ptr<U> const_pointer_cast(const intrusive_ptr<T>& ptr)
  {
    return intrusive_ptr(const_cast<U*>(ptr.get()));
  }

  // impl note:
  // It is important that these functions are non-member template functions.
  // If you make them just member functions, they clash with operator==(Node,
  // const Token&) defined elsewhere.

  template<typename T, typename U>
  constexpr bool
  operator==(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() == rhs.get();
  }

  template<typename T>
  constexpr bool operator==(const intrusive_ptr<T>& lhs, std::nullptr_t)
  {
    return lhs.get() == nullptr;
  }

  template<typename T>
  constexpr bool operator==(std::nullptr_t, const intrusive_ptr<T>& rhs)
  {
    return nullptr == rhs.get();
  }

  template<typename T, typename U>
  constexpr bool
  operator!=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() != rhs.get();
  }

  template<typename T>
  constexpr bool operator!=(const intrusive_ptr<T>& lhs, std::nullptr_t)
  {
    return lhs.get() != nullptr;
  }

  template<typename T>
  constexpr bool operator!=(std::nullptr_t, const intrusive_ptr<T>& rhs)
  {
    return nullptr != rhs.get();
  }

  template<typename T, typename U>
  constexpr bool
  operator<(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() < rhs.get();
  }

  template<typename T, typename U>
  constexpr bool
  operator>(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() > rhs.get();
  }

  template<typename T, typename U>
  constexpr bool
  operator<=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() <= rhs.get();
  }

  template<typename T, typename U>
  constexpr bool
  operator>=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs)
  {
    return lhs.get() >= rhs.get();
  }

  template<typename T>
  std::ostream& operator<<(std::ostream& os, const intrusive_ptr<T> ptr)
  {
    return os << ptr.get();
  }
}

namespace std
{
  template<typename T>
  struct hash<trieste::intrusive_ptr<T>>
  {
    size_t operator()(const trieste::intrusive_ptr<T> ptr) const
    {
      return std::hash<T*>{}(ptr.ptr);
    }
  };

  template<typename T>
  void swap(trieste::intrusive_ptr<T>& lhs, trieste::intrusive_ptr<T>& rhs)
  {
    lhs.swap(rhs);
  }
}
