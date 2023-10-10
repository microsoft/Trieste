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

    SNMALLOC_FAST_PATH Log(detail::LogLevel level)
    {
      if (SNMALLOC_UNLIKELY(level <= detail::report_level))
        start();
    }

    // Query if this log should be printed. Needed for extending the
    // pipe operator to allow the customisation using ADL.
    bool should_print()
    {
      return print;
    }

    SNMALLOC_FAST_PATH typename std::stringstream& get_stringstream()
    {
      if (should_print())
        return strstream.get();

      throw std::runtime_error("Log should not be printed! Use should_print()");
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

  namespace detail
  {
    template<detail::LogLevel L>
    class LogImpl : public Log
    {
    public:
      static constexpr detail::LogLevel level = L;

      static bool active()
      {
        return L <= detail::report_level;
      }

      SNMALLOC_FAST_PATH LogImpl() : Log(L) {}
    };
  } // namespace detail

  using None = detail::LogImpl<detail::LogLevel::None>;
  using Output = detail::LogImpl<detail::LogLevel::Output>;
  using Error = detail::LogImpl<detail::LogLevel::Error>;
  using Warn = detail::LogImpl<detail::LogLevel::Warn>;
  using Info = detail::LogImpl<detail::LogLevel::Info>;
  using Debug = detail::LogImpl<detail::LogLevel::Debug>;
  using Trace = detail::LogImpl<detail::LogLevel::Trace>;

  // Append to the string stream.
  template<typename T>
  inline SNMALLOC_SLOW_PATH void append(Log& self, T&& t)
  {
    self.get_stringstream() << std::forward<T>(t);
  }

  // Pipe operators defined in the global namespace to allow for ADL.
  template<typename T>
  SNMALLOC_FAST_PATH_INLINE Log& operator<<(Log& self, T&& t)
  {
    if (SNMALLOC_UNLIKELY(self.should_print()))
      append(self, std::forward<T>(t));
    return self;
  }

  // Pipe operators defined in the global namespace to allow for ADL.
  template<typename T>
  SNMALLOC_FAST_PATH_INLINE Log& operator<<(Log&& self, T&& t)
  {
    if (SNMALLOC_UNLIKELY(self.should_print()))
      append(self, std::forward<T>(t));
    return self;
  }

  /**
   * @brief Used to delay printing of a value until if is known if printing
   * should occur.
   *
   * @tparam T - Type of the value.
   * @tparam void (f)(Log&, const T&) - Static printing function.
   */
  template<typename T, void(f)(Log&, const T&)>
  struct Lazy
  {
    const T& t;

    SNMALLOC_FAST_PATH Lazy(const T& t) : t(t) {}
  };

  template<typename T, void(f)(Log&, const T&)>
  inline SNMALLOC_SLOW_PATH void append(Log& self, Lazy<T, f>&& p)
  {
    f(self, p.t);
  }

  /**
   * @brief Used to output a separator between values.
   *
   * Use it as follows:
   *
   *  {
   *    logging::Sep sep{", "};
   *    logging::Error log{};
   *    for (size_t i = 0; i < 10; i++)
   *      log << sep << i;
   *  }
   *
   * The first time it is output it does nothing, but after that it output the
   * separator.  This results in the message:
   * 
   *   `0, 1, 2, 3, 4, 5, 6, 7, 8, 9`
   */
  struct Sep
  {
    std::string sep;
    bool first;

    SNMALLOC_FAST_PATH Sep(std::string sep) : sep(sep), first(true) {}
  };

  inline SNMALLOC_SLOW_PATH void append(Log& append, Sep& sep)
  {
    if (sep.first)
      sep.first = false;
    else
      append << sep.sep;
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
    if (SNMALLOC_UNLIKELY(trieste::logging::param::active())) \
    trieste::logging::param()
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
} // namespace trieste::logging
