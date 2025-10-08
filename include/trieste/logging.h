#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <iomanip>
#include <iostream>
#include <snmalloc/ds_core/defines.h>
#include <sstream>
#include <stdexcept>

namespace trieste::logging
{
  class Log;

  // Append to the string stream.
  template<typename T>
  void append(Log&, const T&);

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
      // Used to output a string without a header or indentation.
      String = 0,
      // Represents the status of not logging.
      None = 1,
      // Represents error messages should be printed
      Error = 2,
      // Represents error and output messages should be printed
      Output = 3,
      // Represents same as Output and warning messages should also be printed
      Warn = 4,
      // Represents same as Warn and info messages should also be printed
      Info = 5,
      // Represents same as Info and debug messages should also be
      Debug = 6,
      // Represents same as Debug and trace messages should also be printed
      Trace = 7,
      // Represents an uninitialized logging level.
      Uninitialized = 8,
    };

    // Used to set which default level of message should be reported.
    inline LogLevel default_report_level{LogLevel::Uninitialized};

    inline thread_local LogLevel report_level{LogLevel::Uninitialized};

    class Indent
    {};
    class Undent
    {};
  } // namespace detail

  constexpr detail::Indent Indent{};
  constexpr detail::Undent Undent{};

  class Log
  {
    enum class Status
    {
      Silent = 0,
      Active = 1,
      ActiveNoOutput = 2,
    };

    // Should the log actually do stuff. Decided at creation so that we can
    // fast path away from actually doing the work.
    Status print{Status::Silent};

    // The number of characters to indent by.
    size_t indent_chars;

    // The string stream that we are writing to.
    // Using a local stream prevents log tearing in a concurrent setting.
    // The UnsafeInit wrapper prevents initialisation if the log is not going to
    // print.
    detail::UnsafeInit<std::stringstream> strstream;

    friend class LocalIndent;

    static size_t& thread_local_indent()
    {
      static thread_local size_t indent = 0;
      return indent;
    }

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

    SNMALLOC_SLOW_PATH void start(detail::LogLevel level)
    {
      if (
        detail::report_level == detail::LogLevel::Uninitialized)
      {
        detail::report_level =
          detail::default_report_level == detail::LogLevel::Uninitialized ?
          detail::LogLevel::Output :
          detail::default_report_level;

        if (level > detail::report_level)
        {
          return;
        }
      }

      strstream.init();
      if (level == detail::LogLevel::String)
      {
        print = Status::ActiveNoOutput;
        indent_chars = 0;
        return;
      }
      print = Status::Active;
      indent_chars = thread_local_indent();
      if (header_callback)
      {
        // Indent all lines after a header by 5 spaces.
        indent_chars = 5 + thread_local_indent();
        // Add the header.
        header_callback(strstream.get());
      }
      else
      {
        strstream.get() << std::setw(indent_chars) << "";
      }
    }

    SNMALLOC_SLOW_PATH void end()
    {
      if (print == Status::Active)
      {
        strstream.get() << std::endl;
        dump_callback(strstream.get());
      }
      strstream.destruct();
    }

    SNMALLOC_SLOW_PATH void
    operation(decltype(std::endl<char, std::char_traits<char>>) f)
    {
      auto endl_func = std::endl<char, std::char_traits<char>>;
      // Intercept std::endl and indent the next line.
      if (f == endl_func)
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

    // Query if this log should be printed. Needed for extending the
    // pipe operator to allow the customisation using ADL.
    bool is_active()
    {
      return print != Status::Silent;
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
        start(level);
    }

    SNMALLOC_FAST_PATH typename std::stringstream& get_stringstream()
    {
      if (is_active())
        return strstream.get();

      throw std::runtime_error("Log should not be printed! Use should_print()");
    }

    SNMALLOC_FAST_PATH Log& operator<<(std::ostream& (*f)(std::ostream&)) &
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        operation(f);

      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(std::ostream& (*f)(std::ostream&)) &&
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        operation(f);

      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Indent) &
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        indent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Indent) &&
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        indent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Undent) &
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        undent();
      return *this;
    }

    SNMALLOC_FAST_PATH Log& operator<<(detail::Undent) &&
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        undent();
      return *this;
    }

    template<typename T>
    SNMALLOC_FAST_PATH_INLINE Log& operator<<(T&& t) &
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        append(*this, t);
      return *this;
    }

    template<typename T>
    SNMALLOC_FAST_PATH_INLINE Log& operator<<(T&& t) &&
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        append(*this, t);
      return *this;
    }

    SNMALLOC_FAST_PATH ~Log()
    {
      if (SNMALLOC_UNLIKELY(is_active()))
        end();
    }

    // Get the string representation from this log.
    std::string str()
    {
      std::string result = strstream.get().str();
      return result;
    }
  };

  namespace detail
  {
    /**
     * @brief Class that is used to produce a log of a specific level.
     * Log is has a dynamic level, where as LogImpl has a static level.
     * This allows type aliases that have a specific level.
     */
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

  // These types are used to both set the level of logging, and to log to
  // e.g.
  //   set_level<Error>();
  // and
  //   Error() << "Hello World";
  // The String type is a special type where the output is not set to the
  // dump_callback but can be retrieved with the str() method.
  using String = detail::LogImpl<detail::LogLevel::String>;
  using None = detail::LogImpl<detail::LogLevel::None>;
  using Error = detail::LogImpl<detail::LogLevel::Error>;
  using Output = detail::LogImpl<detail::LogLevel::Output>;
  using Warn = detail::LogImpl<detail::LogLevel::Warn>;
  using Info = detail::LogImpl<detail::LogLevel::Info>;
  using Debug = detail::LogImpl<detail::LogLevel::Debug>;
  using Trace = detail::LogImpl<detail::LogLevel::Trace>;

  // Append to the string stream.  Defined in global namespace so that it can be
  // overridden by ADL.
  template<typename T>
  inline SNMALLOC_SLOW_PATH void append(Log& self, const T& t)
  {
    self.get_stringstream() << t;
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

    SNMALLOC_FAST_PATH Lazy(const T& t_) : t(t_) {}
  };

  template<typename T, void(f)(Log&, const T&)>
  inline SNMALLOC_SLOW_PATH void append(Log& self, const Lazy<T, f>& p)
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
   * The first time it is output it does nothing, but after that it outputs the
   * separator.  This results in the message:
   *
   *   `0, 1, 2, 3, 4, 5, 6, 7, 8, 9`
   */
  struct Sep
  {
    std::string sep;
    bool first;

    SNMALLOC_FAST_PATH Sep(std::string sep_) : sep(sep_), first(true) {}
  };

  inline SNMALLOC_SLOW_PATH void append(Log& append, Sep& sep)
  {
    if (sep.first)
      sep.first = false;
    else
      append << sep.sep;
  }

  /**
   * @brief RAII class for increasing the indent level of the current thread for
   * all logging.
   */
  class LocalIndent
  {
  public:
    LocalIndent()
    {
      ++Log::thread_local_indent();
    }

    ~LocalIndent()
    {
      --Log::thread_local_indent();
    }
  };

  /**
   * @brief RAII class for setting the report level of the current thread for
   * all logging.
   */
  template<typename L>
  class LocalLogLevel
  {
  private:
    detail::LogLevel previous;

  public:
    LocalLogLevel()
    {
      previous = detail::report_level;
      detail::report_level = L::level;
    }

    ~LocalLogLevel()
    {
      detail::report_level = previous;
    }
  };

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
    if (detail::default_report_level != detail::LogLevel::Uninitialized)
    {
      throw std::runtime_error(
        "The default report level has already been initialised. Use "
        "LocalLogLevel for granular report level changes during program "
        "runtime.");
    }

    detail::default_report_level = L::level;
  }

  /**
   * @brief Set the log level from string object.  Design for use with
   * CLI11::Validator.  It will return an empty string if the log level is
   * valid, otherwise it will return an error message.
   */
  inline std::string set_log_level_from_string(const std::string& s)
  {
    std::string name;
    name.resize(s.size());
    std::transform(s.begin(), s.end(), name.begin(), ::tolower);
    if (name == "none")
      set_level<None>();
    else if (name == "error")
      set_level<Error>();
    else if (name == "output")
      set_level<Output>();
    else if (name == "warn")
      set_level<Warn>();
    else if (name == "info")
      set_level<Info>();
    else if (name == "debug")
      set_level<Debug>();
    else if (name == "trace")
      set_level<Trace>();
    else
    {
      std::stringstream ss;
      ss << "Unknown log level: " << s
         << " should be on of None, Error, Output, Warn, Info, Debug, Trace";
      return ss.str();
    }
    return {};
  }

} // namespace trieste::logging
