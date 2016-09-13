// PyV8.cpp : Defines the entry point for the DLL application.
//
#include "libplatform/libplatform.h"

#include <stdexcept>
#include <iostream>
#include <sstream>

#include <Python.h>
#include <frameobject.h>

#include <boost/core/null_deleter.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include <boost/log/keywords/format.hpp>
#include <boost/log/keywords/severity.hpp>
namespace keywords = boost::log::keywords;

#include <boost/log/expressions.hpp>
#include <boost/log/support/date_time.hpp>
namespace expr = boost::log::expressions;

#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
namespace sinks = boost::log::sinks;

#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/filter_parser.hpp>
#include <boost/log/utility/setup/formatter_parser.hpp>

#include "Config.h"
#include "Engine.h"
#include "Debug.h"
#include "Locker.h"
#include "Utils.h"

#ifdef SUPPORT_AST
  #include "AST.h"
#endif

severity_level g_logging_level = error;

BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_level);
BOOST_LOG_ATTRIBUTE_KEYWORD(isolate, "Isolate", const v8::Isolate *);
BOOST_LOG_ATTRIBUTE_KEYWORD(context, "Context", const v8::Context *);

typedef sinks::synchronous_sink< sinks::text_ostream_backend > text_sink;

template< typename CharT, typename TraitsT>
inline std::basic_istream< CharT, TraitsT >& operator>> (
    std::basic_istream< CharT, TraitsT >& stream, severity_level &level)
{
  std::string s;

  stream >> s;

  if (s.compare("TRACE") == 0) level = trace;
  else if (s.compare("DEBUG") == 0) level = debug;
  else if (s.compare("INFO") == 0) level = info;
  else if (s.compare("WARNING") == 0) level = warning;
  else if (s.compare("ERROR") == 0) level = error;
  else if (s.compare("FATAL") == 0) level = fatal;
  else throw std::invalid_argument(s);

  return stream;
}

template< typename CharT, typename TraitsT>
inline std::basic_ostream< CharT, TraitsT >& operator<< (
    std::basic_ostream< CharT, TraitsT >& stream, severity_level level)
{
  switch (level) {
    case trace: stream << "TRACE"; break;
    case debug: stream << "DEBUG"; break;
    case info: stream << "INFO"; break;
    case warning: stream << "WARNING"; break;
    case error: stream << "ERROR"; break;
    case fatal: stream << "FATAL"; break;
  }

  return stream;
}

static void initialize_logging() {
  const char *pyv8_log = getenv("PYV8_LOG");

  if (pyv8_log) {
    std::istringstream(pyv8_log) >> g_logging_level;
  }

  logging::register_simple_formatter_factory< severity_level, char >("Severity");
  logging::register_simple_filter_factory< severity_level >("Severity");

  boost::shared_ptr<text_sink> sink = boost::make_shared<text_sink>();

  sink->locked_backend()->add_stream(
    boost::shared_ptr<std::ostream>(&std::clog, boost::null_deleter())
  );

  sink->locked_backend()->auto_flush(true);

  logging::add_common_attributes();

  sink->set_formatter(
    expr::stream <<
      expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S") <<
      expr::if_(g_logging_level >= debug && expr::has_attr(isolate))
      [
        expr::stream << " [" << isolate << ":" << context << "]"
      ] <<
      " <" << severity << "> " << expr::message
    );

  sink->set_filter(
    severity >= boost::ref(g_logging_level)
  );

  logging::core::get()->add_sink(sink);
}

static void load_external_data(logger_t& logger) {
  PyFrameObject* frame = PyThreadState_Get()->frame;

  const char *filename = PyString_AsString(frame->f_code->co_filename);
  fs::path load_path = fs::canonical(filename, fs::current_path());

  BOOST_LOG_SEV(logger, debug) << "load ICU data from " << load_path.parent_path().c_str() << " ...";

  v8::V8::InitializeICUDefaultLocation(load_path.c_str());

  BOOST_LOG_SEV(logger, debug) << "load external snapshot from " << load_path.parent_path().c_str() << " ...";

  v8::V8::InitializeExternalStartupData(load_path.c_str());
}

BOOST_PYTHON_MODULE(_PyV8)
{
  initialize_logging();

  logger_t logger;

  load_external_data(logger);

  BOOST_LOG_SEV(logger, debug) << "initializing platform ...";

  v8::V8::InitializePlatform(v8::platform::CreateDefaultPlatform());

  BOOST_LOG_SEV(logger, debug) << "initializing V8 ...";

  v8::V8::Initialize();

  CIsolate *isolate = new CIsolate();

  isolate->Enter();

  BOOST_LOG_SEV(logger, debug) << "exposing modules ...";

  CJavascriptException::Expose();
  CWrapper::Expose();
  CContext::Expose();
#ifdef SUPPORT_AST
  CAstNode::Expose();
#endif
  CEngine::Expose();
  CDebug::Expose();
  CLocker::Expose();
}
