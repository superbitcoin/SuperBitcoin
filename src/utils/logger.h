#pragma once

#include <log4cpp/Category.hh>
#include "tinyformat.h"
#include "interface/componentid.h"

typedef log4cpp::Category LogCategory;
typedef log4cpp::Priority LogPriority;

//TODO: log priority enum
//using LogPriority::EMERG;
//using LogPriority::FATAL;
//using LogPriority::ALERT;
//using LogPriority::CRIT;
//using LogPriority::ERROR;
//using LogPriority::WARN;
//using LogPriority::NOTICE;
//using LogPriority::INFO;
//using LogPriority::DEBUG;

#define ENABLE_LOGGING
#define LOG_FILE_LINE_INFO

#ifdef  ENABLE_LOGGING

    #define DECLARE_SBTC_LOGGER(c) static LogCategory* s_logger = &LogCategory::getInstance(#c)

    #define REDIRECT_SBTC_LOGGER(c) struct _rl_{_rl_(){s_logger = &LogCategory::getInstance(#c);}}_rlobj

    #ifdef LOG_FILE_LINE_INFO
    # define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
    # define Logging(prior, fmt, a...) s_logger->log(prior, tinyformat::format(fmt, ##a) + tinyformat::format(" %s(%d)", __FILENAME__, __LINE__))
    # define LoggingLevel(level, fmt, a...) s_logger->level(tinyformat::format(fmt, ##a) + tinyformat::format(" %s(%d)", __FILENAME__, __LINE__))
    # define LogStream(prior) _LogStream(prior, __FILENAME__, __LINE__)
    #else
    # define Logging(prior, fmt, a...) s_logger->log(prior, tinyformat::format(fmt, ##a))
    # define LoggingLevel(level, fmt, a...) s_logger->level(tinyformat::format(fmt, ##a))
    # define LogStream(prior) _LogStream(prior)
    #endif

    DECLARE_SBTC_LOGGER(CID_APP);

#else

    #define DECLARE_SBTC_LOGGER(c)
    #define REDIRECT_SBTC_LOGGER(c)
    #define Logging(level, fmt, a...)
    #define LoggingLevel(level, fmt, a...)
    #define LogStream(prior) _LogStream(prior)

#endif


// TODO: formatted logging defines.
// TODO: e.g. ELogFormat("This is %d line log.", 10);
#define NLogFormat(fmt, a...) LoggingLevel(notice, fmt, ##a)
#define ELogFormat(fmt, a...) LoggingLevel(error,  fmt, ##a)
#define ILogFormat(fmt, a...) LoggingLevel(info,   fmt, ##a)
#define WLogFormat(fmt, a...) LoggingLevel(warn,   fmt, ##a)
#define FLogFormat(fmt, a...) LoggingLevel(fatal,  fmt, ##a)
#define DLogFormat(fmt, a...) LoggingLevel(debug,  fmt, ##a)
#define LogFormat(prior, fmt, a...) Logging(prior, fmt, ##a)


// TODO: stream logging defines
// TODO: e.g. ELogStream() << "This is " << 10 << "line log."
#define NLogStream() LogStream(LogPriority::NOTICE)
#define ELogStream() LogStream(LogPriority::ERROR)
#define ILogStream() LogStream(LogPriority::INFO)
#define WLogStream() LogStream(LogPriority::WARN)
#define FLogStream() LogStream(LogPriority::FATAL)
#define DLogStream() LogStream(LogPriority::DEBUG)



class _LogStream : public std::ostringstream
{
public:
    _LogStream(int prior, const char* filename = nullptr, int line = 0) : _prior(prior), _line(line), _filename(filename) {}

#ifdef  ENABLE_LOGGING
    ~_LogStream()
    {
        if (!str().empty())
        {
            if (_filename)
            {
                *this << ' ' << _filename << '(' << _line << ')';
            }
            s_logger->log(_prior, str());
        }
    }
#endif

private:
    int _prior;
    int _line;
    const char* _filename;
};
