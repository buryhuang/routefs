#ifndef __UTILS_H__
#define __UTILS_H__

#include <pthread.h>
#include <time.h>

#include <string>

using std::string;

static bool abs_to_relative_path(const char * prefix, std::string abs_path, std::string& rel_path)
{
	static size_t prefix_len = 0;
	if(prefix_len == 0) {
		// Just don't want to iterate the prefix string multiple times.
		prefix_len = strlen(prefix);
	}

	size_t preLoc = abs_path.find_last_of("/");
	
	if(preLoc != string::npos
		&& (
			(preLoc + 1) == prefix_len
			|| preLoc == prefix_len
		)
		&& (
			abs_path.substr(0, preLoc+1) == prefix // root
			|| abs_path.substr(0, preLoc) == prefix // non-root, no trailing
		)
	) {
		rel_path = abs_path.substr(preLoc+1);
		return true;
	}
	
	return false;
}

class AutoLock
{
public:
	AutoLock(pthread_mutex_t* mutex):
		mutex_(mutex)
	{
		pthread_mutex_lock(mutex_);
	}

	~AutoLock()
	{
		pthread_mutex_unlock(mutex_);
	}

private:
	pthread_mutex_t* mutex_;
};

#endif
