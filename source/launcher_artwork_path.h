#ifndef LAUNCHER_ARTWORK_PATH_H
#define LAUNCHER_ARTWORK_PATH_H

#include <stddef.h>
#include <string.h>

typedef int (*LauncherArtworkResolveDirectoryComponent)(void *context,
	const char *parent_path, const char *component,
	char *resolved_name, size_t resolved_name_capacity);

/* Resolve every component of an absolute directory path through the caller's
   directory-entry lookup. This lets artwork paths use LFNs while ROM I/O can
   continue using the SFN aliases retained by browser navigation. */
static inline int LauncherArtworkResolveDirectoryPath(const char *path,
	char *out_path, size_t out_path_capacity,
	char *resolved_name, size_t resolved_name_capacity,
	LauncherArtworkResolveDirectoryComponent resolve_component, void *context)
{
	const char *component_start;
	const char *component_end;
	char component[256];
	size_t component_length;
	size_t out_length;
	size_t name_length;
	size_t separator_length;

	if(!path || path[0] != '/' || !out_path || out_path_capacity < 2 ||
		!resolved_name || !resolved_name_capacity || !resolve_component)
		return 0;
	out_path[0] = '\0';
	if(path[1] == '\0')
	{
		memcpy(out_path, "/", 2);
		return 1;
	}
	memcpy(out_path, "/", 2);
	component_start = path + 1;

	while(*component_start)
	{
		component_end = component_start;
		while(*component_end && *component_end != '/')
			component_end++;
		component_length = (size_t)(component_end - component_start);
		if(!component_length || component_length >= sizeof(component))
			goto fail;
		memcpy(component, component_start, component_length);
		component[component_length] = '\0';
		resolved_name[0] = '\0';
		if(!resolve_component(context, out_path, component,
			resolved_name, resolved_name_capacity) || !resolved_name[0] ||
			strchr(resolved_name, '/') || strchr(resolved_name, '\\'))
			goto fail;

		out_length = strlen(out_path);
		name_length = strlen(resolved_name);
		separator_length = (out_length > 1) ? 1 : 0;
		if(name_length > (size_t)-1 - out_length - separator_length - 1 ||
			out_length + separator_length + name_length + 1 > out_path_capacity)
			goto fail;
		if(separator_length)
			out_path[out_length++] = '/';
		memcpy(out_path + out_length, resolved_name, name_length + 1);

		if(*component_end == '\0')
			return 1;
		component_start = component_end + 1;
		if(!*component_start)
			goto fail;
	}

fail:
	out_path[0] = '\0';
	return 0;
}

#endif
