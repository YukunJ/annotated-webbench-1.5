/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

// Some constant and config variable settings
// 程序的一些常量和配置数值的声明
/* values */
volatile int timerexpired=0;
int speed=0;
int failed=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;
int clients=1;
int force=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

// reference: https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Options.html
// to parse long option, when the 3rd pointer argument is not null,
// 4th flag value will be stored if triggered
// when the 3rd pointer is NULL, the 4th flag value is returned from getopt_long when triggered
// 当分解参数时, 当第三个指针参数非空时, 第四个flag值将被存储间指针所指变量, 如果这个option被触发的话
// 而当第三个指针参数是空指针时, getopt_long将直接把第四个flag值返回
static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
// three principal procedures to be further analyzed below
// 三大主要函数 下面会具体仔细分析
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

// when benchmark times out, it triggers the signal and handler set the flag
// 当benchmark的测试时间到了后, 信号量触发, 这个handler会把超时flag设立起来
static void alarm_handler(int signal)
{
   timerexpired=1;
}	

// helper information printing
// 使用指南信息打印
static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};

// main entry
// it parses all the argument and build the HTTP request
// then enter bench() for benchmarking
// 主函数入口
// 它分解所有的入参后 构建HTTP请求
// 然后跳转到bench()函数开始性能测试
int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)
 {
	  usage();
          return 2;
 } 

 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   // case 0 means it's a long option with 3rd pointer set (i.e. 4th flag put into pointer address)
   // case 0 代表这是一个long option并且第三位置的指针不为空 (即 第四个flag的值被放入指针所指位置)
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   case 't': benchtime=atoi(optarg);break;	     
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');
	     proxyhost=optarg;
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 // build the HTTP request based on the querying URL
 // 根据给定的URL来构建HTTP请求
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}

// Build the whole HTTP Request and config
// based on the querying URL
// 根据用户给出的web server的URL地址和配置
// 构建出全局唯一一个HTTP请求的字符串buffer
void build_request(const char *url)
{
  char tmp[10];
  int i;

  // request is a 2048-byte long char array to be built up http request
  // request 是一个长度为2048字节的char array来构建http request
  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  // First line of HTTP request will involve: Method + resource location + HTTP Version
  // HTTP 请求的第一行要包括: 方法 + 资源名 + HTTP版本号

  // The HTTP Method
  // HTTP请求方法
  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");

  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  // if the url is in the form of https://xxxxx.com:80/...
  // i will be positioned at the first x after the ://
  // 如果url是以这个形式https://xxxx.com:80/...出现的
  // i的位置就是在://后的第一个x
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
       // strip out the http protocol from left and port number from right, get host address
       // 把左侧的http协议头和右侧的端口号都去掉, 截取出服务器地址
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   bzero(tmp,10);
       // get the port number, by default is 80
       // 截取出端口号, 默认为80
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else
   {
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // strip out the resource locator to the HTTP request header line
   // 把所请求的资源名截取出来放在http请求头行里
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);
  }
  // complete the http request header line by appending version num
  // 以http版本号来结束http请求头行
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  // a few HTTP headers
  // 一些HTTP头
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}

// the control loop procedure for parent-child forking and harvesting
// 父子进程的复制收割控制流 函数
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);
  /* create pipe */
  // build a pipe for Inter Process Communication, mypipe[0] for read, [1] for write
  // 搭建一个pipe来跨进程沟通, mypipe[0]用来读, [1]用来写
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  // here fork many concurrent client process to query the web server
  // and send back the benchmarking result to the parent process to collect through the pipe
  // 这里fork出去很多并行客户端去向服务器发出请求
  // 并把测试的结果通过pipe写会父进程来集合结果
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }

  if( pid< (pid_t) 0)
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)
  {
    /* I am a child */
    if(proxyhost==NULL)
      // single client benchmarking the server
      // 单客户端向服务器发请求进行性能测试
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

     // child client process write results to parent through mypipe[1]
     // 子进程打开mypipe[1]的写端
	 f=fdopen(mypipe[1],"w");
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }

     // send the benchmark result back to parent process
     // 把测试性能结果发送回父进程
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
	 fclose(f);
	 return 0;
  } else
  {
      // parent process open the reader side of the pipe
      // 父进程打开mypipe[0]读端
	  f=fdopen(mypipe[0],"r");
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);
	  speed=0;
          failed=0;
          bytes=0;

	  while(1)
	  {
          // harvest benchmarking results from all concurrent children
          // 关闭所有的并发客户端请求子进程并收获性能测试的结果
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  if(--clients==0) break;
	  }
	  fclose(f);

  // print the final result to stdout
  // 打印出最终结果到终端
  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}

// single client's real benchmarking func to server
// 单个客户端进程向服务器请求的性能测试函数
void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];
 int s,i;
 struct sigaction sa;

 // here we set the handler for SIGALARM, in which it changes the flag 'timerexpired'
 // which will alarm after a period of time specified
 // 这里我们为SIGALARM这个信号设置handler, 触发后会修改'timerexpired'这个变量
 // 在一段时间后alarm会响
 sa.sa_handler=alarm_handler;
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 // tell the OS to send SIGALARM to us after 'benchtime' seconds
 // 让OS在'benchtime'秒后给我们发送一个SIGALARM信号
 alarm(benchtime);

 rlen=strlen(req);
 nexttry:while(1)
 {
     // if flag is changed, then the benchmark time is overdue and should return
     // 如果flag被修改了, 说明测试时间到期了, 子进程可以返回了
    if(timerexpired)
    {
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
    // build the socket TCP connection with server, and send HTTP request
    // 和服务器搭建套接字的TCP连接, 并发送HTTP请求
    s=Socket(host,port);                          
    if(s<0) { failed++;continue;} 
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
    if(http10==0) 
	    if(shutdown(s,1)) { failed++;close(s);continue;}
    if(force==0) 
    {
            /* read all available data from socket */
	    while(1)
	    {
              if(timerexpired) break; 
	      i=read(s,buf,1500);
              /* fprintf(stderr,"%d\n",i); */
	      if(i<0) 
              { 
                 failed++;
                 close(s);
                 goto nexttry;
              }
	       else
               // i read 0 means no more reply from server, could finish this socket connection
               // i read返回0表示服务器端不会有更多的回复了, 可以关闭这个套接字连接了
		       if(i==0) break;
		       else
			       bytes+=i;
	    }
    }
    if(close(s)) {failed++;continue;}
    // speed represents how many successful http request-reply has been made
    // speed 数量代表了多少成功的http请求-回复已经发生
    speed++;
 }
}
