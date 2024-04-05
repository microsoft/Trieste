#include <set>
#include <string>
#include <string_view>

namespace trieste::yaml
{
  std::string
  escape_chars(const std::string_view& str, const std::set<char>& to_escape);
  std::string unescape_url_chars(const std::string_view& input);
  std::string replace_all(
    const std::string_view& v,
    const std::string_view& find,
    const std::string_view& replace);
}
