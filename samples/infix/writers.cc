#include "internal.h"

namespace
{
  using namespace trieste;
  using namespace infix;

  // clang-format off
  const auto wf_to_file =
    infix::wf
    | (Top <<= File)
    | (File <<= Path * Calculation)
    ;
  // clang-format on

  PassDef to_file(const std::filesystem::path& path)
  {
    return {
      "to_file",
      wf_to_file,
      dir::bottomup | dir::once,
      {
        In(Top) * T(Calculation)[Calculation] >>
          [path](Match& _) {
            return File << (Path ^ path.string()) << _(Calculation);
          },
      }};
  }

  bool write_infix(std::ostream& os, Node node)
  {
    if (node == Expression)
    {
      node = node->front();
    }

    if (node == Ref)
    {
      node = node->front();
    }

    if (node->in({Int, Float, String, Ident}))
    {
      os << node->location().view();
      return false;
    }

    if (node->in({Add, Subtract, Multiply, Divide}))
    {
      os << "(";
      if (write_infix(os, node->front()))
      {
        return true;
      }
      os << " " << node->location().view() << " ";
      if (write_infix(os, node->back()))
      {
        return true;
      }
      os << ")";

      return false;
    }

    if (node == Assign)
    {
      if (write_infix(os, node->front()))
      {
        return true;
      }

      os << " = ";
      if (write_infix(os, node->back()))
      {
        return true;
      }

      os << ";" << std::endl;

      return false;
    }

    if (node == Output)
    {
      os << "print ";
      if (write_infix(os, node->front()))
      {
        return true;
      }

      os << " ";
      if (write_infix(os, node->back()))
      {
        return true;
      }

      os << ";" << std::endl;
      return false;
    }

    if (node == Calculation)
    {
      for (const Node& step : *node)
      {
        if (write_infix(os, step))
        {
          return true;
        }
      }

      return false;
    }

    os << "<error: unknown node type " << node->type() << ">" << std::endl;
    return true;
  }

  bool write_postfix(std::ostream& os, Node node)
  {
    if (node == Expression)
    {
      node = node->front();
    }

    if (node == Ref)
    {
      node = node->front();
    }

    if (node->in({Int, Float, String, Ident}))
    {
      os << node->location().view();
      return false;
    }

    if (node->in({Add, Subtract, Multiply, Divide}))
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " " << node->location().view();

      return false;
    }

    if (node == Assign)
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " =" << std::endl;

      return false;
    }

    if (node == Output)
    {
      if (write_postfix(os, node->front()))
      {
        return true;
      }

      os << " ";

      if (write_postfix(os, node->back()))
      {
        return true;
      }

      os << " print" << std::endl;
      return false;
    }

    if (node == Calculation)
    {
      for (const Node& step : *node)
      {
        if (write_postfix(os, step))
        {
          return true;
        }
      }

      return false;
    }

    os << "<error: unknown node type " << node->type() << ">" << std::endl;
    return true;
  }
}

namespace infix
{
  Writer writer(const std::filesystem::path& path)
  {
    return {
      "infix", {to_file(path)}, infix::wf, [](std::ostream& os, Node contents) {
        return write_infix(os, contents);
      }};
  }

  Writer postfix_writer(const std::filesystem::path& path)
  {
    return {
      "postfix",
      {to_file(path)},
      infix::wf,
      [](std::ostream& os, Node contents) {
        return write_postfix(os, contents);
      }};
  }
}
