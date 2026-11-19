#pragma once

#include <stdio.h>
#include <string.h>

#include <string>

#include "LogStream.h"

class AsyncLogging;

enum LogLevel
{
	LOG_LEVEL_TRACE = 0,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_FATAL,
};

const char *logLevelName(LogLevel level);

// 对外接口：每次写日志构造一个临时 Logger，析构时补齐时间/级别/位置信息并投递到异步后端
class Logger
{
public:
	Logger(const char *fileName, int line, LogLevel level);
	~Logger();

	LogStream &stream() { return impl_.stream_; }

	static void setLogFileName(std::string fileName);
	static std::string getLogFileName();

	static void setLogLevel(LogLevel level) { logLevel_ = level; }
	static LogLevel getLogLevel() { return logLevel_; }

private:
	class Impl
	{
	public:
		Impl(const char *fileName, int line, LogLevel level);
		void formatPrefix();

		LogStream stream_;
		LogLevel level_;
		int line_;
		std::string basename_;
	};

	Impl impl_;

	static std::string logFileName_;
	static LogLevel logLevel_;
};

namespace detail
{
	// 用于在日志级别被过滤掉时吸收 << 运算符，避免整条日志的格式化开销
	struct LogVoidify
	{
		void operator&(LogStream &) {}
	};
} // namespace detail

#define LOG_AT(level)                                              \
	(Logger::getLogLevel() > (level))                              \
		? (void)0                                                  \
		: ::detail::LogVoidify() &                                 \
			  Logger(__FILE__, __LINE__, (level)).stream()

#define LOG_TRACE LOG_AT(LOG_LEVEL_TRACE)
#define LOG_DEBUG LOG_AT(LOG_LEVEL_DEBUG)
#define LOG_INFO LOG_AT(LOG_LEVEL_INFO)
#define LOG_WARN LOG_AT(LOG_LEVEL_WARN)
#define LOG_ERROR LOG_AT(LOG_LEVEL_ERROR)
#define LOG_FATAL LOG_AT(LOG_LEVEL_FATAL)

#define LOG LOG_INFO