#pragma once

#include <pthread.h>

#include "noncopyable.h"

// 互斥锁封装，配合 MutexLockGuard 使用实现 RAII
class MutexLock : noncopyable
{
public:
	MutexLock()
	{
		pthread_mutex_init(&mutex, nullptr);
	}

	~MutexLock()
	{
		pthread_mutex_destroy(&mutex);
	}

	void lock()
	{
		pthread_mutex_lock(&mutex);
	}

	void unlock()
	{
		pthread_mutex_unlock(&mutex);
	}

private:
	pthread_mutex_t mutex;

private:
	friend class Condition;
};

class MutexLockGuard : noncopyable
{
public:
	explicit MutexLockGuard(MutexLock &_mutex) : mutex(_mutex)
	{
		mutex.lock();
	}

	~MutexLockGuard()
	{
		mutex.unlock();
	}

private:
	MutexLock &mutex;
};