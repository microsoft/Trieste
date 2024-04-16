#pragma once

#include <version>
#ifdef __cpp_lib_source_location
#  include <source_location>
#endif

namespace trieste
{
  namespace detail
  {
    /*
     * Type used to track where a particular value was constructed in the
     * source. This aids in debugging and error reporting.
     */
#ifdef __cpp_lib_source_location
    struct DebugLocation
    {
      std::source_location location;

      DebugLocation(std::source_location l = std::source_location::current())
      : location(l)
      {}
    };
#else
    struct DebugLocation
    {
      DebugLocation() {}
    };
#endif

    template<typename T>
    struct Located
    {
      T value;
      DebugLocation location;

      Located(T t, DebugLocation l = {})
      : value(t), location(l)
      {}
    };
  }
}