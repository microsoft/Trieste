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
      assert(intrusive_refcount > 0);
      intrusive_refcount -= 1;
      if (intrusive_refcount == 0)
      {
        delete this;
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

    virtual ~intrusive_refcounted_blk() = 0;
  };

  inline intrusive_refcounted_blk::~intrusive_refcounted_blk()
  {
    assert(intrusive_refcount == 0);
  }

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
    constexpr intrusive_ptr(const intrusive_ptr<U>& other) : ptr{nullptr}
    {
      // enforce U* to T* compatibility
      T* tmp = other.get();
      ptr = tmp;
      inc_ref();
    }

    template<typename U>
    constexpr intrusive_ptr(intrusive_ptr<U>&& other) : ptr{nullptr}
    {
      // to enforce the pointer compatibility (from U* to T* to
      // intrusive_refcounted_blk*)
      T* tmp = other.release();
      ptr = tmp;
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
      // hold onto our old ptr without incrementing refcount, then destroy it at
      // end method (using the actual constructor here causes a memory leak by
      // incrementing refcount one too many times)
      intrusive_ptr<T> tmp;
      tmp.ptr = ptr;

      ptr = other.ptr;

      // this happens before tmp is destroyed, meaning
      // we get this->inc_ref(); tmp.dec_ref().
      // If we are doing a self-assign, then the inc and dec order
      // ensures we don't deallocate.
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

    ~intrusive_ptr()
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
