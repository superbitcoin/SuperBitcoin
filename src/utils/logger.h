#pragma once

#include <cstring>
#include <log4cpp/Category.hh>
#include "tinyformat.h"

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

    #define REDIRECT_SBTC_LOGGER(c) static struct _rl_{_rl_(){s_logger = &LogCategory::getInstance(#c);}}_rlobj

    #define SET_TEMP_LOG_CATEGORY(c) _TempLogCategorySetter _tmp_##c(#c)

    #ifdef LOG_FILE_LINE_INFO
    # define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1) : __FILE__)
    # define Logging(prior, fmt, a...) s_logger->log(prior, tinyformat::format(fmt, ##a) + tinyformat::format(" %s(%d)", __FILENAME__, __LINE__))
    # define LoggingLevel(level, fmt, a...) s_logger->level(tinyformat::format(fmt, ##a) + tinyformat::format(" %s(%d)", __FILENAME__, __LINE__))
    # define LogStream(prior) _LogStream(prior, __FILENAME__, __LINE__)
    # define LogRetV(prior, fmt, a...) _LogFormatRetV<prior>(__FILENAME__, __LINE__, fmt, ##a)
    #else
    # define Logging(prior, fmt, a...) s_logger->log(prior, tinyformat::format(fmt, ##a))
    # define LoggingLevel(level, fmt, a...) s_logger->level(tinyformat::format(fmt, ##a))
    # define LogStream(prior) _LogStream(prior)
    # define LogRetV(prior, fmt, a...) _LogFormatRetV<prior>(nullptr, 0, fmt, ##a)
    #endif

    DECLARE_SBTC_LOGGER(CID_APP);

#else

    #define DECLARE_SBTC_LOGGER(c)
    #define REDIRECT_SBTC_LOGGER(c)
    #define SET_TEMP_LOG_CATEGORY(c)
    #define Logging(level, fmt, a...)
    #define LoggingLevel(level, fmt, a...)
    #define LogStream(prior) _LogStream(prior)
    #define LogRetV(prior, fmt, a...) (prior > LogPriority::ERROR)

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
// TODO: e.g. ELogStream() << "This is " << 10 << " line log.";
#define NLogStream() LogStream(LogPriority::NOTICE)
#define ELogStream() LogStream(LogPriority::ERROR)
#define ILogStream() LogStream(LogPriority::INFO)
#define WLogStream() LogStream(LogPriority::WARN)
#define FLogStream() LogStream(LogPriority::FATAL)
#define DLogStream() LogStream(LogPriority::DEBUG)


// TODO: some log defines with return value.
// TODO: e.g. return rLogError("This is %d line log.", 10);
#define rLogNotice(fmt, a...) LogRetV(LogPriority::NOTICE, fmt, ##a)
#define rLogError(fmt, a...)  LogRetV(LogPriority::ERROR, fmt, ##a)
#define rLogFatal(fmt, a...)  LogRetV(LogPriority::FATAL, fmt, ##a)


class _LogStream : public std::ostringstream
{
public:
#ifndef  ENABLE_LOGGING
    _LogStream(int) {}
#else
    _LogStream(int prior, const char* filename = nullptr, int line = 0) : _prior(prior), _line(line), _filename(filename) {}
    ~_LogStream()
    {
        if (!str().empty())
        {
#ifdef LOG_FILE_LINE_INFO
            *this << ' ' << _filename << '(' << _line << ')';
#endif
            s_logger->log(_prior, str());
        }
    }
private:
    int _prior;
    int _line;
    const char* _filename;
#endif
};


template<int prior, typename... TArgs>
inline bool _LogFormatRetV(const char* filename, int line, const char* fmt, TArgs&&... args)
{
    s_logger->log(prior, tinyformat::format(fmt, args...)
#ifdef LOG_FILE_LINE_INFO
                       + tinyformat::format(" %s(%d)", filename, line)
#endif
    );
    return prior > LogPriority::ERROR;
}

template<int prior, typename... TArgs>
inline bool _LogFormatRetV(const char* filename, int line, const std::string& fmt, TArgs&&... args)
{
    return _LogFormatRetV<prior>(filename, line, fmt.c_str(), args...);
}


class _TempLogCategorySetter
{
public:
    _TempLogCategorySetter(const char* category)
    {
        _old_logger = s_logger;
        s_logger = &LogCategory::getInstance(category);
    }

    ~_TempLogCategorySetter()
    {
        s_logger = _old_logger; // restore
    }

private:
    LogCategory* _old_logger;
};