#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <snmalloc/ds_core/defines.h>
#include <sstream>

namespace trieste::logging
{
  namespace detail
  {
    /**
     * @brief Wrapper for delaying the C++ constructor of an object to
     * allow for a manual lifetime with placement new.
     *
     * @tparam T - The underlying type that is being wrapped.
     */
    template<typename T>
    class UnsafeInit
    {
      char data[sizeof(T)];

    public:
      template<typename... Args>
      void init(Args&&... args)
      {
        new (data) T(std::forward<Args>(args)...);
      }

      T& get()
      {
        return *reinterpret_cast<T*>(data);
      }

      void destruct()
      {
        get().~T();
      }
    };

    enum class LogLevel
    {
      None = 0,
      Error = 1,
      Output = 2,
      Warn = 3,
      Info = 4,
      Debug = 5,
      Trace = 6
    };

    // Used to set which level of message should be reported.
    inline static LogLevel report_level{LogLevel::Output};

    class Indent
    {};
    class Undent
    {};
  } // namespace detail

  static constexpr detail::Indent Indent{};
  static constexpr detail::Undent Undent{};

  template<detail::LogLevel Level>
  class Log
  {
    // Should the log actually do stuff. Decided at creation so that we can
    // fast path away from actually doing the work.
    bool print{false};

    // The number of characters to indent by.
    size_t indent_chars;

    // The string stream that we are writing to.
    // Using a local stream prevents log tearing in a concurrent setting.
    // The UnsafeInit wrapper prevents initialisation if the log is not going to
    // print.
    detail::UnsafeInit<std::stringstream> strstream;

    /**
     * The following methods
     *  - start
     *  - append
     *  - end
     *  - operation
     *  - indent
     *  - undent
     * all are the slow paths where logging is actually enabled.
     *
     * The code is structured so these should not impact perf when logging is
     * not going to occur.
     */

    SNMALLOC_SLOW_PATH void start()
    {
      strstream.init();
      indent_chars = 0;
      print = true;
      if (header_callback)
      {
        // Indent all lines after a header by 5 spaces.
        indent_chars = 5;
        // Add the header.
        header_callback(strstream.get());
      }
    }

    SNMALLOC_SLOW_PATH void end()
    {
      strstream.get() << std::endl;
      dump_callback(strstream.get());
      strstream.destruct();
    }

    SNMALLOC_SLOW_PATH void operation(std::ostream& (*f)(std::ostream&))
    {
      // Intercept std::endl and indent the next line.
      if (f == std::endl<char, std::char_traits<char>>)
        strstream.get() << std::endl << std::setw(indent_chars) << "";
      else
        strstream.get() << f;
    }

    SNMALLOC_SLOW_PATH void indent()
    {
      ++indent_chars;
      *this << std::endl;
    }

    SNMALLOC_SLOW_PATH void undent()
    {
      if (indent_chars == 0)
        throw std::runtime_error("Undent called too many times");
      --indent_chars;
      *this << std::endl;
    }

  public:
    static constexpr detail::LogLevel level{Level};

    // Used to add a header to each log message.
    // For example, this could be used to start each line with a thread
    // identifier or a timestamp.
    inline static std::function<void(std::stringstream&)> header_callback{};

    // Used to dump the string stream to a file or stdout.
    // Defaults to stdout.
    inline static std::function<void(std::stringstream&)> dump_callback{
      [](std::stringstream& s) { std::cout << s.str() << std::flush; }};

    // Delete copy constructor and assignment operator.
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    // Delete move constructor and assignment operator.
    Log(Log&&) = delete;
    Log& operator=(Log&&) = delete;

    static bool active()
    {
      return Level <= detail::report_level;
    }

    SNMALLOC_FAST_PATH Log()
    {
      if (SNMALLOC_UNLIKELY(active()))
        start();
    }

    // Query if this log should be printed. Needed for extending the
    // pipe operator to allow the customisation using ADL.
    bool should_print()
    {
      return print;
    }

    // Append to the string stream.
    template<typename T>
    SNMALLOC_SLOW_PATH void append(T&& t)
    {
      strstream.get() << std::forward<T>(t);
    }


    SNMALLOC_FAST_PATH Log& operator<<(std::ostream& (*f)(std::ostream&)) &
    {
      if (SNMALLOC_UNLIKELY(print))
        operation(f);

      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(std::ostream& (*f)(std::ostream&)) &&
    {
      if (SNMALLOC_UNLIKELY(print))
        operation(f);

      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Indent) &
    {
      if (SNMALLOC_UNLIKELY(print))
        indent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Indent) &&
    {
      if (SNMALLOC_UNLIKELY(print))
        indent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Undent) &
    {
      if (SNMALLOC_UNLIKELY(print))
        undent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Undent) &&
    {
      if (SNMALLOC_UNLIKELY(print))
        undent();
      return *this;
    }

    SNMALLOC_FAST_PATH ~Log()
    {
      if (SNMALLOC_UNLIKELY(print))
        end();
    }
  };

  using Output = Log<detail::LogLevel::Output>;
  using Error = Log<detail::LogLevel::Error>;
  using Warn = Log<detail::LogLevel::Warn>;
  using Info = Log<detail::LogLevel::Info>;
  using Debug = Log<detail::LogLevel::Debug>;
  using Trace = Log<detail::LogLevel::Trace>;

  // Pipe operators defined in the global namespace to allow for ADL.
  template<typename T, detail::LogLevel L>
  SNMALLOC_FAST_PATH_INLINE Log<L>& operator<<(Log<L>& self, T&& t)
  {
    if (SNMALLOC_UNLIKELY(self.should_print()))
      self.append(std::forward<T>(t));
    return self;
  }

  // Pipe operators defined in the global namespace to allow for ADL.
  template<typename T, detail::LogLevel L>
  SNMALLOC_FAST_PATH_INLINE Log<L>& operator<<(Log<L>&& self, T&& t)
  {
    if (SNMALLOC_UNLIKELY(self.should_print()))
      self.append(std::forward<T>(t));
    return self;
  }

#ifdef TRIESTE_EXPOSE_LOG_MACRO
// This macro is used to expose the logging to uses in a way that
// guarantees no evaluation of the pipe sequence:
//   LOG(Info) << "Hello " << "World" << fib(23);
// would not evaluate fib(23) if Info is not enabled.
// Where as the pure C++ version
//   logging::Info() << "Hello " << "World" << fib(23);
// would be required to evaluate fib(23) even if Info is not enabled.
#  define LOG(param) \
    if (SNMALLOC_UNLIKELY( \
          trieste::logging::Log< \
            trieste::logging::detail::LogLevel::param>::active())) \
    trieste::logging::Log<trieste::logging::detail::LogLevel::param>()
#endif

  /**
   * @brief Sets the level of logging that should be reported.
   *
   * @tparam L - The level of logging that should be reported.
   */
  template<typename L>
  inline void set_level()
  {
    detail::report_level = L::level;
  }
} // namespace trieste
