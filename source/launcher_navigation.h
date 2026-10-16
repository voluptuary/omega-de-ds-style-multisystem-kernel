#ifndef LAUNCHER_NAVIGATION_H
#define LAUNCHER_NAVIGATION_H

static inline unsigned int LauncherNavigationStepIndex(unsigned int current,
	unsigned int total, int direction, unsigned int wrap)
{
	if(!total || current >= total || direction == 0)
		return current;

	if(direction > 0)
	{
		if(current < total - 1)
			return current + 1;
		return wrap ? 0 : current;
	}

	if(current > 0)
		return current - 1;
	return wrap ? total - 1 : current;
}

static inline unsigned int LauncherNavigationJumpIndex(unsigned int current,
	unsigned int total, int direction, unsigned int distance, unsigned int wrap)
{
	if(!total || current >= total || direction == 0 || distance == 0)
		return current;

	if(direction > 0)
	{
		if(distance < total - current)
			return current + distance;
		return wrap ? 0 : total - 1;
	}

	if(distance <= current)
		return current - distance;
	return wrap ? total - 1 : 0;
}

#endif
