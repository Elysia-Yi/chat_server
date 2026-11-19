#include "FileUtil.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

using namespace std;

AppendFile::AppendFile(const char *filename) : fp_(fopen(filename, "a"))
{
	if (fp_ == nullptr)
	{
		fprintf(stderr, "AppendFile: cannot open %s\n", filename);
		return;
	}
	setbuffer(fp_, buffer_, sizeof(buffer_));
}

AppendFile::~AppendFile()
{
	if (fp_)
	{
		fclose(fp_);
		fp_ = nullptr;
	}
}

void AppendFile::append(const char *logline, const size_t len)
{
	if (fp_ == nullptr)
	{
		return;
	}

	size_t n = this->write(logline, len);
	size_t remain = len - n;
	while (remain > 0)
	{
		size_t x = this->write(logline + n, remain);
		if (x == 0)
		{
			int err = ferror(fp_);
			if (err)
				fprintf(stderr, "AppendFile::append() failed\n");
			break;
		}
		n += x;
		remain = len - n;
	}
}

void AppendFile::flush()
{
	if (fp_)
	{
		fflush(fp_);
	}
}

size_t AppendFile::write(const char *logline, size_t len)
{
	return fwrite_unlocked(logline, 1, len, fp_);
}