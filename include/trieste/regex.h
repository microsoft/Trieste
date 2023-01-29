// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "source.h"

#include <re2/re2.h>

namespace trieste
{
  class REMatch
  {
    friend class REIterator;

  private:
    std::vector<re2::StringPiece> match;
    std::vector<Location> locations;
    size_t matches = 0;

  public:
    REMatch(size_t max_capture = 0)
    {
      match.resize(max_capture + 1);
      locations.resize(max_capture + 1);
    }

    const Location& at(size_t index = 0) const
    {
      if (index >= matches)
        return locations.at(0);

      return locations.at(index);
    }

    template<typename T>
    T parse(size_t index = 0) const
    {
      if (index >= matches)
        return T();

      T t;
      RE2::Arg arg(&t);
      auto& m = match.at(index);
      arg.Parse(m.data(), m.size());
      return t;
    }
  };

  class REIterator
  {
  private:
    Source source;
    re2::StringPiece sp;

  public:
    REIterator(Source source) : source(source), sp(source->view()) {}

    bool empty()
    {
      return sp.empty();
    }

    bool consume(const RE2& regex, REMatch& m)
    {
      m.matches = regex.NumberOfCapturingGroups() + 1;

      if (m.match.size() < m.matches)
        m.match.resize(m.matches);

      if (m.locations.size() < m.matches)
        m.locations.resize(m.matches);

      auto matched = regex.Match(
        sp,
        0,
        sp.length(),
        re2::RE2::ANCHOR_START,
        m.match.data(),
        static_cast<int>(m.matches));

      if (!matched || (m.match.at(0).size() == 0))
      {
        return false;
      }

      for (size_t i = 0; i < m.matches; i++)
      {
        m.locations[i] = {
          source,
          static_cast<size_t>(m.match.at(i).data() - source->view().data()),
          m.match.at(i).size()};
      }

      sp.remove_prefix(m.match.at(0).size());
      return true;
    }

    Location current() const
    {
      return {
        source, static_cast<size_t>(sp.data() - source->view().data()), 1};
    }

    void skip(size_t count = 1)
    {
      sp.remove_prefix(count);
    }
  };
}
