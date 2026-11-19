#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Condition.h"
#include "CountDownLatch.h"
#include "LogStream.h"
#include "MutexLock.h"
#include "Thread.h"
#include "noncopyable.h"

// 双缓冲异步日志后端：
//   前端线程只把日志追加到内存缓冲并立即返回；
//   后端线程在缓冲写满或等待超时后，用指针交换拿到整块数据批量落盘。
class AsyncLogging : noncopyable
{
public:
	AsyncLogging(const std::string &basename, int flushInterval = 2);
	~AsyncLogging();

	void append(const char *logline, int len);

	void start();
	void stop();

private:
	void threadFunc();

	typedef FixedBuffer<kLargeBuffer> Buffer;
	typedef std::shared_ptr<Buffer> BufferPtr;
	typedef std::vector<BufferPtr> BufferVector;

	const int flushInterval_;
	std::atomic<bool> running_;
	const std::string basename_;
	Thread thread_;
	MutexLock mutex_;
	Condition cond_;
	BufferPtr currentBuffer_;
	BufferPtr nextBuffer_;
	BufferVector buffers_;
	CountDownLatch latch_;
};