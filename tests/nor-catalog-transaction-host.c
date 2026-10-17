#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nor_catalog_transaction.h"

typedef struct CatalogEntry
{
	unsigned id;
	char name[12];
} CatalogEntry;

typedef struct PersistContext
{
	CatalogEntry *slot;
	const CatalogEntry *expected;
	int result;
	int called;
	int saw_candidate;
} PersistContext;

typedef struct EmptyPersistContext
{
	void *catalog;
	size_t size;
	int result;
	int called;
	int saw_empty;
} EmptyPersistContext;

static int persist_catalog(void *opaque)
{
	PersistContext *context = (PersistContext*)opaque;
	context->called++;
	context->saw_candidate = memcmp(context->slot, context->expected,
		sizeof(*context->slot)) == 0;
	return context->result;
}

static int persist_empty_catalog(void *opaque)
{
	EmptyPersistContext *context = (EmptyPersistContext*)opaque;
	const unsigned char *bytes = (const unsigned char*)context->catalog;
	size_t i;

	context->called++;
	context->saw_empty = 1;
	for(i = 0; i < context->size; i++)
	{
		if(bytes[i] != 0)
		{
			context->saw_empty = 0;
			break;
		}
	}
	return context->result;
}

static void check(int condition, const char *message)
{
	if(!condition)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

int main(void)
{
	CatalogEntry original = { 7, "old-title" };
	CatalogEntry replacement = { 9, "new-title" };
	CatalogEntry backup;
	CatalogEntry catalog[2] = {
		{ 7, "old-title" },
		{ 8, "older-title" }
	};
	unsigned char empty_catalog[sizeof(catalog)] = { 0 };
	PersistContext context;
	EmptyPersistContext empty_context;

	memset(&context, 0, sizeof(context));
	context.slot = &original;
	context.expected = &replacement;
	context.result = 0;
	check(!NOR_CommitCatalogSlot(&original, &replacement, &backup,
		sizeof(original), persist_catalog, &context),
		"failed persistence reports failure");
	check(context.called == 1 && context.saw_candidate,
		"persistence callback sees the pending catalog entry");
	check(original.id == 7 && strcmp(original.name, "old-title") == 0,
		"failed persistence restores the previous in-memory entry");

	context.called = 0;
	context.saw_candidate = 0;
	context.result = 1;
	check(NOR_CommitCatalogSlot(&original, &replacement, &backup,
		sizeof(original), persist_catalog, &context),
		"successful persistence reports success");
	check(context.called == 1 && context.saw_candidate,
		"successful persistence sees the pending catalog entry");
	check(original.id == 9 && strcmp(original.name, "new-title") == 0,
		"successful persistence keeps the new in-memory entry");

	memset(&empty_context, 0, sizeof(empty_context));
	empty_context.catalog = catalog;
	empty_context.size = sizeof(catalog);
	empty_context.result = 0;
	check(!NOR_ClearCatalogAndPersist(catalog, sizeof(catalog),
		persist_empty_catalog, &empty_context),
		"failed empty-catalog persistence is reported");
	check(empty_context.called == 1 && empty_context.saw_empty,
		"persistence callback sees the cleared catalog");
	check(memcmp(catalog, empty_catalog, sizeof(catalog)) == 0,
		"runtime catalog remains empty after persistence failure");

	catalog[0] = (CatalogEntry){ 7, "old-title" };
	catalog[1] = (CatalogEntry){ 8, "older-title" };
	empty_context.called = 0;
	empty_context.result = 1;
	check(NOR_ClearCatalogAndPersist(catalog, sizeof(catalog),
		persist_empty_catalog, &empty_context),
		"successful empty-catalog persistence reports success");
	check(empty_context.called == 1 && empty_context.saw_empty,
		"successful persistence sees the cleared catalog");

	puts("NOR catalog transaction scenarios passed.");
	return 0;
}
