#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace trieste
{
  namespace utf8
  {
    enum class DetectResult
    {
      None,
      BigEndian,
      LittleEndian
    };

    const uint8_t MaskX = 0b11000000;
    const uint8_t Mask1 = 0b10000000;
    const uint8_t Mask2 = 0b11100000;
    const uint8_t Mask3 = 0b11110000;
    const uint8_t Mask4 = 0b11111000;

    const uint8_t MarkX = 0b10000000;
    const uint8_t Mark1 = 0b00000000;
    const uint8_t Mark2 = 0b11000000;
    const uint8_t Mark3 = 0b11100000;
    const uint8_t Mark4 = 0b11110000;

    const uint8_t ValueX = 0b00111111;
    const uint8_t Value1 = 0b01111111;
    const uint8_t Value2 = 0b00011111;
    const uint8_t Value3 = 0b00001111;
    const uint8_t Value4 = 0b00000111;

    const uint32_t Max1 = 0x007F;
    const uint32_t Max2 = 0x07FF;
    const uint32_t Max3 = 0xFFFF;
    const uint32_t Max4 = 0x10FFFF;

    const std::size_t ShiftX = 6;

    const uint32_t Bad = 0xFFFD;

    inline std::ostream& write_rune(std::ostream& os, uint32_t value)
    {
      if (value <= Max1)
      {
        return os << static_cast<char>(Mark1 | value);
      }

      if (value <= Max2)
      {
        char c1 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c0 = static_cast<char>(Mark2 | value);
        return os << c0 << c1;
      }

      if (value <= Max3)
      {
        char c2 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c1 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c0 = static_cast<char>(Mark3 | value);
        return os << c0 << c1 << c2;
      }

      if (value <= Max4)
      {
        char c3 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c2 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c1 = static_cast<char>((value & ValueX) | MarkX);
        value >>= ShiftX;
        char c0 = static_cast<char>(Mark4 | value);
        return os << c0 << c1 << c2 << c3;
      }

      // bad
      return write_rune(os, Bad);
    }

    struct rune
    {
      rune(uint32_t v) : value(v) {}
      std::size_t size()
      {
        if (value <= Max1)
        {
          return 1;
        }

        if (value <= Max2)
        {
          return 2;
        }

        if (value <= Max3)
        {
          return 3;
        }

        if (value <= Max4)
        {
          return 4;
        }

        return 3;
      }

      std::string to_utf8()
      {
        std::ostringstream os;
        write_rune(os, value);
        return os.str();
      }

      uint32_t value;
    };

    using runestring = std::basic_string<char32_t>;
    using runestring_view = std::basic_string_view<char32_t>;

    inline std::pair<rune, std::string_view>
    utf8_to_rune(const std::string_view& utf8, bool unescape_unicode)
    {
      uint8_t c0 = static_cast<uint8_t>(utf8[0]);
      if (c0 == '\\' && unescape_unicode)
      {
        if (utf8.size() >= 3 && utf8[1] == 'x')
        {
          std::string hex = std::string(utf8.substr(2, 2));
          return {std::stoul(hex, nullptr, 16), utf8.substr(0, 4)};
        }

        if (utf8.size() >= 5 && utf8[1] == 'u')
        {
          std::string hex = std::string(utf8.substr(2, 4));
          return {std::stoul(hex, nullptr, 16), utf8.substr(0, 6)};
        }

        if (utf8.size() >= 9 && utf8[1] == 'U')
        {
          std::string hex = std::string(utf8.substr(2, 8));
          return {std::stoul(hex, nullptr, 16), utf8.substr(0, 10)};
        }

        return {utf8[0], utf8.substr(0, 1)};
      }

      if ((c0 & Mask1) == Mark1)
      {
        return {c0 & Value1, utf8.substr(0, 1)};
      }

      if ((c0 & Mask2) == Mark2 && utf8.size() >= 2)
      {
        uint8_t c1 = static_cast<uint8_t>(utf8[1]);
        if ((c1 & MaskX) == MarkX)
        {
          uint32_t value = c0 & Value2;
          value = (value << ShiftX) | (c1 & ValueX);
          return {value, utf8.substr(0, 2)};
        }
      }
      else if ((c0 & Mask3) == Mark3 && utf8.size() >= 3)
      {
        uint8_t c1 = static_cast<uint8_t>(utf8[1]);
        uint8_t c2 = static_cast<uint8_t>(utf8[2]);
        if ((c1 & MaskX) == MarkX && (c2 & MaskX) == MarkX)
        {
          uint32_t value = c0 & Value3;
          value = (value << ShiftX) | (c1 & ValueX);
          value = (value << ShiftX) | (c2 & ValueX);
          return {value, utf8.substr(0, 3)};
        }
      }
      else if ((c0 & Mask4) == Mark4 && utf8.size() >= 4)
      {
        uint8_t c1 = static_cast<uint8_t>(utf8[1]);
        uint8_t c2 = static_cast<uint8_t>(utf8[2]);
        uint8_t c3 = static_cast<uint8_t>(utf8[3]);
        if (
          (c1 & MaskX) == MarkX && (c2 & MaskX) == MarkX &&
          (c3 & MaskX) == MarkX)
        {
          uint32_t value = c0 & Value4;
          value = (value << ShiftX) | (c1 & ValueX);
          value = (value << ShiftX) | (c2 & ValueX);
          value = (value << ShiftX) | (c3 & ValueX);
          return {value, utf8.substr(0, 4)};
        }
      }

      // bad
      return {Bad, utf8.substr(0, 1)};
    }

    inline std::ostream& operator<<(std::ostream& os, const rune& r)
    {
      return write_rune(os, r.value);
    }

    inline std::ostream&
    operator<<(std::ostream& os, const runestring_view& runes)
    {
      for (uint32_t r : runes)
      {
        os << rune(r);
      }

      return os;
    }

    inline std::ostream& operator<<(std::ostream& os, const runestring& runes)
    {
      for (uint32_t r : runes)
      {
        os << rune(r);
      }

      return os;
    }

    inline runestring utf8_to_runestring(
      const std::string_view& input, bool unescape_hexunicode = false)
    {
      runestring runes;
      runes.reserve(input.size());
      std::size_t pos = 0;
      while (pos < input.size())
      {
        auto [r, s] = utf8_to_rune(input.substr(pos), unescape_hexunicode);
        runes.push_back(r.value);
        pos += s.size();
      }

      return runes;
    }

    inline bool detect_utf8(const std::string& contents)
    {
      auto runes = utf8_to_runestring(contents, false);
      return !std::any_of(
        runes.begin(), runes.end(), [](uint32_t r) { return r == Bad; });
    }

    inline DetectResult detect_utf16(const std::string& contents)
    {
      const std::set<uint16_t> big_endian = {
        0x002C, // comma
        0x0022, // double quote
        0x0028, // left parenthesis
        0x0029, // right parenthesis
        0x005B, // left square bracket
        0x005D, // right square bracket
        0x007B, // left curly bracket
        0x007D, // right curly bracket,
        0x003A, // colon
        0x003B, // semicolon
        0x0020, // space
        0x000A, // newline
      };

      const std::set<uint16_t> little_endian = {
        0x2C00, // comma
        0x2200, // double quote
        0x2800, // left parenthesis
        0x2900, // right parenthesis
        0x5B00, // left square bracket
        0x5D00, // right square bracket
        0x7B00, // left curly bracket
        0x7D00, // right curly bracket,
        0x3A00, // colon
        0x3B00, // semicolon
        0x2000, // space
        0x0A00, // newline
      };

      if (contents.size() % 2 != 0)
      {
        return DetectResult::None;
      }

      std::size_t le_counts = 0;
      std::size_t be_counts = 0;
      for (std::size_t i = 0; i < contents.size(); i += 2)
      {
        uint8_t b0 = static_cast<uint8_t>(contents[i]);
        uint8_t b1 = static_cast<uint8_t>(contents[i + 1]);
        uint16_t value = (b0 << 8) | b1;
        if (little_endian.find(value) != little_endian.end())
        {
          le_counts++;
        }
        if (big_endian.find(value) != big_endian.end())
        {
          be_counts++;
        }
      }

      if (le_counts > be_counts)
      {
        return DetectResult::LittleEndian;
      }
      if (be_counts > le_counts)
      {
        return DetectResult::BigEndian;
      }
      return DetectResult::None;
    }

    inline DetectResult detect_utf32(const std::string& contents)
    {
      const std::set<uint32_t> big_endian = {
        0x0000002C, // comma
        0x00000022, // double quote
        0x00000028, // left parenthesis
        0x00000029, // right parenthesis
        0x0000005B, // left square bracket
        0x0000005D, // right square bracket
        0x0000007B, // left curly bracket
        0x0000007D, // right curly bracket,
        0x0000003A, // colon
        0x0000003B, // semicolon
        0x00000020, // space
        0x0000000A, // newline
      };

      const std::set<uint32_t> little_endian = {
        0x2C000000, // comma
        0x22000000, // double quote
        0x28000000, // left parenthesis
        0x29000000, // right parenthesis
        0x5B000000, // left square bracket
        0x5D000000, // right square bracket
        0x7B000000, // left curly bracket
        0x7D000000, // right curly bracket,
        0x3A000000, // colon
        0x3B000000, // semicolon
        0x20000000, // space
        0x0A000000, // newline
      };

      if (contents.size() % 4 != 0)
      {
        return DetectResult::None;
      }

      std::size_t le_counts = 0;
      std::size_t be_counts = 0;
      for (std::size_t i = 0; i < contents.size(); i += 4)
      {
        uint8_t b0 = static_cast<uint8_t>(contents[i]);
        uint8_t b1 = static_cast<uint8_t>(contents[i + 1]);
        uint8_t b2 = static_cast<uint8_t>(contents[i + 2]);
        uint8_t b3 = static_cast<uint8_t>(contents[i + 3]);
        uint32_t value = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        if (little_endian.find(value) != little_endian.end())
        {
          le_counts++;
        }
        if (big_endian.find(value) != big_endian.end())
        {
          be_counts++;
        }
      }

      if (le_counts > be_counts)
      {
        return DetectResult::LittleEndian;
      }
      if (be_counts > le_counts)
      {
        return DetectResult::BigEndian;
      }
      return DetectResult::None;
    }

    inline std::string read_utf16_be(const std::string& contents)
    {
      std::ostringstream result;
      for (std::size_t i = 0; i < contents.size(); i += 2)
      {
        uint8_t b0 = static_cast<uint8_t>(contents[i]);
        uint8_t b1 = static_cast<uint8_t>(contents[i + 1]);
        result << rune((b0 << 8) | b1);
      }

      return result.str();
    }

    inline std::string read_utf16_le(const std::string& contents)
    {
      std::ostringstream result;
      for (std::size_t i = 0; i < contents.size(); i += 2)
      {
        uint8_t b1 = static_cast<uint8_t>(contents[i]);
        uint8_t b0 = static_cast<uint8_t>(contents[i + 1]);
        result << rune((b0 << 8) | b1);
      }

      return result.str();
    }

    inline std::string read_utf32_be(const std::string& contents)
    {
      std::ostringstream result;
      for (std::size_t i = 0; i < contents.size(); i += 4)
      {
        uint8_t b0 = static_cast<uint8_t>(contents[i]);
        uint8_t b1 = static_cast<uint8_t>(contents[i + 1]);
        uint8_t b2 = static_cast<uint8_t>(contents[i + 2]);
        uint8_t b3 = static_cast<uint8_t>(contents[i + 3]);
        result << rune((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
      }
      return result.str();
    }

    inline std::string read_utf32_le(const std::string& contents)
    {
      std::ostringstream result;
      for (std::size_t i = 0; i < contents.size(); i += 4)
      {
        uint8_t b3 = static_cast<uint8_t>(contents[i]);
        uint8_t b2 = static_cast<uint8_t>(contents[i + 1]);
        uint8_t b1 = static_cast<uint8_t>(contents[i + 2]);
        uint8_t b0 = static_cast<uint8_t>(contents[i + 3]);
        result << rune((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
      }
      return result.str();
    }

    inline std::string sanitize_utf8(const std::string& input)
    {
      std::ostringstream os;
      auto runes = utf8_to_runestring(input);
      for (uint32_t r : runes)
      {
        os << rune(r);
      }
      return os.str();
    }

    inline std::string unescape_hexunicode(const std::string_view& input)
    {
      std::ostringstream os;
      std::size_t pos = 0;
      while (pos < input.size())
      {
        if (input[pos] == '\\')
        {
          auto [r, s] = utf8_to_rune(input.substr(pos), true);
          os << r;
          pos += s.size();
        }
        else
        {
          os << input[pos];
          pos++;
        }
      }

      return os.str();
    }

    inline std::string escape_unicode(const std::string_view& input)
    {
      std::ostringstream os;
      std::size_t pos = 0;
      while (pos < input.size())
      {
        auto [r, s] = utf8_to_rune(input.substr(pos), false);
        if (r.value > 0x7FFF)
        {
          os << "\\U" << std::uppercase << std::setfill('0') << std::setw(8)
             << std::hex << r.value;
        }
        else if (r.value > 0x7F)
        {
          os << "\\u" << std::uppercase << std::setfill('0') << std::setw(4)
             << std::hex << r.value;
        }
        else
        {
          os << (char)r.value;
        }
        pos += s.size();
      }

      return os.str();
    }

    inline std::string
    read_to_end(const std::filesystem::path& path, bool autodetect = false)
    {
      std::ifstream fs(path, std::ios::binary);

      // read as raw bytes
      std::stringstream ss;
      ss << fs.rdbuf();
      std::string contents = ss.str();

      if (contents.size() >= 2)
      {
        uint8_t bom[4] = {
          static_cast<uint8_t>(contents[0]),
          static_cast<uint8_t>(contents[1]),
          0,
          0,
        };
        if (contents.size() >= 3)
        {
          bom[2] = static_cast<uint8_t>(contents[2]);
        }
        if (contents.size() >= 4)
        {
          bom[3] = static_cast<uint8_t>(contents[3]);
        }

        if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
        {
          // remove BOM
          contents = contents.substr(3);
          return contents;
        }

        if (bom[0] == 0xFF && bom[1] == 0xFE)
        {
          if (bom[2] == 0x00 && bom[3] == 0x00)
          {
            return read_utf32_le(contents.substr(4));
          }

          return read_utf16_le(contents.substr(2));
        }

        if (
          bom[0] == 0x00 && bom[1] == 0x00 && bom[2] == 0xFE && bom[3] == 0xFF)
        {
          return read_utf32_be(contents.substr(4));
        }

        if (bom[0] == 0xFE && bom[1] == 0xFF)
        {
          return read_utf16_be(contents.substr(2));
        }
      }

      if (!autodetect)
      {
        return contents;
      }

      if (detect_utf8(contents))
      {
        return sanitize_utf8(contents);
      }

      switch (detect_utf16(contents))
      {
        case DetectResult::BigEndian:
          return read_utf16_be(contents);
        case DetectResult::LittleEndian:
          return read_utf16_le(contents);
        default:
          break;
      }

      switch (detect_utf32(contents))
      {
        case DetectResult::BigEndian:
          return read_utf32_be(contents);
        case DetectResult::LittleEndian:
          return read_utf32_le(contents);
        default:
          break;
      }

      return sanitize_utf8(contents);
    }
  }
}
