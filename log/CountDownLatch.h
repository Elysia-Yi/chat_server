#pragma once

#include "Condition.h"
#include "MutexLock.h"
#include "noncopyable.h"

// 倒计时门闩：用于等待后台日志线程真正启动完成
class CountDownLatch : noncopyable
{
public:
	explicit CountDownLatch(int count);
	void wait();
	void countDown();

private:
	mutable MutexLock mutex_;
	Condition condition_;
	int count_;
};