#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>


int main(int argc, char** argv){


    while(1)
    {
		void* ptr = malloc(9);
		void* mptr  = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
		munmap(mptr, 4096);
		free(ptr);
		sleep(1);
    }
 
    return 1;
}
