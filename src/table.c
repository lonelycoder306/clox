#include "../include/table.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/value.h"
#include <stdlib.h>
#include <string.h>

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table)
{
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table)
{
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity, Value key)
{
    uint32_t index = hashValue(key) % capacity;
    Entry* tombstone = NULL;
    while (true)
    {
        Entry* entry = &entries[index];
        if (valuesEqual(entry->key, key))
            // We found the key.
            return entry;
        else if (IS_EMPTY(entry->key))
        {
            if (IS_NIL(entry->value))
                // Empty entry.
                // Key is NULL =
                // Key not found (for look-up).
                // Empty space found (for insertion).
                return (tombstone == NULL ? entry : tombstone);
            else
            {
                // We found a tombstone.
                // Only assign tombstone the first time.
                if (tombstone == NULL) tombstone = entry;
            }
        }
        
        index = (index + 1) % capacity;
    }
}

static void adjustCapacity(Table* table, int capacity)
{
    Entry* entries = ALLOCATE(Entry, capacity);
    // Initialize array.
    for (int i = 0; i < capacity; i++)
    {
        entries[i].key = EMPTY_VAL;
        entries[i].value = NIL_VAL;
    }

    // Re-build table.
    table->count = 0;
    for (int i = 0; i < table->capacity; i++)
    {
        Entry* entry = &table->entries[i];
        // We drop tombstones and empty buckets.
        if (IS_EMPTY(entry->key)) continue;

        // If we can slot the entry into the correct bucket,
        // we do so.
        // Otherwise, the nearest empty bucket.
        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

bool tableGet(Table* table, Value key, Value* value)
{
    // Can't access bucket array if it's NULL.
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_EMPTY(entry->key)) return false;

    value = &entry->value;
    (void) value;
    return true;
}

bool tableSet(Table* table, Value key, Value value)
{
    // Table will always have empty buckets.
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD)
    {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = (IS_EMPTY(entry->key));
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, Value key)
{
    if (table->count == 0) return false;

    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (IS_EMPTY(entry->key)) return false; // Didn't find it.

    // Place a tombstone in the entry.
    entry->key = EMPTY_VAL;
    entry->value = BOOL_VAL(true);
    return true;
}

void tableAddAll(Table* from, Table* to)
{
    for (int i = 0; i < from->capacity; i++)
    {
        Entry* entry = &from->entries[i];
        if (IS_EMPTY(entry->key))
            tableSet(to, entry->key, entry->value);
    }
}

ObjString* tableFindString(Table* table, const char* chars,
                            int length, uint32_t hash)
{
    if (table->count == 0) return NULL;
    
    uint32_t index = hash % table->capacity;
    while (true)
    {
        Entry* entry = &table->entries[index];
        if (IS_EMPTY(entry->key))
        {
            // Stop if we find a non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
        }

        ObjString* string = AS_STRING(entry->key);
        if (string->length == length &&
            string->hash == hash &&
            memcmp(string->chars, chars, length) == 0)
                // We found it.
                return string;
        
        index = (index + 1) % table->capacity;
    }
}