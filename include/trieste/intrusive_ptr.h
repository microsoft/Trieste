#pragma once

#include "snmalloc/ds_core/defines.h"

#include <cassert>
#include <cstddef>
#include <functional>
#include <ostream>
#include <utility>

namespace trieste
{
  struct intrusive_refcounted_blk
  {
  private:
    size_t intrusive_refcount = 0;

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
      if (intrusive_refcount == 1)
      {
        intrusive_refcount = 0;
        delete this;
      }
      else
      {
        intrusive_refcount -= 1;
      }
    }

  public:
    template<typename T>
    friend struct intrusive_ptr;

    // impl note:
    // This is virtual, because Trieste relies heavily on being able to
    // use incomplete types in these pointers.
    // The vtable replaces shared_ptr's way of tolerating that, by allowing
    // the compiler to emit vtable lookups when it wants to
    // call the destructor, rather than run into undefined behavior.

    constexpr virtual ~intrusive_refcounted_blk()
    {
      assert(intrusive_refcount == 0);
    }
  };

  template<typename T>
  struct intrusive_ptr final
  {
  private:
    intrusive_refcounted_blk* ptr;

    constexpr void inc_ref()
    {
      if (ptr)
      {
        ptr->intrusive_inc_ref();
      }
    }

    constexpr void dec_ref()
    {
      if (ptr)
      {
        ptr->intrusive_dec_ref();
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
    constexpr intrusive_ptr(const intrusive_ptr<U>& other) : ptr{other.get()}
    {
      inc_ref();
    }

    template<typename U>
    constexpr intrusive_ptr(intrusive_ptr<U>&& other) : ptr{other.get()}
    {
      other.release();
    }

    constexpr intrusive_ptr(const intrusive_ptr<T>& other) : ptr{other.ptr}
    {
      inc_ref();
    }

    constexpr intrusive_ptr(intrusive_ptr<T>&& other) : ptr{nullptr}
    {
      std::swap(ptr, other.ptr);
    }

    constexpr intrusive_ptr<T>& operator=(const intrusive_ptr<T>& other)
    {
      // Sets us to nullptr and holds onto our ptr in tmp
      intrusive_ptr<T> tmp = std::move(*this);
      ptr = other.ptr;
      inc_ref();
      // dec_ref for our original ptr goes here, where tmp gets destroyed.
      // If ptr == other.ptr, this ensures we don't accidentally delete ptr
      // on self-assignment because refcount never hits 0 (it goes 1, 2, 1
      // instead).
      return *this;
    }

    constexpr intrusive_ptr<T>& operator=(intrusive_ptr<T>&& other)
    {
      std::swap(ptr, other.ptr);
      other.dec_ref();
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
      // our "runtime check" for this being ok is by construction
      // of the pointer itself.
      // We can't have been given some U* that is not valid to static_cast to
      // T*, because none of our methods allow it.
      return static_cast<T*>(ptr);
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

    constexpr ~intrusive_ptr()
    {
      dec_ref();
    }

    friend std::hash<intrusive_ptr<T>>;
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

  template<typename T>
  struct intrusive_refcounted : public intrusive_refcounted_blk
  {
  public:
    constexpr intrusive_ptr<T> intrusive_ptr_from_this()
    {
      return intrusive_ptr{static_cast<T*>(this)};
    }
  };
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
