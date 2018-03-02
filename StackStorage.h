#ifndef _STACK_STORAGE_
#define _STACK_STORAGE_

#include <stdio.h>
#include <stdlib.h>

typedef int STACK_ID;

STACK_ID ReferenceStack(int size);
void DereferenceStack(STACK_ID stackId, int size);
void DumpPopularStacks(FILE* f);
void InitStackStorage();
typedef enum _HeapwatchMethod
{
	BySize  = 1,
    ByRefCount = 2,
    ByAllocCount = 4,
    ByFreeCount = 8,
} HeapWatchMethod;
#define MIN_DUMP 1
#define MAX_DUMP 1000
#define DEFAULT_DUMP 10
extern HeapWatchMethod heapWatchMethod;
extern int heapWatchSize;

#endif

