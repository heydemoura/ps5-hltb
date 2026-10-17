/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Read-only console inspection server. Stops after 15 minutes or /quit. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <ps5/kernel.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
static void reply(int c, const char *status) {
 char h[256]; int n=snprintf(h,sizeof h,"HTTP/1.0 %s\r\nContent-Type: application/octet-stream\r\nConnection: close\r\n\r\n",status); send(c,h,n,0);
}
static int sendall(int c,const void *p,size_t n) {
 while(n){ssize_t k=send(c,p,n,0);if(k<=0)return -1;p=(const char*)p+k;n-=k;}return 0;
}
int main(void) {
 int s=socket(AF_INET,SOCK_STREAM,0),one=1;
 setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
 struct sockaddr_in a={.sin_len=sizeof a,.sin_family=AF_INET,.sin_port=htons(9118),.sin_addr={.s_addr=INADDR_ANY}};
 if(bind(s,(struct sockaddr*)&a,sizeof a)||listen(s,4)){perror("listen");return 1;}
 printf("HLTB read-only probe on :9118 fw=%08x\n",kernel_get_fw_version());fflush(stdout);
 time_t until=time(NULL)+900;
 while(time(NULL)<until){fd_set f;FD_ZERO(&f);FD_SET(s,&f);struct timeval tv={1,0};if(select(s+1,&f,NULL,NULL,&tv)<=0)continue;
 int c=accept(s,NULL,NULL);if(c<0)continue;struct timeval io={5,0};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&io,sizeof io);setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,&io,sizeof io);
 char req[4096],path[2048];ssize_t n=recv(c,req,sizeof req-1,0);if(n<=0){close(c);continue;}req[n]=0;
 if(sscanf(req,"GET %2047s",path)!=1){reply(c,"400 Bad Request");close(c);continue;}
 if(!strcmp(path,"/quit")){reply(c,"200 OK");close(c);break;}
 if(!strcmp(path,"/info")){reply(c,"200 OK");char b[128];n=snprintf(b,sizeof b,"firmware=%08x\npid=%d\n",kernel_get_fw_version(),getpid());sendall(c,b,n);}
 else if(!strncmp(path,"/symbol/",8)) {int pid;char name[256],sym[256];if(sscanf(path+8,"%d/%255[^/]/%255s",&pid,name,sym)!=3)reply(c,"400 Bad Request");else{uint32_t h=0;int r=kernel_dynlib_handle(pid,name,&h);char b[256];int k=snprintf(b,sizeof b,"result=%d address=%lx\n",r,(unsigned long)(r?0:kernel_dynlib_dlsym(pid,h,sym)));reply(c,"200 OK");sendall(c,b,k);}}
 else if(!strncmp(path,"/module/",8)) {int pid;char name[256];if(sscanf(path+8,"%d/%255s",&pid,name)!=2)reply(c,"400 Bad Request");else{uint32_t h=0;int r=kernel_dynlib_handle(pid,name,&h);char b[256];int k=snprintf(b,sizeof b,"result=%d handle=%x base=%lx\n",r,h,(unsigned long)(r?0:kernel_dynlib_mapbase_addr(pid,h)));reply(c,"200 OK");sendall(c,b,k);}}
 else if(!strncmp(path,"/maps/",6)) {int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_VMMAP,atoi(path+6)};size_t z=0;
 if(sysctl(mib,4,NULL,&z,NULL,0))reply(c,"500 Maps Failed");else {void*b=malloc(z);if(!b||sysctl(mib,4,b,&z,NULL,0)){reply(c,"500 Maps Failed");}else{reply(c,"200 OK");size_t off=0;while(off+sizeof(int)<=z){struct kinfo_vmentry*v=(void*)((char*)b+off);if(v->kve_structsize<=0||off+v->kve_structsize>z)break;char line[2048];int k=snprintf(line,sizeof line,"%016lx %016lx %x %s\n",(unsigned long)v->kve_start,(unsigned long)v->kve_end,v->kve_protection,v->kve_path);sendall(c,line,k);off+=v->kve_structsize;}}free(b);}}
 else if(!strncmp(path,"/memory/",8)) {int pid;unsigned long addr,len;if(sscanf(path+8,"%d/%lx/%lx",&pid,&addr,&len)!=3||len>0x4000000)reply(c,"400 Bad Request");else{char*b=malloc(len);if(!b||kernel_proc_copyout(pid,addr,b,len))reply(c,"500 Read Failed");else{reply(c,"200 OK");sendall(c,b,len);}free(b);}}
 else if(!strncmp(path,"/list/",6)){DIR*d=opendir(path+5);if(!d)reply(c,"404 Not Found");else{reply(c,"200 OK");struct dirent*e;while((e=readdir(d))){sendall(c,e->d_name,strlen(e->d_name));sendall(c,"\n",1);}closedir(d);}}
 else if(!strncmp(path,"/file/",6)){FILE*f=fopen(path+5,"rb");if(!f)reply(c,"404 Not Found");else{reply(c,"200 OK");char b[16384];size_t k;while((k=fread(b,1,sizeof b,f)))if(sendall(c,b,k))break;fclose(f);}}
 else reply(c,"404 Not Found");close(c);
 }close(s);return 0;
}
