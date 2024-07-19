// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace trieste
{
  class SourceDef;
  struct Location;
  class NodeDef;
  using Source = std::shared_ptr<SourceDef>;
  using Node = std::shared_ptr<NodeDef>;

  class SourceDef
  {
  private:
    std::string origin_;
    std::string contents;
    std::vector<std::pair<size_t, size_t>> lines;

  public:
    static Source load(const std::filesystem::path& file)
    {
      std::ifstream f(file, std::ios::binary | std::ios::in | std::ios::ate);

      if (!f)
        return {};

      auto size = f.tellg();
      f.seekg(0, std::ios::beg);

      auto source = std::make_shared<SourceDef>();
      source->origin_ = std::filesystem::relative(file).string();
      source->contents.resize(static_cast<std::size_t>(size));
      f.read(&source->contents[0], size);

      if (!f)
        return {};

      source->find_lines();
      return source;
    }

    static Source synthetic(const std::string& contents)
    {
      auto source = std::make_shared<SourceDef>();
      source->contents = contents;
      source->find_lines();
      return source;
    }

    const std::string& origin() const
    {
      return origin_;
    }

    std::string_view view() const
    {
      return std::string_view(contents);
    }

    std::pair<size_t, size_t> linecol(size_t pos) const
    {
      // If we have no lines, contents is an empty string.
      // Realistically this case will only happen for pos == 0.
      if (lines.empty())
      {
        return {0, pos};
      }

      // Find the first line that begins _after_ pos, and then backtrack one
      // element if we need to. We can't write this directly because "last
      // element for which condition was false" is not an stdlib primitive.
      auto it = std::lower_bound(
        lines.begin(),
        lines.end(),
        pos,
        [](std::pair<size_t, size_t> elem, size_t pos_) {
          return elem.first <= pos_;
        });
      // If we're at the beginning already, or we couldn't find a line after
      // pos, then our current line should be fine. If lines is constructed
      // correctly, it is on either the only line or the last line (and pos is
      // on that line or beyond all lines).
      if (it == lines.end() || (it != lines.begin() && it->first > pos))
      {
        --it;
      }

      size_t line = std::distance(lines.begin(), it);
      size_t col = pos - it->first;

      return {line, col};
    }

    std::pair<size_t, size_t> linepos(size_t line) const
    {
      // Special case: for out of range lines, index them at the end of our
      // string and give them length 0. This will cause minimally-ugly
      // misbehavior if there's an out of range error. Also, this gracefully
      // handles the technically out of range situation where we ask for line 0
      // of an empty string, which is what linecol(0) will give its caller in
      // that case.

      // Change note: this used to return std::string::npos when line was out of
      // range by more than 1, but callers weren't checking for it so that case
      // just caused things like .subtr(max_long) in practice. Best return an
      // empty line with max idx, which will cause blank outputs. Otherwise,
      // actually assert(line <= lines.size()) to crash here and not half way
      // down the calling function.
      if (line >= lines.size())
      {
        return {contents.size(), 0};
      }

      return lines[line]; // already in {start, size} format
    }

  private:
    void find_lines()
    {
      using namespace std::string_view_literals;
      // Find the lines (accounting for cross-platform line ending issues).
      // We store the size of the line to support linepos(line), so people can
      // print exactly the line and not fragments of \r\n.
      size_t cursor = 0;
      auto try_match = [&](std::string_view part) -> bool {
        if (std::string_view(contents).substr(cursor, part.size()) == part)
        {
          cursor += part.size();
          return true;
        }
        else
        {
          return false;
        }
      };

      size_t line_start = 0;
      while (cursor < contents.size())
      {
        size_t last_pos = cursor;
        if (try_match("\r\n"sv) || try_match("\n"sv) || try_match("\r"sv))
        {
          lines.emplace_back(line_start, last_pos - line_start);
          line_start = cursor;
        }
        else
        {
          ++cursor;
        }
      }

      // Trailing content without a new line at the end
      if (line_start < contents.size())
      {
        lines.emplace_back(line_start, contents.size() - line_start);
      }
    }
  };

  struct Location
  {
    Source source;
    size_t pos;
    size_t len;

    Location() = default;

    Location(Source source_, size_t pos_, size_t len_)
    : source(source_), pos(pos_), len(len_)
    {}

    Location(const std::string& s)
    : source(SourceDef::synthetic(s)), pos(0), len(s.size())
    {}

    std::string_view view() const
    {
      if (!source)
        return {};

      return source->view().substr(pos, len);
    }

    std::string origin_linecol() const
    {
      std::stringstream ss;

      if (source && !source->origin().empty())
      {
        auto [line, col] = linecol();
        ss << source->origin() << ":" << (line + 1) << ":" << (col + 1);
      }

      return ss.str();
    }

    std::string str() const
    {
      if (!source)
        return {};

      std::stringstream ss;
      auto [line, col] = linecol();
      auto [linepos, linelen] = source->linepos(line);

      if (view().find_first_of('\n') != std::string::npos)
      {
        auto cover = std::min(linelen - col, len);
        std::fill_n(std::ostream_iterator<char>(ss), col, ' ');
        std::fill_n(std::ostream_iterator<char>(ss), cover, '~');

        auto [line2, col2] = source->linecol(pos + len);
        auto [linepos2, linelen2] = source->linepos(line2);
        linelen = (linepos2 - linepos) + linelen2;

        ss << std::endl << source->view().substr(linepos, linelen) << std::endl;

        std::fill_n(std::ostream_iterator<char>(ss), col2, '~');
        ss << std::endl;
      }
      else
      {
        ss << source->view().substr(linepos, linelen) << std::endl;
        std::fill_n(std::ostream_iterator<char>(ss), col, ' ');
        std::fill_n(std::ostream_iterator<char>(ss), len, '~');
        ss << std::endl;
      }

      return ss.str();
    }

    std::pair<size_t, size_t> linecol() const
    {
      if (!source)
        return {0, 0};

      return source->linecol(pos);
    }

    Location operator*(const Location& that) const
    {
      if (source != that.source)
        return *this;

      auto lo = std::min(pos, that.pos);
      auto hi = std::max(pos + len, that.pos + that.len);
      return {source, lo, hi - lo};
    }

    Location& operator*=(const Location& that)
    {
      *this = *this * that;
      return *this;
    }

    bool operator==(const Location& that) const
    {
      return view() == that.view();
    }

    bool operator!=(const Location& that) const
    {
      return !(*this == that);
    }

    bool operator<(const Location& that) const
    {
      return view() < that.view();
    }

    bool operator<=(const Location& that) const
    {
      return (*this < that) || (*this == that);
    }

    bool operator>(const Location& that) const
    {
      return !(*this <= that);
    }

    bool operator>=(const Location& that) const
    {
      return !(*this < that);
    }
  };
}
