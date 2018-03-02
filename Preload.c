






#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <execinfo.h>
#include <memory.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>

#include "StackStorage.h"
#include "funchook.h"
#include <malloc.h>

size_t  heap_malloc_total, heap_free_total,mmap_total, mmap_count;

char* print_info() 
{ 
	static char tmpbuff[10240];
	int cur_pos = 0;
    struct mallinfo mi = mallinfo(); 
	cur_pos += sprintf(tmpbuff + cur_pos, "count by itself:\n"); 
    cur_pos += sprintf(tmpbuff + cur_pos, "\theap_malloc_total=%lu heap_free_total=%lu heap_in_use=%lu\n\tmmap_total=%lu mmap_count=%lu\n", 
              heap_malloc_total*1024, heap_free_total*1024, heap_malloc_total*1024-heap_free_total*1024, 
              mmap_total*1024, mmap_count); 
	cur_pos += sprintf(tmpbuff + cur_pos, "count by mallinfo:\n"); 
	cur_pos += sprintf(tmpbuff + cur_pos, "\theap_malloc_total=%lu heap_free_total=%lu heap_in_use=%lu\n\tmmap_total=%lu mmap_count=%lu\n", 
             mi.arena, mi.fordblks, mi.uordblks, 
             mi.hblkhd, mi.hblks); 
	cur_pos += sprintf(tmpbuff + cur_pos, "from malloc_stats:\n"); 

	return tmpbuff;
}

static pthread_mutex_t g_mmapStatMutex;
static FILE* g_mmapStatFile = NULL;

#define USE_HOOK

// note: __sync_add_and_fetch does not work
// if binary is built on Linux RHEL-4
static int atomic_add_return(unsigned long long *v, int i)
{
	return __sync_fetch_and_add(v, i);

}

static int gLogCount = 0;
static int gIsLogEnabled = 1;
#define LOG_COUNT_LIMIT 10
void PreloadLog(const char* format, ...)
{
    if(gIsLogEnabled)
    {
        gLogCount++;
        if(gLogCount <= LOG_COUNT_LIMIT)
        {
            va_list args;
            va_start (args, format);
            vprintf (format, args);
            va_end (args);            
        }
    }
}

static int gIsDumpRequested = 0;
static unsigned long long gMallocCount = 0;
static unsigned long long gCallocCount = 0;
static unsigned long long gFreeCount = 0;
static unsigned long long gReallocCount = 0;
static unsigned long long gMemalignCount = 0;
static unsigned long long gVallocCount = 0;
static unsigned long long gMismatchedFreeCount = 0;
static unsigned long long gMismatchedReallocCount = 0;
static unsigned long long gTotalMem = 0;

int g_hasStarted = 0;

static pthread_mutex_t gMutex;
__thread int inCall;

static char dummyBuf[8192];
static unsigned long dummyPos = 0;
static unsigned long dummyAllocs = 0;

void UserSignalHandler(int sig);

void* (*RealMalloc)(size_t size);
void* (*RealCalloc)(size_t nmemb, size_t size);
void* (*RealRealloc)(void *ptr, size_t size);
void* (*RealValloc)(size_t size);
int   (*RealPosixMemalign)(void** memptr, size_t alignment, size_t size);
void  (*RealFree)(void *ptr);
void  (*RealFork)(void);
void  (*Real_malloc_stats)(void);
int (*Real_mallopt)(int param,int value);



__thread int inMapCall = 0;
void* (*RealMmap)(void *addr, size_t length, int prot, int flags,int fd, off_t offset);
int (*RealMunmap)(void *addr, size_t length);
void* (*RealMremap)(void *old_address, size_t old_size,size_t new_size, int flags, ... /* void *new_address */);
void* (*RealSbrk)(intptr_t increment);


void* hook_mmap(void *addr, size_t length, int prot, int flags,int fd, off_t offset);
int hook_munmap(void *addr, size_t length);
void* hook_mremap(void *old_address, size_t old_size,size_t new_size, int flags, ... /* void *new_address */);

void *hook_sbrk(intptr_t increment);

void init_ctl_thread();



static void* (*TempMalloc)(size_t size);
static void* (*TempCalloc)(size_t nmemb, size_t size);
static void* (*TempRealloc)(void *ptr, size_t size);
static void* (*TempValloc)(size_t size);
static int   (*TempPosixMemalign)(void** memptr, size_t alignment, size_t size);
static void  (*TempFree)(void *ptr);

int   (*RealBacktrace)(void**,int);
char**(*RealBacktraceSymbols)(void*const*,int);

typedef struct _BLOCK_HDR
{
    STACK_ID magic;
    STACK_ID stackId;
	int  size;
	int  reserve;

} BLOCK_HDR;

#define MAGIC 0xA0BC0DEF





void* DummyMalloc(size_t size)
{
    if (dummyPos + size >= sizeof(dummyBuf))
    {
        exit(1);
    }
    void *retptr = dummyBuf + dummyPos;
    dummyPos += size;
    ++dummyAllocs;
    return retptr;
}

void* DummyCalloc(size_t nmemb, size_t size)
{
    void *ptr = DummyMalloc(nmemb * size);
    unsigned int i = 0;
    for (; i < nmemb * size; ++i)
    {
        ((char*)ptr)[i] = 0;
    }
    return ptr;
}

void DummyFree(void *ptr)
{
}

void __attribute__((constructor)) Init()
{
	init_ctl_thread();
	////mallopt(M_MMAP_THRESHOLD, 128);
	g_mmapStatFile = fopen("mmapstat1.txt", "w");

    inCall = 1;
    // This runs on startup
    PreloadLog("Initializing memory traces... `echo \"help\"|nc -U /tmp/heapwatch.sock` to get help..\n");

	pthread_mutex_init(&g_mmapStatMutex, NULL);

    // init lock
     pthread_mutex_init(&gMutex, NULL);

    // set signal handler
    //signal(7, UserSignalHandler);
	//signal(12, UserSignalHandler);

    // hook malloc
    // use Dummy* implementation on initialization (may be called from dlsym etc)
    RealMalloc         = DummyMalloc;
    RealCalloc         = DummyCalloc;
    RealRealloc        = NULL;
    RealFree           = DummyFree;
    RealValloc         = NULL;
    RealPosixMemalign  = NULL;

    // get addresses of real malloc, free, etc
    TempMalloc         = dlsym(RTLD_NEXT, "malloc");
    TempCalloc         = dlsym(RTLD_NEXT, "calloc");
    TempRealloc        = dlsym(RTLD_NEXT, "realloc");
    TempFree           = dlsym(RTLD_NEXT, "free");
    TempValloc         = dlsym(RTLD_NEXT, "valloc");
    TempPosixMemalign  = dlsym(RTLD_NEXT, "posix_memalign");

	Real_malloc_stats = dlsym(RTLD_NEXT, "malloc_stats");
	
	RealFork = dlsym(RTLD_NEXT, "fork");
    if (!TempMalloc || !TempCalloc || !TempRealloc || !TempFree ||
        !TempValloc || !TempPosixMemalign)
    {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
        exit(1);
    }

    // 

    RealMalloc         = TempMalloc;
    RealCalloc         = TempCalloc;
    RealRealloc        = TempRealloc;
    RealFree           = TempFree;
    RealValloc         = TempValloc;
    RealPosixMemalign  = TempPosixMemalign;
	

	RealMmap = dlsym(RTLD_NEXT, "mmap");
	RealMunmap = dlsym(RTLD_NEXT, "munmap");
	RealMremap = dlsym(RTLD_NEXT, "mremap");
	RealSbrk = dlsym(RTLD_NEXT, "sbrk");
#ifdef USE_HOOK 
	// 使用HOOK方式，如果不定义则使用LD_PRELOAD方式

	funchook_t *malloc_ft = funchook_create();	
	funchook_t *calloc_ft = funchook_create();
	funchook_t *realloc_ft = funchook_create();
	funchook_t *free_ft = funchook_create();
	funchook_t *valloc_ft = funchook_create();
	funchook_t *posixmemalign_ft = funchook_create();
	funchook_t *fork_ft = funchook_create();

	funchook_t *mmap_ft = funchook_create();
	funchook_t *munmap_ft = funchook_create();
	funchook_t *mremap_ft = funchook_create();	
	funchook_t *msbrk_ft = funchook_create();

	PreloadLog("Init RealMalloc=%p, RealCalloc=%p, RealRealloc=%p, RealFree=%p, RealValloc=%p, RealPosixMemalign=%p,RealFork=%p", RealMalloc, RealCalloc, RealRealloc, RealFree, RealValloc, RealPosixMemalign, RealFork);
	
	
    funchook_prepare(malloc_ft, (void**)&RealMalloc, malloc);
    funchook_install(malloc_ft, 0);

	funchook_prepare(calloc_ft, (void**)&RealCalloc, calloc);
	funchook_install(calloc_ft, 0);

    funchook_prepare(realloc_ft, (void**)&RealRealloc, realloc);
    funchook_install(realloc_ft, 0);	
	
    funchook_prepare(free_ft, (void**)&RealFree, free);
    funchook_install(free_ft, 0);

	
    funchook_prepare(valloc_ft, (void**)&RealValloc, valloc);
    funchook_install(valloc_ft, 0);

	funchook_prepare(posixmemalign_ft, (void**)&RealPosixMemalign, posix_memalign);
	funchook_install(posixmemalign_ft, 0);

	funchook_prepare(fork_ft, (void**)&RealFork, fork);
	funchook_install(fork_ft, 0);	

/*
	funchook_prepare(mmap_ft, (void**)&RealMmap, hook_mmap);
	funchook_install(mmap_ft, 0);		

	funchook_prepare(munmap_ft, (void**)&RealMunmap, hook_munmap);
	funchook_install(munmap_ft, 0);	

	funchook_prepare(mremap_ft, (void**)&RealMremap, hook_mremap);
	funchook_install(mremap_ft, 0);	

	funchook_prepare(msbrk_ft, (void**)&RealSbrk, hook_sbrk);
	funchook_install(msbrk_ft, 0);	
*/

#endif

    RealBacktrace = dlsym(RTLD_NEXT, "backtrace");
    RealBacktraceSymbols = dlsym(RTLD_NEXT, "backtrace_symbols");
    if(!RealBacktrace || !RealBacktraceSymbols)
    {
        fprintf(stderr, "error in `dlsym`(2): %s\n", dlerror());
    }

    PreloadLog("Initializing stack storage...\n");
    InitStackStorage();
    PreloadLog("Done\n");
    inCall = 0;

	
}

void UserSignalHandler(int sig)
{
    if(sig == 7)
    {
        g_hasStarted = 1;
    }
    else   if(sig == 12)
    {
        gIsDumpRequested = 1;
    }
}

void DumpInfo();

void* malloc(size_t size)
{
    STACK_ID stackId;
    BLOCK_HDR* blockHdr;

    if(inCall || 0 ==  g_hasStarted)
    {
        return RealMalloc(size);
    }
    inCall = 1;

    atomic_add_return(&gMallocCount, 1);
	__sync_fetch_and_add(&gTotalMem, size);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    stackId = ReferenceStack(size);
    blockHdr = RealMalloc(size + sizeof(BLOCK_HDR));
    blockHdr->magic = MAGIC;
    blockHdr->stackId = stackId;
	blockHdr->size = size;

    PreloadLog("malloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               stackId,
               size,
               size+sizeof(BLOCK_HDR),
               blockHdr,
               (char*)blockHdr + sizeof(BLOCK_HDR));

    inCall = 0;
    return (void*)((char*)blockHdr + sizeof(BLOCK_HDR));
}

void* calloc(size_t nmemb, size_t size)
{
    STACK_ID stackId;
    BLOCK_HDR* blockHdr;
    void* result;

    if(inCall || 0 ==  g_hasStarted)
    {
        return RealCalloc(nmemb, size);
    }
    inCall = 1;

    atomic_add_return(&gCallocCount, 1);
	__sync_fetch_and_add(&gTotalMem, nmemb*size);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    stackId = ReferenceStack( nmemb*size);
    blockHdr = RealMalloc(nmemb*size + sizeof(BLOCK_HDR));
    blockHdr->magic = MAGIC;
    blockHdr->stackId = stackId;
	blockHdr->size = nmemb*size;
    result = (void*)((char*)blockHdr + sizeof(BLOCK_HDR));
    memset(result, 0, nmemb*size);

    PreloadLog("calloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               stackId,
               size*nmemb,
               size*nmemb+sizeof(BLOCK_HDR),
               blockHdr,
               result);

    inCall = 0;
    return result;
}

void* realloc(void *ptr, size_t size)
{
    BLOCK_HDR* blockHdr;
    void* result;
    if(inCall || 0 ==  g_hasStarted)
    {
        return RealRealloc(ptr, size);
    }

    if(ptr == NULL) 
    {
        STACK_ID stackId;
        inCall = 1;
        atomic_add_return(&gReallocCount, 1);
		__sync_fetch_and_add(&gTotalMem, size);
        PreloadLog("realloc(NULL, %d) called...\n", size);
        stackId = ReferenceStack(size);
        blockHdr = RealMalloc(size + sizeof(BLOCK_HDR));
        blockHdr->magic = MAGIC;
        blockHdr->stackId = stackId;
		blockHdr->size = size;
        result = (void*)((char*)blockHdr + sizeof(BLOCK_HDR));
        PreloadLog("realloc(NULL, %d) returning %p\n", size, result);
        inCall = 0;
        return result;
    }

    blockHdr = (BLOCK_HDR*)((char*)ptr - sizeof(BLOCK_HDR));
    if(blockHdr->magic != MAGIC)
    {
        atomic_add_return(&gMismatchedReallocCount, 1);
        return RealRealloc(ptr, size);
    }
    
    inCall = 1;
    atomic_add_return(&gReallocCount, 1);

    PreloadLog("realloc stackId: %08x, orig.size: %d, act.size: %d, ptr: %p, user ptr: %p\n",
               blockHdr->stackId,
               size,
               size+sizeof(BLOCK_HDR),
               blockHdr,
               ptr);

	blockHdr->magic = 0;
	__sync_fetch_and_sub(&gTotalMem, blockHdr->size);
	DereferenceStack(blockHdr->stackId, blockHdr->size);
	STACK_ID stackId = ReferenceStack(size);
	__sync_fetch_and_add(&gTotalMem, size);
	
    blockHdr = RealRealloc(blockHdr, size+sizeof(BLOCK_HDR));
	blockHdr->magic = MAGIC;
    blockHdr->stackId = stackId;
	blockHdr->size = size;
    inCall = 0;
    return (void*)((char*)blockHdr+sizeof(BLOCK_HDR));
}

void free(void *ptr)
{
    BLOCK_HDR* blockHdr;
    if(ptr == NULL) return;
    if(inCall || 0 ==  g_hasStarted)
    {
        return RealFree(ptr);
    }

    blockHdr = (BLOCK_HDR*)((char*)ptr - sizeof(BLOCK_HDR));
    if(blockHdr->magic != MAGIC)
    {
        atomic_add_return(&gMismatchedFreeCount, 1);
        return RealFree(ptr);
    }

    inCall = 1;
    atomic_add_return(&gFreeCount, 1);
	__sync_fetch_and_sub(&gTotalMem, blockHdr->size);
    int isDumpRequested = 0;
    if(gIsDumpRequested)
    {
        pthread_mutex_lock(&gMutex);
        if(gIsDumpRequested)
        {
            gIsDumpRequested = 0;
            isDumpRequested = 1;
        }
        pthread_mutex_unlock(&gMutex);
    }

    if(isDumpRequested)
    {
        DumpInfo();
    }

    DereferenceStack(blockHdr->stackId, blockHdr->size);

    PreloadLog("free stackId: %08x, ptr: %p, user ptr: %p\n",
               blockHdr->stackId,
               blockHdr,
               ptr);

	blockHdr->magic = 0;
    RealFree(blockHdr);
    inCall = 0;
}

int posix_memalign(void** memptr, size_t alignment, size_t size)
{
    atomic_add_return(&gMemalignCount, 1);
    return RealPosixMemalign(memptr, alignment, size);
}

void* valloc(size_t size)
{
    atomic_add_return(&gVallocCount, 1);
    return RealValloc(size);
}


void hook_output(const char* opt, ...){

	int old_InCall = inCall;

	inCall = 1;
	
	pthread_mutex_lock(&g_mmapStatMutex);
	va_list ap;	
	va_start(ap, opt);		
	vfprintf(g_mmapStatFile,opt, ap);	
	va_end(ap);

	 
	/* get new_addr parameter. */	
	int j = 0;
	
	void* currentStack[30];
	int stackSize = RealBacktrace(currentStack, 30);	

/*
	char** strings = RealBacktraceSymbols(currentStack, stackSize);

	fprintf(g_mmapStatFile, "(");
	
	for(j = 0; j  < stackSize; j++){
		fprintf(g_mmapStatFile, "|%s", strings[j]);
	}*/


	fprintf(g_mmapStatFile, " %d\n", time(NULL));
	 
    int stack_idx = 0;
	for(j=0; j < stackSize; j++)
	{
		Dl_info info;
		void* fun_addr = currentStack[stack_idx++];
		dladdr(fun_addr, &info);		
		int xx = (int)((unsigned long long )fun_addr - (unsigned long long )info.dli_fbase);
		fprintf(g_mmapStatFile, "## %s %d %p %p\n", info.dli_fname, xx,fun_addr, info.dli_fbase);
	} 


	fprintf(g_mmapStatFile, "\n");
	//RealFree(strings);

	fflush(g_mmapStatFile);

	
	pthread_mutex_unlock(&g_mmapStatMutex);

	inCall = old_InCall;
}

void *hook_mmap(void *addr, size_t length, int prot, int flags,
				 int fd, off_t offset){

	if(inMapCall == 1 || 0 ==  g_hasStarted){
		return RealMmap(addr, length, prot, flags, fd, offset);
	}

	inMapCall = 1;
	void* ret = RealMmap(addr, length, prot, flags, fd, offset);
	hook_output("[MMAP] %p %u", ret, (unsigned int)length);
	inMapCall = 0;
	return ret;
}
int hook_munmap(void *addr, size_t length)
{
	if(inMapCall == 1 || 0 ==  g_hasStarted){
		return RealMunmap(addr, length);
	}

	inMapCall = 1;

	int ret = RealMunmap(addr, length);
	hook_output("[MUNMAP] %p %u", addr, (unsigned int)length);

	inMapCall = 0;
	return ret;
}

void *hook_mremap(void *old_address, size_t old_size,
                    size_t new_size, int flags, ... /* void *new_address */){

	void *new_addr = NULL;	
	va_list ap;	
	if(flags & MREMAP_FIXED) 
	{		
		/* get new_addr parameter. */		
		va_start(ap, flags);		
		new_addr = va_arg(ap, void *);		
		va_end(ap);	
	}	
	

	if(inMapCall == 1 || 0 ==  g_hasStarted){
		return RealMremap(old_address, old_size, new_size, flags, new_addr);
	}

	inMapCall = 1;
 	
	void* ret = RealMremap(old_address, old_size, new_size, flags, new_addr);
	hook_output("[MREMAP] %p %u %p %u", old_address, (unsigned int)old_size, ret, (unsigned int)new_size);

	inMapCall = 0;
	return ret;
}

void *hook_sbrk(intptr_t increment){

	
	if(inMapCall == 1 || 0 ==  g_hasStarted){
		return RealSbrk(increment);;
	}

	inMapCall = 1;
	void* ret =RealSbrk(increment);
	hook_output("[SBRK] %p %u", ret, (unsigned int)increment);
	inMapCall = 0;
	return ret;	
}


void
DumpInfo()
{
	inMapCall = 1;
    FILE* result = fopen("heapwatch.dump", "w");
    if(result != NULL)
    {
    	fprintf(result, "unfree size: %llu\n", gTotalMem);
        fprintf(result, "malloc count: %llu\n", gMallocCount);
        fprintf(result, "calloc count: %llu\n", gCallocCount);
        fprintf(result, "realloc count: %llu\n", gReallocCount);
        fprintf(result, "free count: %llu\n", gFreeCount);
        fprintf(result, " *** non-zero numbers below mean trouble *** \n");        
        fprintf(result, "valloc count: %llu\n", gVallocCount);
        fprintf(result, "memalign count: %llu\n", gMemalignCount);
        fprintf(result, "mismatched free count: %llu\n", gMismatchedFreeCount);
        fprintf(result, "mismatched realloc count: %llu\n", gMismatchedReallocCount);
        fprintf(result, " ***   ***   ***\n\n");        
        DumpPopularStacks(result);
		fclose(result);
    }
	inMapCall = 0;
}

pid_t fork(){
	PreloadLog("skip fork, just return 0...\n");
	return 0;
}



int ctl_thread(void* data){

	const char* sock_path = "/tmp/heapwatch.sock";

	static char buff[4096];

	int pos = 0;
	
	unlink(sock_path);
	
	/* create a socket */
	int server_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	
	struct sockaddr_un server_addr;
	server_addr.sun_family = AF_UNIX;
	strcpy(server_addr.sun_path, sock_path);
	
	/* bind with the local file */
	bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	
	/* listen */
	listen(server_sockfd, 5);

	char message[255];

	char cmd[1024 + 1];
	
	char ch;
	int client_sockfd;
	struct sockaddr_un client_addr;
	socklen_t len = sizeof(client_addr);
	while(1)
	{
	  PreloadLog("server waiting:\n");
	
	  /* accept a connection */
	  client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &len);

	  if(client_sockfd == -1)
	  	continue;


	  memset(cmd, 0, sizeof(cmd));
	
	  /* exchange data */
	  read(client_sockfd, cmd, 1024);
	  
	  PreloadLog("get cmd from client: %s\n", cmd);

	  write(client_sockfd, cmd, strlen(cmd));
	  
	  if(strncmp(cmd, "begin", 5) == 0){
			g_hasStarted = 1;
			sprintf(message, "set start g_hasStarted=%d\n", g_hasStarted);
			PreloadLog("%s", message);
			write(client_sockfd, message, strlen(message));
	  }else if(strncmp(cmd, "dump", 4) == 0){
			sprintf(message, "begin dump now!!\n");
			PreloadLog("%s", message);
			write(client_sockfd, message, strlen(message));
			
			inCall = 1;
			DumpInfo();
			inCall = 0;
			
			sprintf(message, "dump end!!\n");
			write(client_sockfd, message, strlen(message));
	  }else if(strncmp(cmd, "status", 6) == 0){

			sprintf(message, "g_hasStarted=%d gIsDumpRequested=%d,heapWatchSize=%d,heapWatchMethod=%d\n",g_hasStarted, gIsDumpRequested, heapWatchSize, heapWatchMethod);
			PreloadLog("%s", message);
			write(client_sockfd, message, strlen(message));	
	  }else if(strncmp(cmd, "show", 4) == 0){
			pos = 0;
			pos += sprintf(buff + pos, "unfree size: %llu\n", gTotalMem);
			pos += sprintf(buff + pos, "malloc count: %llu\n", gMallocCount);
			pos += sprintf(buff + pos, "calloc count: %llu\n", gCallocCount);
			pos += sprintf(buff + pos, "realloc count: %llu\n", gReallocCount);
			pos += sprintf(buff + pos, "free count: %llu\n", gFreeCount);
			pos += sprintf(buff + pos, " *** non-zero numbers below mean trouble *** \n"); 	   
			pos += sprintf(buff + pos, "valloc count: %llu\n", gVallocCount);
			pos += sprintf(buff + pos, "memalign count: %llu\n", gMemalignCount);
			pos += sprintf(buff + pos, "mismatched free count: %llu\n", gMismatchedFreeCount);
			pos += sprintf(buff + pos, "mismatched realloc count: %llu\n", gMismatchedReallocCount);
			pos += sprintf(buff + pos, " ***	***   ***\n\n");  
			write(client_sockfd, buff, strlen(buff));
			
	  }else if(strncmp(cmd, "watchmethod=", 12) == 0){
			 
			heapWatchMethod = atoi(cmd + 12);
			sprintf(message, "heapWatchMethod=%d\n", heapWatchMethod);
			write(client_sockfd, message, strlen(message));	
	  }else if(strncmp(cmd, "watchsize=", 10) == 0){
			int size = atoi(cmd + 10);

			if(size >= MIN_DUMP && size <= MAX_DUMP){
				 
				heapWatchSize = size;
			}

			sprintf(message, "heapWatchSize=%d\n", heapWatchSize);
			write(client_sockfd, message, strlen(message));	
	  	}else if(strncmp(cmd, "bt", 2) == 0){/*
			int i = 0;
			for(i = 0; i  < 1024*64*100; i++){
				char* p = (char*)RealMalloc(16);
				p[0] = 'a';
				p[1] = '\0';
				if(i%100 == 0)
					write(client_sockfd, p, 1);	
			}*/
			char* p  = print_info();
			

			Real_malloc_stats();
			sprintf(message, "heapWatchSize=%d\n", heapWatchSize);
			write(client_sockfd, p, strlen(p));	

			FILE* fp = fopen("meminfo_0.txt", "w");
			malloc_info(0, fp);
			
			fclose(fp);

		    fp = fopen("meminfo_1.txt", "w");
			malloc_info(0, fp);
			
			fclose(fp);			
	  	}
	  else{
			pos = 0;
			pos += sprintf(buff + pos, "****** help version 1.0 *****\n");
			pos += sprintf(buff + pos,"`echo \"status\"|nc -U /tmp/heapwatch.sock` get status.\n");
			pos += sprintf(buff + pos,"`echo \"begin\"|nc -U /tmp/heapwatch.sock` to start watch.\n");
			pos += sprintf(buff + pos,"`echo \"show\"|nc -U /tmp/heapwatch.sock` to get base stat info.\n");	
			pos += sprintf(buff + pos,"`echo \"dump\"|nc -U /tmp/heapwatch.sock` to dump.\n");
			pos += sprintf(buff + pos,"`echo \"watchmethod=X\"|nc -U /tmp/heapwatch.sock` to dump. X can be set 1,2,4,8, and also can be set add value[	BySize  = 1,  ByRefCount = 2,    ByAllocCount = 4,    ByFreeCount = 8,]\n");			
			pos += sprintf(buff + pos,"`echo \"watchsize=X\"|nc -U /tmp/heapwatch.sock`  to dump. X cant be set from 1 to 1000 integer\n");
			pos += sprintf(buff + pos, "****** end help *****\n");
			write(client_sockfd, buff, strlen(buff));	
	  }

	  /* close the socket */
	  close(client_sockfd);
	}

}

void init_ctl_thread(){
	pthread_t id_1;
	int ret=pthread_create(&id_1,NULL,(void  *) ctl_thread,NULL);
	if(ret != 0){
		PreloadLog("init ctl thread failed... exit");
		exit(0);
	}
}	



