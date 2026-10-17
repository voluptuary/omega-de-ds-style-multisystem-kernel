#ifndef NOR_CATALOG_TRANSACTION_H
#define NOR_CATALOG_TRANSACTION_H

#include <stddef.h>
#include <string.h>

typedef int (*NOR_CatalogPersistCallback)(void *context);

/* Expose a pending slot to the persistence callback, then restore it on failure. */
static inline int NOR_CommitCatalogSlot(void *catalog_slot, const void *new_slot,
	void *backup_slot, size_t slot_size, NOR_CatalogPersistCallback persist,
	void *persist_context)
{
	if(!catalog_slot || !new_slot || !backup_slot || !slot_size || !persist)
		return 0;

	memcpy(backup_slot, catalog_slot, slot_size);
	memcpy(catalog_slot, new_slot, slot_size);
	if(!persist(persist_context))
	{
		memcpy(catalog_slot, backup_slot, slot_size);
		return 0;
	}
	return 1;
}

/* Keep the runtime catalog empty after a physical NOR format, even on save failure. */
static inline int NOR_ClearCatalogAndPersist(void *catalog, size_t catalog_size,
	NOR_CatalogPersistCallback persist, void *persist_context)
{
	if(!catalog || !catalog_size || !persist)
		return 0;

	memset(catalog, 0, catalog_size);
	return persist(persist_context);
}

#endif
