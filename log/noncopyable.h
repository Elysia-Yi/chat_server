#pragma once

// 禁止拷贝的基类
class noncopyable
{
protected:
	noncopyable() = default;
	~noncopyable() = default;

private:
	noncopyable(const noncopyable &) = delete;
	const noncopyable &operator=(const noncopyable &) = delete;
};