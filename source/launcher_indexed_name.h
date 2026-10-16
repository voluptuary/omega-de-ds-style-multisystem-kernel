#ifndef LAUNCHER_INDEXED_NAME_H
#define LAUNCHER_INDEXED_NAME_H

#include <stddef.h>
#include <string.h>

typedef int (*LauncherIndexedNameResolver)(void *context, const char *identity,
	char *out_name, size_t out_name_capacity);

/* Directory indexes keep a complete LFN when the source name fits. Use that
   value directly; only entries explicitly marked as clipped need a FatFs
   identity lookup through their short-name alias. */
static inline int LauncherCopyIndexedNameOrResolve(const char *indexed_name,
	unsigned int flags, unsigned int clipped_name_flag, const char *identity,
	char *out_name, size_t out_name_capacity,
	LauncherIndexedNameResolver resolver, void *context)
{
	size_t length;

	if(!out_name || !out_name_capacity)
		return 0;
	out_name[0] = '\0';
	if(!indexed_name || !indexed_name[0])
		return 0;

	if(!(flags & clipped_name_flag))
	{
		length = strlen(indexed_name);
		if(length >= out_name_capacity)
			return 0;
		memcpy(out_name, indexed_name, length + 1);
		return 1;
	}

	if(!identity || !identity[0] || !resolver ||
		!resolver(context, identity, out_name, out_name_capacity))
	{
		out_name[0] = '\0';
		return 0;
	}
	if(!memchr(out_name, '\0', out_name_capacity) || !out_name[0])
	{
		out_name[0] = '\0';
		return 0;
	}
	return 1;
}

#endif
