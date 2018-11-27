#ifndef UTILS_H
#define UTILS_H

#include <algorithm>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <Rcpp.h>
#include <boost/optional.hpp>
#include "thread.h"

// A callback for deleting objects on the main thread using later(). This is
// needed when the object is an Rcpp object or contains one, because deleting
// such objects invoke R's memory management functions.
template <typename T>
void deleter_main(void* obj) {
  ASSERT_MAIN_THREAD()
  // later() passes a void* to the callback, so we have to cast it.
  T* typed_obj = reinterpret_cast<T*>(obj);

  try {
    delete typed_obj;
  } catch (...) {}
}

// Does the same as deleter_main, but checks that it's running on the
// background thread (when thread debugging is enabled).
template <typename T>
void deleter_background(void* obj) {
  ASSERT_BACKGROUND_THREAD()
  T* typed_obj = reinterpret_cast<T*>(obj);

  try {
    delete typed_obj;
  } catch (...) {}
}

// It's not safe to call REprintf from the background thread but we need some
// way to output error messages. R CMD check does not it if the code uses the
// symbols stdout, stderr, and printf, so this function is a way to avoid
// those. It's to calling `fprintf(stderr, ...)`.
inline void err_printf(const char *fmt, ...) {
  const size_t max_size = 4096;
  char buf[max_size];

  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, max_size, fmt, args);
  va_end(args);

  if (n == -1)
    return;

  ssize_t res = write(STDERR_FILENO, buf, n);
  // This is here simply to avoid a warning about "ignoring return value" of
  // the write(), on some compilers. (Seen with gcc 4.4.7 on RHEL 6)
  res += 0;
}


// For debugging. See Makevars for information on how to enable.
void trace(const std::string& msg);

// Indexing into an empty vector causes assertion failures on some platforms
template <typename T>
T* safe_vec_addr(std::vector<T>& vec) {
  return vec.size() ? &vec[0] : NULL;
}

// Indexing into an empty vector causes assertion failures on some platforms
inline const char* safe_str_addr(const std::string& str) {
  return str.size() ? &str[0] : NULL;
}

inline std::string to_lower(const std::string& str) {
  std::string lowered = str;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), tolower);
  return lowered;
}

template <typename T>
std::string toString(T x) {
  std::stringstream ss;
  ss << x;
  return ss.str();
}

// This is used for converting an Rcpp named vector (T2) to a std::map.
template <typename T1, typename T2>
std::map<std::string, T1> toMap(T2 x) {
  ASSERT_MAIN_THREAD()

  std::map<std::string, T1> strmap;

  if (x.size() == 0) {
    return strmap;
  }

  Rcpp::CharacterVector names = x.names();
  if (names.isNULL()) {
    throw Rcpp::exception("Error converting R object to map<string, T>: vector does not have names.");
  }

  for (int i=0; i<x.size(); i++) {
    std::string name  = Rcpp::as<std::string>(names[i]);
    T1          value = Rcpp::as<T1>         (x[i]);
    if (name == "") {
      throw Rcpp::exception("Error converting R object to map<string, T>: element has empty name.");
    }

    strmap.insert(
      std::pair<std::string, T1>(name, value)
    );
  }

  return strmap;
}

// A wrapper for Rcpp::as. If the R value is NULL, this returns boost::none;
// otherwise it returns the usual value that Rcpp::as returns, wrapped in
// boost::optional<T2>.
template <typename T1, typename T2>
boost::optional<T1> optional_as(T2 value) {
  if (value.isNULL()) {
    return boost::none;
  }
  return boost::optional<T1>( Rcpp::as<T1>(value) );
}

// A wrapper for Rcpp::wrap. If the C++ value is boost::none, this returns the
// R value NULL; otherwise it returns the usual value that Rcpp::wrap returns, after
// unwrapping from the boost::optional<T>.
template <typename T>
Rcpp::RObject optional_wrap(boost::optional<T> value) {
  if (value == boost::none) {
    return R_NilValue;
  }
  return Rcpp::wrap(value.get());
}


// as() and wrap() for ResponseHeaders. Since the ResponseHeaders typedef is
// in constants.h and this file doesn't include constants.h, we'll define them
// using the actual vector type instead of the ResponseHeaders typedef.
// (constants.h doesn't include Rcpp.h so we can't define these functions
// there.)
namespace Rcpp {
  template <> inline std::vector<std::pair<std::string, std::string>> as(SEXP x) {
    ASSERT_MAIN_THREAD()
    Rcpp::CharacterVector headers(x);
    Rcpp::CharacterVector names = headers.names();

    if (names.isNULL()) {
      throw Rcpp::exception("All values must be named.");
    }

    std::vector<std::pair<std::string, std::string>> result;

    for (int i=0; i<headers.size(); i++) {
      std::string name = Rcpp::as<std::string>(names[i]);
      if (name == "") {
        throw Rcpp::exception("All values must be named.");
      }

      std::string value = Rcpp::as<std::string>(headers[i]);

      result.push_back(std::make_pair(name, value));
    }

    return result;
  }

  template <> inline SEXP wrap(const std::vector<std::pair<std::string, std::string>> &x) {
    ASSERT_MAIN_THREAD()

    std::vector<std::string> values(x.size());
    std::vector<std::string> names(x.size());

    for (int i=0; i<x.size(); i++) {
      names[i]  = x[i].first;
      values[i] = x[i].second;
    }

    Rcpp::CharacterVector result = Rcpp::wrap(values);
    result.attr("names") = Rcpp::wrap(names);

    return result;
  }
}


// Return a date string in the format required for the HTTP Date header. For
// example: "Wed, 21 Oct 2015 07:28:00 GMT"
inline std::string http_date_string(const time_t& t) {
  struct tm timeptr;
  #ifdef _WIN32
  gmtime_s(&timeptr, &t);
  #else
  gmtime_r(&t, &timeptr);
  #endif

  std::string day_name;
  switch(timeptr.tm_wday) {
    case 0:  day_name = "Sun"; break;
    case 1:  day_name = "Mon"; break;
    case 2:  day_name = "Tue"; break;
    case 3:  day_name = "Wed"; break;
    case 4:  day_name = "Thu"; break;
    case 5:  day_name = "Fri"; break;
    case 6:  day_name = "Sat"; break;
    default: day_name = "Err"; // Throw?
  }

  std::string month_name;
  switch(timeptr.tm_mon) {
    case 0:  month_name = "Jan"; break;
    case 1:  month_name = "Feb"; break;
    case 2:  month_name = "Mar"; break;
    case 3:  month_name = "Apr"; break;
    case 4:  month_name = "May"; break;
    case 5:  month_name = "Jun"; break;
    case 6:  month_name = "Jul"; break;
    case 7:  month_name = "Aug"; break;
    case 8:  month_name = "Sep"; break;
    case 9:  month_name = "Oct"; break;
    case 10: month_name = "Nov"; break;
    case 11: month_name = "Dec"; break;
    default: month_name = "Err"; // Throw?
  }

  const int maxlen = 30;
  char res[maxlen];
  snprintf(res, maxlen, "%s, %02d %s %04d %02d:%02d:%02d GMT",
    day_name.c_str(),
    timeptr.tm_mday,
    month_name.c_str(),
    timeptr.tm_year + 1900,
    timeptr.tm_hour,
    timeptr.tm_min,
    timeptr.tm_sec
  );

  return std::string(res);
}

#endif
