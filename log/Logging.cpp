#include "Logging.h"

#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#include <cstdlib>
#include <string>

#include "AsyncLogging.h"
#include "CurrentThread.h"

const char *logLevelName(LogLevel level)
{
	switch (level)
	{
	case LOG_LEVEL_TRACE:
		return "TRACE";
	case LOG_LEVEL_DEBUG:
		return "DEBUG";
	case LOG_LEVEL_INFO:
		return "INFO ";
	case LOG_LEVEL_WARN:
		return "WARN ";
	case LOG_LEVEL_ERROR:
		return "ERROR";
	case LOG_LEVEL_FATAL:
		return "FATAL";
	}
	return "UNKN ";
}

std::string Logger::logFileName_ = "server.log";
LogLevel Logger::logLevel_ = LOG_LEVEL_INFO;

namespace
{
	pthread_once_t g_onceControl = PTHREAD_ONCE_INIT;
	AsyncLogging *g_asyncLog = nullptr;

	// 首次写日志时惰性启动后台线程；进程正常退出时刷新并回收
	void initAsyncLog()
	{
		g_asyncLog = new AsyncLogging(Logger::getLogFileName());
		g_asyncLog->start();
		std::atexit([] {
			if (g_asyncLog)
			{
				g_asyncLog->stop();
				delete g_asyncLog;
				g_asyncLog = nullptr;
			}
		});
	}

	void output(const char *msg, int len)
	{
		pthread_once(&g_onceControl, initAsyncLog);
		if (g_asyncLog)
		{
			g_asyncLog->append(msg, len);
		}
	}
} // namespace

void Logger::setLogFileName(std::string fileName)
{
	logFileName_ = std::move(fileName);
}

std::string Logger::getLogFileName()
{
	return logFileName_;
}

Logger::Impl::Impl(const char *fileName, int line, LogLevel level)
	: stream_(),
	  level_(level),
	  line_(line),
	  basename_(fileName)
{
	// 日志里只保留文件名，避免把构建路径写进日志
	const char *slash = strrchr(fileName, '/');
	if (slash == nullptr)
	{
		slash = strrchr(fileName, '\\');
	}
	if (slash)
	{
		basename_ = slash + 1;
	}
	formatPrefix();
}

void Logger::Impl::formatPrefix()
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);

	time_t sec = tv.tv_sec;
	struct tm tm_time;
	localtime_r(&sec, &tm_time);

	char time_buf[32];
	size_t n = strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_time);

	char usec_buf[8];
	snprintf(usec_buf, sizeof(usec_buf), "%06ld", static_cast<long>(tv.tv_usec));

	stream_ << '[' << std::string(time_buf, n) << '.' << usec_buf << "] ["
			<< logLevelName(level_) << "] [" << CurrentThread::tid() << "] ";
}

Logger::Logger(const char *fileName, int line, LogLevel level)
	: impl_(fileName, line, level)
{
}

Logger::~Logger()
{
	impl_.stream_ << " -- " << impl_.basename_ << ':' << impl_.line_ << '\n';

	const LogStream::Buffer &buf(stream().buffer());
	output(buf.data(), buf.length());
}