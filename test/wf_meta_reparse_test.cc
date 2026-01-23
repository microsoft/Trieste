#include <initializer_list>
#include <sstream>
#include <trieste/fuzzer.h>
#include <trieste/trieste.h>
#include <trieste/wf_meta.h>

using namespace trieste;

namespace
{
  std::string node_to_string(const Node& node)
  {
    std::ostringstream out;
    out << node;
    return out.str();
  }

  Node reparse_wf(const Node& node, const std::string& ns)
  {
    return wf::meta::wf_to_node(wf::meta::node_to_wf(node), ns);
  }

  bool reparse_test(
    const std::string& name,
    const wf::Wellformed& target_wf,
    const std::string& ns)
  {
    Node out1 = wf::meta::wf_to_node(target_wf, ns);
    if (!wf::meta::wf_wf.check(out1))
    {
      std::cout << "Generated node failed meta-wf. Aborting." << std::endl;
      return false;
    }
    Node out2 = reparse_wf(out1, ns);

    auto out1_str = node_to_string(out1);
    auto out2_str = node_to_string(out2);
    if (out1_str != out2_str)
    {
      std::cout << "Mismatched reparse for " << name << ", given ns=\"" << ns
                << "\"." << std::endl
                << "First version:" << std::endl
                << out1 << std::endl
                << "Second version:" << std::endl
                << out2 << std::endl;
      return false;
    }
    return true;
  }
}

int main()
{
  std::initializer_list<
    std::tuple<std::string, const wf::Wellformed&, std::string>>
    tests = {
      {"wf_wf", wf::meta::wf_wf, "wf-meta"},
      {"wf_wf", wf::meta::wf_wf, ""},
    };

  for (const auto& [name, target_wf, ns] : tests)
  {
    std::cout << "Checking " << name << " with ns=\"" << ns << "\"..."
              << std::endl;
    if (!reparse_test(name, target_wf, ns))
    {
      return 1;
    }
  }

  std::cout << "All ok." << std::endl;

  return 0;
}
