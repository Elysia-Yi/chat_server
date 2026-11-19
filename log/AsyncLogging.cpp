#include "AsyncLogging.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include <functional>

#include "LogFile.h"

AsyncLogging::AsyncLogging(const std::string &basename, int flushInterval)
	: flushInterval_(flushInterval),
	  running_(false),
	  basename_(basename),
	  thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
	  mutex_(),
	  cond_(mutex_),
	  currentBuffer_(new Buffer),
	  nextBuffer_(new Buffer),
	  buffers_(),
	  latch_(1)
{
	assert(!basename_.empty());
	currentBuffer_->bzero();
	nextBuffer_->bzero();
	buffers_.reserve(16);
}

AsyncLogging::~AsyncLogging()
{
	if (running_)
	{
		stop();
	}
}

void AsyncLogging::start()
{
	if (running_.exchange(true))
	{
		return;
	}
	thread_.start();
	latch_.wait();
}

void AsyncLogging::stop()
{
	if (!running_.exchange(false))
	{
		return;
	}
	cond_.notify();
	thread_.join();
}

void AsyncLogging::append(const char *logline, int len)
{
	MutexLockGuard lock(mutex_);
	if (currentBuffer_->avail() >= len)
	{
		currentBuffer_->append(logline, len);
	}
	else
	{
		// 当前缓冲写满：交给后端，换一块空缓冲继续写
		buffers_.push_back(currentBuffer_);
		currentBuffer_.reset();
		if (nextBuffer_)
		{
			currentBuffer_ = std::move(nextBuffer_);
		}
		else
		{
			// 备用缓冲也被用光，只能临时分配，保证不丢日志
			currentBuffer_.reset(new Buffer);
		}
		currentBuffer_->append(logline, len);
		cond_.notify();
	}
}

void AsyncLogging::threadFunc()
{
	assert(running_ == true);
	latch_.countDown();

	LogFile output(basename_);

	BufferPtr newBuffer1(new Buffer);
	BufferPtr newBuffer2(new Buffer);
	newBuffer1->bzero();
	newBuffer2->bzero();

	BufferVector buffersToWrite;
	buffersToWrite.reserve(16);

	while (running_)
	{
		assert(newBuffer1 && newBuffer1->length() == 0);
		assert(newBuffer2 && newBuffer2->length() == 0);
		assert(buffersToWrite.empty());

		{
			// 缓冲为空时最多等 flushInterval_ 秒，避免日志长时间滞留内存
			MutexLockGuard lock(mutex_);
			if (buffers_.empty())
			{
				cond_.waitForSeconds(flushInterval_);
			}
			buffers_.push_back(currentBuffer_);
			currentBuffer_.reset();

			currentBuffer_ = std::move(newBuffer1);
			buffersToWrite.swap(buffers_);
			if (!nextBuffer_)
			{
				nextBuffer_ = std::move(newBuffer2);
			}
		}

		assert(!buffersToWrite.empty());

		// 生产过快时主动丢弃积压，防止内存无限制膨胀
		if (buffersToWrite.size() > 25)
		{
			buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
		}

		for (size_t i = 0; i < buffersToWrite.size(); ++i)
		{
			output.append(buffersToWrite[i]->data(), buffersToWrite[i]->length());
		}

		if (buffersToWrite.size() > 2)
		{
			buffersToWrite.resize(2);
		}

		// 回收两块缓冲，留给下一轮使用，减少堆分配
		if (!newBuffer1)
		{
			assert(!buffersToWrite.empty());
			newBuffer1 = buffersToWrite.back();
			buffersToWrite.pop_back();
			newBuffer1->reset();
		}

		if (!newBuffer2)
		{
			assert(!buffersToWrite.empty());
			newBuffer2 = buffersToWrite.back();
			buffersToWrite.pop_back();
			newBuffer2->reset();
		}

		buffersToWrite.clear();
		output.flush();
	}

	output.flush();
}