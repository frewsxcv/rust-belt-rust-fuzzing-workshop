/* Copyright (c) 2015, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

// rustc always links with the non-debug runtime, but when _DEBUG is defined
// MSVC's C++ standard library expects to be linked to the debug runtime.
#if defined(_DEBUG)
#undef _DEBUG
#endif

#include "file_test.h"

#include <memory>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>


FileTest::FileTest(const char *path)
  : file_(nullptr),
    line_(0),
    start_line_(0)
{
  file_ = fopen(path, "r");
  if (file_ == nullptr) {
    fprintf(stderr, "Could not open file %s: %s.\n", path, strerror(errno));
  }
}

FileTest::~FileTest() {
  if (file_ != nullptr) {
    fclose(file_);
  }
}

// FindDelimiter returns a pointer to the first '=' or ':' in |str| or nullptr
// if there is none.
static const char *FindDelimiter(const char *str) {
  while (*str) {
    if (*str == ':' || *str == '=') {
      return str;
    }
    str++;
  }
  return nullptr;
}

// StripSpace returns a string containing up to |len| characters from |str| with
// leading and trailing whitespace removed.
static std::string StripSpace(const char *str, size_t len) {
  // Remove leading space.
  while (len > 0 && isspace(*str)) {
    str++;
    len--;
  }
  while (len > 0 && isspace(str[len-1])) {
    len--;
  }
  return std::string(str, len);
}

FileTest::ReadResult FileTest::ReadNext() {
  // If the previous test had unused attributes, it is an error.
  if (!unused_attributes_.empty()) {
    for (const std::string &key : unused_attributes_) {
      PrintLine("Unused attribute: ", key.c_str());
    }
    return kReadError;
  }

  ClearTest();

  static const size_t kBufLen = 64 + 8192*2;
  std::unique_ptr<char[]> buf(new char[kBufLen]);

  while (true) {
    // Read the next line.
    if (fgets(buf.get(), kBufLen, file_) == nullptr) {
      if (feof(file_)) {
        // EOF is a valid terminator for a test.
        return start_line_ > 0 ? kReadSuccess : kReadEOF;
      }
      fprintf(stderr, "Error reading from input.\n");
      return kReadError;
    }

    line_++;
    size_t len = strlen(buf.get());
    // Check for truncation.
    if (len > 0 && buf[len - 1] != '\n' && !feof(file_)) {
      fprintf(stderr, "Line %u too long.\n", line_);
      return kReadError;
    }

    if (buf[0] == '\n' || buf[0] == '\0') {
      // Empty lines delimit tests.
      if (start_line_ > 0) {
        return kReadSuccess;
      }
    } else if (buf[0] != '#') {  // Comment lines are ignored.
      // Parse the line as an attribute.
      const char *delimiter = FindDelimiter(buf.get());
      if (delimiter == nullptr) {
        fprintf(stderr, "Line %u: Could not parse attribute.\n", line_);
        return kReadError;
      }
      std::string key = StripSpace(buf.get(), delimiter - buf.get());
      std::string value = StripSpace(delimiter + 1,
                                     buf.get() + len - delimiter - 1);

      unused_attributes_.insert(key);
      attributes_[key] = value;
      if (start_line_ == 0) {
        // This is the start of a test.
        type_ = key;
        parameter_ = value;
        start_line_ = line_;
      }
    }
  }
}

void FileTest::PrintLine(const char *p1, const char *p2, const char *p3) {
  fprintf(stderr, "Line %u: %s%s%s\n", start_line_, p1, p2, p3);
}

const std::string &FileTest::GetType() {
  OnKeyUsed(type_);
  return type_;
}

bool FileTest::GetAttribute(std::string *out_value, const std::string &key) {
  OnKeyUsed(key);
  auto iter = attributes_.find(key);
  if (iter == attributes_.end()) {
    PrintLine("Missing attribute '", key.c_str(), "'.");
    return false;
  }
  *out_value = iter->second;
  return true;
}

static bool FromHexDigit(uint8_t *out, char c) {
  if ('0' <= c && c <= '9') {
    *out = c - '0';
    return true;
  }
  if ('a' <= c && c <= 'f') {
    *out = c - 'a' + 10;
    return true;
  }
  if ('A' <= c && c <= 'F') {
    *out = c - 'A' + 10;
    return true;
  }
  return false;
}

bool FileTest::GetBytesOrDefault(std::vector<uint8_t> *out, bool *is_default,
                                 const std::string &key) {
  std::string value;
  if (!GetAttribute(&value, key)) {
    return false;
  }

  *is_default = value == "DEFAULT";
  if (*is_default) {
    return true;
  }

  if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"') {
    out->assign(value.begin() + 1, value.end() - 1);
    return true;
  }

  if (value.size() % 2 != 0) {
    PrintLine("Error decoding value: ", value.c_str());
    return false;
  }
  out->reserve(value.size() / 2);
  for (size_t i = 0; i < value.size(); i += 2) {
    uint8_t hi, lo;
    if (!FromHexDigit(&hi, value[i]) || !FromHexDigit(&lo, value[i+1])) {
      PrintLine("Error decoding value: ", value.c_str());
      return false;
    }
    out->push_back((hi << 4) | lo);
  }
  return true;
}

bool FileTest::GetBytes(std::vector<uint8_t> *out, const std::string &key) {
  bool is_default;
  if (!GetBytesOrDefault(out, &is_default, key)) {
    return false;
  }
  if (is_default) {
    return false;
  }
  return true;
}

static std::string EncodeHex(const uint8_t *in, size_t in_len) {
  static const char kHexDigits[] = "0123456789abcdef";
  std::string ret;
  ret.reserve(in_len * 2);
  for (size_t i = 0; i < in_len; i++) {
    ret += kHexDigits[in[i] >> 4];
    ret += kHexDigits[in[i] & 0xf];
  }
  return ret;
}

bool FileTest::ExpectBytesEqual(const uint8_t *expected, size_t expected_len,
                                const uint8_t *actual, size_t actual_len) {
  if (expected_len == actual_len &&
      memcmp(expected, actual, expected_len) == 0) {
    return true;
  }

  std::string expected_hex = EncodeHex(expected, expected_len);
  std::string actual_hex = EncodeHex(actual, actual_len);
  PrintLine("Expected: ", expected_hex.c_str());
  PrintLine("Actual:   ", actual_hex.c_str());
  return false;
}

void FileTest::ClearTest() {
  start_line_ = 0;
  type_.clear();
  parameter_.clear();
  attributes_.clear();
  unused_attributes_.clear();
}

void FileTest::OnKeyUsed(const std::string &key) {
  unused_attributes_.erase(key);
}

int FileTestMain(bool (*run_test)(FileTest *t, void *arg), void *arg,
                 const char *path) {
  FileTest t(path);
  if (!t.is_open()) {
    return 1;
  }

  bool failed = false;
  while (true) {
    FileTest::ReadResult ret = t.ReadNext();
    if (ret == FileTest::kReadError) {
      return 1;
    } else if (ret == FileTest::kReadEOF) {
      break;
    }

    bool result = run_test(&t, arg);
    if (!result) {
      // In case the test itself doesn't print output, print something so the
      // line number is reported.
      t.PrintLine("Test failed");
      failed = true;
      continue;
    }
  }

  if (failed) {
    return 1;
  }

  return 0;
}
