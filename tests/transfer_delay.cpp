#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static ssize_t (*original_write)(int,const void *,size_t);
static int (*original_fsync)(int);
static pthread_once_t initialized=PTHREAD_ONCE_INIT;
static void initialize(void){original_write=reinterpret_cast<decltype(original_write)>(dlsym(RTLD_NEXT,"write"));original_fsync=reinterpret_cast<decltype(original_fsync)>(dlsym(RTLD_NEXT,"fsync"));}
static int copying(int fd){
    char path[4096],descriptor[64];snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",fd);
    ssize_t n=readlink(descriptor,path,sizeof(path)-1);if(n<0)return 0;path[n]=0;
    return strstr(path,"/.ltree-copy-")!=NULL;
}
/* La demora solo afecta a los temporales de copia del fixture. */
extern "C" ssize_t write(int fd,const void *data,size_t count){pthread_once(&initialized,initialize);if(copying(fd))usleep(10000);return original_write(fd,data,count);}
extern "C" int fsync(int fd){pthread_once(&initialized,initialize);if(copying(fd))usleep(1500000);return original_fsync(fd);}
