// #include "my402list.h"
#include <math.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h> 

#include <pthread.h>
#include <signal.h>
#include "my402list.h"
#include <unistd.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

pthread_t packet_thread;
pthread_t token_thread;
pthread_t server1_thread;
pthread_t server2_thread;

sigset_t set;

int time_to_quit=0;

long int token_inter_arrival_time_milli=0;
int bucket_cap=0;
int number_of_packets_for_processing=0;
long int packet_inter_arrival_time_milli=0;
int tokens_needed=0;
long int service_time_milli=0;

int tokens_in_bucket=0;
int current_token_number=0;

int number_of_packets_dropped=0;
int number_of_packets_arrived=0;

struct timeval emulation_begins;
double emulation_begins_sec=0;

int packet_thread_exited=0;


int mode=0;


typedef struct tagMyPacket {
    // struct timespec arrivas, enters_q1, leaves_q1, enters_q2, leaves_q2, begins_service, departs_server;
    double arrives, enters_q1, leaves_q1, enters_q2, leaves_q2, begins_service, departs_server;
    double measured_inter_token_arrival_time_milli;
    int packet_number;
    int server_number;
    int no_tokens_required;
    long int service_time_milli;
    double packet_inter_arrival_time_milli;
    long int inter_token_arrival_time_milli;
} MyPacket;


typedef struct {
  My402List* q1;
  My402List* q2;
  FILE * fp;
} Thread_args;

typedef struct {
  double average_packet_inter_arrival_time_numerator;
  double average_packet_inter_arrival_time_denominator;
  double average_packet_inter_arrival_time;

  double average_packet_service_time_numerator;
  double average_packet_service_time_denominator;
  double average_packet_service_time;

  double average_packets_q1_numerator;
  double average_packets_q1_denominator;
  double average_packets_q1;

  double average_packets_q2_numerator;
  double average_packets_q2_denominator;
  double average_packets_q2;

  double average_packets_s1_numerator;
  double average_packets_s1_denominator;
  double average_packets_s1;

  double average_packets_s2_numerator;
  double average_packets_s2_denominator;
  double average_packets_s2;

  double average_packet_system_time_numerator;
  double number_completed_service;
  double average_packet_system_time;

  double sum_of_squares_of_packet_system_time;
  
  double system_time_variance;
  double system_time_standard_deviation;

  double number_of_packets_dropped;
  double total_number_of_packets_arrived;
  double packet_drop_probability;

  double number_of_tokens_dropped;
  double total_number_of_tokens_generated;
  double token_drop_probability;

} Stats;

Stats stats={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

double log_time()
{
    struct timeval log_timestamp_time;
    gettimeofday(&log_timestamp_time, 0);
    double log_timestamp_seconds;
    log_timestamp_seconds=log_timestamp_time.tv_sec- emulation_begins.tv_sec;
    double log_timestamp_useconds;
    log_timestamp_useconds=log_timestamp_time.tv_usec-emulation_begins.tv_usec;
    double log_timestamp_milli_time;
    log_timestamp_milli_time=((log_timestamp_seconds)*1000+log_timestamp_useconds/1000.0);
    return log_timestamp_milli_time;
}

// double log_time()
// {
//     struct timeval log_timestamp_time;
//     double log_timestamp_time_sec=0;
//     gettimeofday(&log_timestamp_time, 0);
//     log_timestamp_time_sec= (double)log_timestamp_time.tv_sec+ (double)(((double)log_timestamp_time.tv_usec)/1000000);
//     double log_timestamp=(log_timestamp_time_sec-emulation_begins_sec)*1000;
    
//     return log_timestamp;
// }

void *packet_function(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
    Thread_args *thread_args=(Thread_args*) arg;
    double previous_arrival_milli=0;
    char recordBuffer[1025];
    const char delim[]="\t ";
    for(;;)
    {
        if(number_of_packets_arrived==number_of_packets_for_processing)
        {
            break;
        }

        if(strlen(recordBuffer)==1024 && recordBuffer[1023]!='\n')
        {
            printf("Error: The line is longer than 1,024 characters\n");
            exit(1);
        }

        double current_inter_arrival_time, current_tokens_needed, current_service_time;
        if(mode==0)
        {
            current_inter_arrival_time=packet_inter_arrival_time_milli;
            current_service_time=service_time_milli;
            current_tokens_needed=tokens_needed;
        }   
        else
        {
            char * field1;
            char * field2;
            char * field3;
            fgets(recordBuffer,1024,thread_args->fp);
            field1=strtok(recordBuffer,delim);
            field2=strtok(NULL,delim);
            field3=strtok(NULL,delim);
            current_inter_arrival_time=atoi(field1);
            current_tokens_needed=atoi(field2);
            current_service_time=atoi(field3);
        }

        struct timeval current_time;
        double current_time_sec=0;
        gettimeofday(&current_time, 0);
        current_time_sec= (double)current_time.tv_sec+ (double)(((double)current_time.tv_usec)/1000000);

        double delta_milli=(current_time_sec-emulation_begins_sec)*1000;
        if(current_inter_arrival_time>delta_milli-previous_arrival_milli)
        {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
            usleep((current_inter_arrival_time-(delta_milli-previous_arrival_milli))*1000);
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
        }

        MyPacket* myPacket;
        myPacket=(MyPacket*)malloc(sizeof(MyPacket));
        myPacket->no_tokens_required=current_tokens_needed;
        myPacket->service_time_milli=current_service_time;
        pthread_mutex_lock(&mutex);
        if(time_to_quit==1)
        {
            pthread_mutex_unlock(&mutex);
            return NULL;
        }
        myPacket->arrives=log_time();
        myPacket->measured_inter_token_arrival_time_milli=myPacket->arrives-previous_arrival_milli;
        previous_arrival_milli=myPacket->arrives;
        number_of_packets_arrived=number_of_packets_arrived+1;
        myPacket->packet_number=number_of_packets_arrived;
        stats.total_number_of_packets_arrived+=1;
        if(current_tokens_needed>bucket_cap)
        {
            number_of_packets_dropped=number_of_packets_dropped+1;

            printf("%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms, dropped\n",myPacket->arrives,myPacket->packet_number, myPacket->no_tokens_required, myPacket->measured_inter_token_arrival_time_milli);
            stats.number_of_packets_dropped+=1;
            stats.average_packet_inter_arrival_time_numerator+=myPacket->measured_inter_token_arrival_time_milli;
            stats.average_packet_inter_arrival_time_denominator+=1;
        }
        else
        {
            printf("%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n",myPacket->arrives,myPacket->packet_number,myPacket->no_tokens_required,myPacket->measured_inter_token_arrival_time_milli);
            stats.average_packet_inter_arrival_time_numerator+=myPacket->measured_inter_token_arrival_time_milli;
            stats.average_packet_inter_arrival_time_denominator+=1;
            (void)My402ListAppend(thread_args->q1,myPacket);
            
            myPacket->enters_q1=log_time();
            printf("%012.3fms: p%d enters Q1\n",myPacket->enters_q1,myPacket->packet_number);

            if(thread_args->q1->num_members==1 && myPacket->no_tokens_required<=tokens_in_bucket)
            {
                My402ListElem *elem=NULL;

                elem=My402ListFirst(thread_args->q1);
                MyPacket *myOutPacket=NULL;
                myOutPacket=(MyPacket*) elem->obj;

                My402ListUnlink(thread_args->q1,elem);
                    
                myOutPacket->leaves_q1=log_time();
                printf("%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has 0 token\n",myOutPacket->leaves_q1, myOutPacket->packet_number, myOutPacket->leaves_q1 - myOutPacket->enters_q1);
                stats.average_packets_q1_numerator+=myOutPacket->leaves_q1 - myOutPacket->enters_q1;

                (void)My402ListAppend(thread_args->q2,myOutPacket);

                myOutPacket->enters_q2=log_time();
                printf("%012.3fms: p%d enters Q2\n",myOutPacket->enters_q2, myOutPacket->packet_number);

                tokens_in_bucket-=myOutPacket->no_tokens_required;
            }
            pthread_cond_broadcast(&cv);
        }

        pthread_mutex_unlock(&mutex);
    }
    packet_thread_exited=1;
    // pthread_mutex_lock(&mutex);
    // printf("\n*********************Packet thread exits*****************\n");
    // pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutex);
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mutex);
    return NULL;
}

void * token_function(void* arg)
{
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
    Thread_args *thread_args=(Thread_args*) arg;
    double previous_arrival_milli=0;

    for(;;)
    {
        
        // token_inter_arrival_time_milli
        struct timeval current_time;
        double current_time_sec=0;
        gettimeofday(&current_time, 0);
        current_time_sec= (double)current_time.tv_sec+ (double)(((double)current_time.tv_usec)/1000000);

        double delta_milli=(current_time_sec-emulation_begins_sec)*1000;
        if(token_inter_arrival_time_milli>delta_milli-previous_arrival_milli)
        {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
            usleep((token_inter_arrival_time_milli-(delta_milli-previous_arrival_milli))*1000);
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
        }

        pthread_mutex_lock(&mutex);

        if(packet_thread_exited==1 && thread_args->q1->num_members==0)
        {
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mutex);
            break;
        }

        if(time_to_quit==1)
        {
            pthread_mutex_unlock(&mutex);
            return NULL;
        }
        
        double current_token_arrives=log_time();
        
        previous_arrival_milli=current_token_arrives;
        
        current_token_number+=1;
        stats.total_number_of_tokens_generated+=1;
        if(tokens_in_bucket==bucket_cap)
        {
            printf("%012.3fms: token t%d arrives, dropped\n",current_token_arrives,current_token_number);
            stats.number_of_tokens_dropped+=1;
        }
        else
        {
            tokens_in_bucket+=1;
            printf("%012.3fms: token t%d arrives, token bucket now has %d token\n",current_token_arrives,current_token_number,tokens_in_bucket);

            if(thread_args->q1->num_members!=0)
            {
                My402ListElem *elem=NULL;

                elem=My402ListFirst(thread_args->q1);
                MyPacket *myPacket=NULL;
                myPacket=(MyPacket*) elem->obj;
                if(myPacket->no_tokens_required<=tokens_in_bucket)
                {
                    My402ListUnlink(thread_args->q1,elem);
                    
                    myPacket->leaves_q1=log_time();
                    printf("%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has 0 token\n",myPacket->leaves_q1, myPacket->packet_number, myPacket->leaves_q1 - myPacket->enters_q1);
                    stats.average_packets_q1_numerator+=myPacket->leaves_q1 - myPacket->enters_q1;

                    (void)My402ListAppend(thread_args->q2,myPacket);

                    myPacket->enters_q2=log_time();
                    printf("%012.3fms: p%d enters Q2\n",myPacket->enters_q2, myPacket->packet_number);

                    pthread_cond_broadcast(&cv);
                    tokens_in_bucket-=myPacket->no_tokens_required;
                }
            }
            
        }
        
        pthread_mutex_unlock(&mutex);
    }
    // pthread_mutex_lock(&mutex);
    // printf("\n************************Token exits*******************************\n");
    // pthread_mutex_unlock(&mutex);
    return NULL;
}

void *server1(void* arg)
{
    Thread_args *thread_args=(Thread_args*) arg;
    for(;;)
    {
        pthread_mutex_lock(&mutex);
        if(packet_thread_exited==1 && thread_args->q1->num_members==0 && thread_args->q2->num_members==0)
        {
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mutex);
            break;
        }
        while(thread_args->q2->num_members==0 && !time_to_quit)
        {
            if(packet_thread_exited==1 && thread_args->q1->num_members==0 && thread_args->q2->num_members==0)
            {
                pthread_cond_broadcast(&cv);
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
            pthread_cond_wait(&cv, &mutex);
        }
        if(time_to_quit==1)
        {
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mutex);
            break;
        }
        My402ListElem *elem=NULL;
        elem=My402ListFirst(thread_args->q2);
        MyPacket *myPacket=NULL;
        myPacket=(MyPacket*) elem->obj;
        myPacket->server_number=1;
        My402ListUnlink(thread_args->q2,elem);
        
        myPacket->leaves_q2=log_time();
        printf("%012.3fms: p%d leaves Q2, time in Q2 = %.3fms\n",myPacket->leaves_q2, myPacket->packet_number, myPacket->leaves_q2 - myPacket->enters_q2);
        stats.average_packets_q2_numerator+=myPacket->leaves_q2 - myPacket->enters_q2;

        myPacket->begins_service=log_time();
        printf("%012.3fms: p%d begins service at S1, requesting %ldms of service\n",myPacket->begins_service,myPacket->packet_number,myPacket->service_time_milli);
        
        pthread_mutex_unlock(&mutex);

        usleep(myPacket->service_time_milli*1000);

        pthread_mutex_lock(&mutex);
        myPacket->departs_server=log_time();
        printf("%012.3fms: p%d departs from S1, service time = %.3fms, time in system = %.3fms\n",myPacket->departs_server,myPacket->packet_number,myPacket->departs_server-myPacket->begins_service,myPacket->departs_server-myPacket->arrives);
        stats.average_packets_s1_numerator+=myPacket->departs_server-myPacket->begins_service;

        stats.average_packet_service_time_numerator+=myPacket->departs_server-myPacket->begins_service;
        stats.average_packet_service_time_denominator+=1;

        stats.average_packet_system_time_numerator+=myPacket->departs_server-myPacket->arrives;
        stats.number_completed_service+=1;

        stats.sum_of_squares_of_packet_system_time+=(myPacket->departs_server-myPacket->arrives)*(myPacket->departs_server-myPacket->arrives);
        pthread_mutex_unlock(&mutex);
        
    }
    // pthread_mutex_lock(&mutex);
    // printf("\n*****************************S1 exits**********************\n");
    // pthread_mutex_unlock(&mutex);
    return NULL;
}
void *server2(void* arg)
{
    Thread_args *thread_args=(Thread_args*) arg;
    for(;;)
    {
        pthread_mutex_lock(&mutex);
        if(packet_thread_exited==1 && thread_args->q1->num_members==0 && thread_args->q2->num_members==0)
        {
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mutex);
            break;
        }
        while(thread_args->q2->num_members==0 && !time_to_quit)
        {
            if(packet_thread_exited==1 && thread_args->q1->num_members==0 && thread_args->q2->num_members==0)
            {
                pthread_cond_broadcast(&cv);
                pthread_mutex_unlock(&mutex);
                return NULL;
            }
            pthread_cond_wait(&cv, &mutex);
        }
        if(time_to_quit==1)
        {
            pthread_cond_broadcast(&cv);
            pthread_mutex_unlock(&mutex);
            break;
        }
        My402ListElem *elem=NULL;
        elem=My402ListFirst(thread_args->q2);
        MyPacket *myPacket=NULL;
        myPacket=(MyPacket*) elem->obj;
        myPacket->server_number=1;
        My402ListUnlink(thread_args->q2,elem);
        
        myPacket->leaves_q2=log_time();
        printf("%012.3fms: p%d leaves Q2, time in Q2 = %.3fms\n",myPacket->leaves_q2, myPacket->packet_number, myPacket->leaves_q2 - myPacket->enters_q2);
        stats.average_packets_q2_numerator+=myPacket->leaves_q2 - myPacket->enters_q2;

        myPacket->begins_service=log_time();
        printf("%012.3fms: p%d begins service at S2, requesting %ldms of service\n",myPacket->begins_service,myPacket->packet_number,myPacket->service_time_milli);
        
        // printf("\nService time is: %ld\n",myPacket->service_time_milli);
        pthread_mutex_unlock(&mutex);

        usleep(myPacket->service_time_milli*1000);

        pthread_mutex_lock(&mutex);
        myPacket->departs_server=log_time();
        printf("%012.3fms: p%d departs from S2, service time = %.3fms, time in system = %.3fms\n",myPacket->departs_server,myPacket->packet_number,myPacket->departs_server-myPacket->begins_service,myPacket->departs_server-myPacket->arrives);
        stats.average_packets_s2_numerator+=myPacket->departs_server-myPacket->begins_service;

        stats.average_packet_service_time_numerator+=myPacket->departs_server-myPacket->begins_service;
        stats.average_packet_service_time_denominator+=1;
        
        stats.average_packet_system_time_numerator+=myPacket->departs_server-myPacket->arrives;
        stats.number_completed_service+=1;

        stats.sum_of_squares_of_packet_system_time+=(myPacket->departs_server-myPacket->arrives)*(myPacket->departs_server-myPacket->arrives);
        pthread_mutex_unlock(&mutex);
    }

    // pthread_mutex_lock(&mutex);
    // printf("\n******************************S2 exits**************************\n");
    // pthread_mutex_unlock(&mutex);


    return NULL;
}



void *monitor() {
  int sig;
  while (1)
  {
      sigwait(&set, &sig); 
      pthread_mutex_lock(&mutex); 
      time_to_quit=1;
      pthread_cancel(packet_thread);
      pthread_cancel(token_thread);
      printf("%012.3fms: SIGINT caught, no new packets or tokens will be allowed\n",log_time());
      pthread_cond_broadcast(&cv);
      pthread_mutex_unlock(&mutex);
  }
  return(0);
}
int main(int argc, char *argv[])
{
    sigemptyset(&set);
    sigaddset(&set,SIGINT);
    sigprocmask(SIG_BLOCK,&set, 0);

    char recordBuffer[1025];
    
    FILE* filePointer;

    int B=-1;
    int P=-1;
    int n=-1;
    double lambda=-1, mu=-1, r=-1;
    char* t;

    for(int i=1;i<argc;i++)
    {
        if(strcmp(argv[i],"-lambda")==0)
        {
            lambda=atof(argv[i+1]);
            i=i+1;
            // printf("%f",lambda);
        }
        else if(strcmp(argv[i],"-mu")==0)
        {
            mu=atof(argv[i+1]);
            i=i+1;
            // printf("%f",mu);
        }
        else if(strcmp(argv[i],"-r")==0)
        {
            r=atof(argv[i+1]);
            i=i+1;
            // printf("%f",r);
        }
        else if(strcmp(argv[i],"-B")==0)
        {
            B=atoi(argv[i+1]);
            i=i+1;
            // printf("%d",B);
        }
        else if(strcmp(argv[i],"-n")==0)
        {
            n=atoi(argv[i+1]);
            i=i+1;
            // printf("%d",n);
        }
        else if(strcmp(argv[i],"-P")==0)
        {
            P=atoi(argv[i+1]);
            i=i+1;
            // printf("%d",P);
        }
        else if(strcmp(argv[i],"-r")==0)
        {
            r=atof(argv[i+1]);
            i=i+1;
            // printf("%f",r);
        }
        else if(strcmp(argv[i],"-t")==0)
        {
            mode=1;
            t=argv[i+1];
            i=i+1;
            filePointer=fopen(t,"r");
            if(filePointer==NULL)
            {
                printf("Error while opening the file\n");
                exit(1);
            }
            // printf("%s",t);
        }
        else
        {
            printf("Bad commandline or command\n");
            exit(1);
        }
        
        // printf("\n");
    }

    //Common stuff
    if(r==-1)
    {
        r=1.5;
    }

    if(B==-1)
    {
        B=10;
    }
    
    //Specific stuff
    if(mode==0)
    {
        if(lambda==-1)
        {
            lambda=1;
        }

        if(n==-1)
        {
            n=20;
        }

        if(P==-1)
        {
            P=3;
            
        }
        if(mu==-1)
        {
            mu=0.35;
        }

    }

    //Common things
    bucket_cap=B;
    if(1/r>10)
    {
        token_inter_arrival_time_milli=10000;
    }
    else
    {
        token_inter_arrival_time_milli=(long int)(round((1/r)*1000));
    }

    //Specific stuff
    if(mode==0)
    {
        number_of_packets_for_processing=n;
        tokens_needed=P;

        if(1/lambda>10)
        {
            packet_inter_arrival_time_milli=10000;
        }
        else
        {
            packet_inter_arrival_time_milli=(long int)(round((1/lambda)*1000));
        }

        if(1/mu>10)
        {
            service_time_milli=10000;
        }
        else
        {
            service_time_milli=(long int)(round((1/mu)*1000));
        }
        
    }
    else
    {
        fgets(recordBuffer,1024,filePointer);
        if(strlen(recordBuffer)==1024 && recordBuffer[1023]!='\n')
        {
            printf("Error: The line is longer than 1,024 characters\n");
            exit(1);
        }
        number_of_packets_for_processing=atoi(recordBuffer);
    }
    

    My402List q1, q2;

    memset(&q1, 0, sizeof(My402List));
    memset(&q2, 0, sizeof(My402List));
    (void)My402ListInit(&q1);
    (void)My402ListInit(&q2);


    printf("Emulation Parameters:\n");
    printf("\tnumber to arrive = %d\n",number_of_packets_for_processing);
    if(mode==0)
    {
        printf("\tlambda = %.6g\n",lambda);
        printf("\tmu = %.6g\n",mu);
    }

    printf("\tr = %.6g\n",r);
    printf("\tB = %d\n",B);
    if(mode==0)
    {
        printf("\tP = %d\n",P);
    }

    if(mode==1)
    {
        printf("\ttsfile = %s\n",t);
    }

    printf("\n");
    gettimeofday(&emulation_begins, 0);
    emulation_begins_sec= (double)emulation_begins.tv_sec+ (double)(((double)emulation_begins.tv_usec)/1000000);

    printf("00000000.000ms: emulation begins\n");

    pthread_t canceler_thread;
    pthread_create( &canceler_thread, 0,monitor, 0);

    Thread_args thread_args;
    memset(&thread_args, 0, sizeof(Thread_args));

    thread_args.q1=&q1;
    thread_args.q2=&q2;
    if(mode==1)
    {
        thread_args.fp=filePointer;
    }
    
    bucket_cap=B;

    pthread_create(&token_thread,
    0,
    token_function,&thread_args);

    pthread_create(&packet_thread,
    0,
    packet_function,&thread_args);

    pthread_create(&server1_thread,
    0,
    server1,&thread_args);

    pthread_create(&server2_thread,
    0,
    server2,&thread_args);

    pthread_join(packet_thread,0);
    pthread_join(token_thread,0);
    pthread_join(server1_thread,0);
    pthread_join(server2_thread,0);


    while(q1.num_members!=0)
    {
        My402ListElem *elem=NULL;

        elem=My402ListFirst(&q1);
        MyPacket *myPacket=NULL;
        myPacket=(MyPacket*) elem->obj;
        My402ListUnlink(&q1,elem);
        
        printf("%012.3fms: p%d removed from Q1\n",log_time(), myPacket->packet_number);
    }

    while(q2.num_members!=0)
    {
        My402ListElem *elem=NULL;

        elem=My402ListFirst(&q2);
        MyPacket *myPacket=NULL;
        myPacket=(MyPacket*) elem->obj;
        My402ListUnlink(&q2,elem);
        
        printf("%012.3fms: p%d removed from Q2\n",log_time(), myPacket->packet_number);
    }

    double emulation_end_time=log_time();
    printf("%012.3fms: emulation ends\n",emulation_end_time);

    printf("\nStatistics:\n\n");

    stats.average_packets_q1_denominator=emulation_end_time;
    stats.average_packets_q2_denominator=emulation_end_time;
    stats.average_packets_s1_denominator=emulation_end_time;
    stats.average_packets_s2_denominator=emulation_end_time;

    stats.average_packet_inter_arrival_time=stats.average_packet_inter_arrival_time_numerator/stats.average_packet_inter_arrival_time_denominator;
    stats.average_packet_service_time=stats.average_packet_service_time_numerator/stats.average_packet_service_time_denominator;
    
    if(stats.average_packet_inter_arrival_time_denominator!=0)
    {
        printf("\taverage packet inter-arrival time = %.6g\n",stats.average_packet_inter_arrival_time/1000);
    }
    else
    {
        printf("\taverage packet inter-arrival time = N/A no packet arrived\n");
    }

    if(stats.average_packet_service_time_denominator!=0)
    {
        printf("\taverage packet service time = %.6g\n",stats.average_packet_service_time/1000);
    }
    else
    {
        printf("\taverage packet service time = N/A no packet was served\n");
    }

    printf("\n");

    if(stats.average_packets_q1_denominator!=0)
    {
        stats.average_packets_q1=stats.average_packets_q1_numerator/stats.average_packets_q1_denominator;
        printf("\taverage number of packets in Q1 = %.6g\n",stats.average_packets_q1);
    }
    else
    {
        printf("\taverage number of packets in Q1 = N/A emulation duration is zero\n");
    }

    if(stats.average_packets_q2_denominator!=0)
    {
        stats.average_packets_q2=stats.average_packets_q2_numerator/stats.average_packets_q2_denominator;
        printf("\taverage number of packets in Q2 = %.6g\n",stats.average_packets_q2);
    }
    else
    {
        printf("\taverage number of packets in Q2 = N/A emulation duration is zero\n");
    }
    
    if(stats.average_packets_s1_denominator!=0)
    {
        stats.average_packets_s1=stats.average_packets_s1_numerator/stats.average_packets_s1_denominator;
        printf("\taverage number of packets at S1 = %.6g\n",stats.average_packets_s1);
    }
    else
    {
        printf("\taverage number of packets at S1 = N/A emulation duration is zero\n");
    }
    
    if(stats.average_packets_s2_denominator!=0)
    {
        stats.average_packets_s2=stats.average_packets_s2_numerator/stats.average_packets_s2_denominator;
        printf("\taverage number of packets at S2 = %.6g\n",stats.average_packets_s2);
    }
    else
    {
        printf("\taverage number of packets at S2 = N/A emulation duration is zero\n");
    }
    
    printf("\n");

    if(stats.number_completed_service!=0)
    {
        stats.average_packet_system_time=stats.average_packet_system_time_numerator/stats.number_completed_service;
        stats.system_time_variance=(stats.sum_of_squares_of_packet_system_time/stats.number_completed_service)-pow(stats.average_packet_system_time,2);
        stats.system_time_standard_deviation=sqrt(stats.system_time_variance);

        printf("\taverage time a packet spent in system = %.6g\n",stats.average_packet_system_time/1000);
        printf("\tstandard deviation for time spent in system = %.6g\n",stats.system_time_standard_deviation/1000);

    }
    else
    {
        printf("\taverage time a packet spent in system = N/A no packet was served\n");
        printf("\tstandard deviation for time spent in system = N/A no packet was served\n");
    }
    
    printf("\n");

    if(stats.total_number_of_tokens_generated!=0)
    {
        stats.token_drop_probability=stats.number_of_tokens_dropped/stats.total_number_of_tokens_generated;
        printf("\ttoken drop probability = %.6g\n",stats.token_drop_probability);
    }
    else
    {
        printf("\ttoken drop probability = N/A no token was generated\n");
    }

    if(stats.total_number_of_packets_arrived!=0)
    {
        stats.packet_drop_probability=stats.number_of_packets_dropped/stats.total_number_of_packets_arrived;
        printf("\tpacket drop probability = %.6g\n",stats.packet_drop_probability);
    }
    else
    {
        printf("\tpacket drop probability = N/A no packet arrived\n");
    }
    
    if(mode!=0)
    {
        fclose(filePointer);
    }
    return(0);
}