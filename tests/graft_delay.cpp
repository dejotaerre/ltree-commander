#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>
static ssize_t (*original_write)(int,const void *,size_t);
static ssize_t (*original_copy)(int,off64_t *,int,off64_t *,size_t,unsigned);
static pthread_once_t initialized=PTHREAD_ONCE_INIT;
static void initialize(){
    original_write=reinterpret_cast<decltype(original_write)>(dlsym(RTLD_NEXT,"write"));
    original_copy=reinterpret_cast<decltype(original_copy)>(dlsym(RTLD_NEXT,"copy_file_range"));
}
static bool fixture(int fd){
    char path[4096],descriptor[64];snprintf(descriptor,sizeof(descriptor),"/proc/self/fd/%d",fd);
    const auto size=readlink(descriptor,path,sizeof(path)-1);if(size<0)return false;path[size]=0;
    return strstr(path,"/ltc-graft-progress-")!=nullptr;
}
// Solo los destinos aislados de estas pruebas reciben demora; GNU mv conserva su flujo normal.
extern "C" ssize_t write(int fd,const void *data,size_t count){
    pthread_once(&initialized,initialize);
    if(fixture(fd)){usleep(15000);count=std::min(count,size_t(65536));}
    return original_write(fd,data,count);
}
extern "C" ssize_t copy_file_range(int in,off64_t *from,int out,off64_t *to,size_t size,unsigned flags){
    pthread_once(&initialized,initialize);
    if(fixture(out)){errno=ENOSYS;return -1;}
    return original_copy(in,from,out,to,size,flags);
}
