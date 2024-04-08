#pragma once

#include "parse.h"
#include "passes.h"
#include "reader.h"
#include "rewriter.h"
#include "writer.h"

namespace CLI
{
  class App;
}

namespace trieste
{
  struct Options
  {
    virtual void configure(CLI::App&) {}
  };

  inline ProcessResult operator>>(Reader& reader, Rewriter& rewriter)
  {
    ProcessResult result = reader.read();
    if (result.ok)
    {
      return rewriter.rewrite(result.ast);
    }

    return result;
  }

  inline ProcessResult operator>>(Reader& reader, Writer& writer)
  {
    ProcessResult result = reader.read();
    if (result.ok)
    {
      return writer.write(result.ast);
    }

    return result;
  }

  inline ProcessResult
  operator>>(const ProcessResult& result, Rewriter& rewriter)
  {
    if (result.ok)
    {
      return rewriter.rewrite(result.ast);
    }

    return result;
  }

  inline ProcessResult operator>>(const ProcessResult& result, Writer& writer)
  {
    if (result.ok)
    {
      return writer.write(result.ast);
    }

    return result;
  }

  inline ProcessResult operator>>(const Node& ast, Rewriter& rewriter)
  {
    return rewriter.rewrite(ast->clone());
  }

  inline ProcessResult operator>>(const Node& ast, Writer& writer)
  {
    return writer.write(ast->clone());
  }

} // namespace trieste
