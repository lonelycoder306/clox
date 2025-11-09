#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

typedef struct {
    Value key;
    Value value;
} Entry;

// Load factor = count / capacity.
typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, Value key, Value* value);
// To add a key-value pair to a table.
// Returns true if key-value pair is new.
// New value overwrites old one.
bool tableSet(Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars,
                            int length, uint32_t hash);
void markTable(Table* table);
void tableRemoveWhite(Table* table);

#endif