#pragma once

#include <stdio.h>

#include <string>

#include "noncopyable.h"

// 文件追加写封装，自带 64KB 用户态缓冲以减少 write 系统调用
class AppendFile : noncopyable
{
public:
	explicit AppendFile(const char *filename);
	~AppendFile();

	void append(const char *logline, const size_t len);
	void flush();

private:
	size_t write(const char *logline, size_t len);

	FILE *fp_;
	char buffer_[64 * 1024];
};