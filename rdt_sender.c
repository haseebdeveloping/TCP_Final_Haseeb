#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>



#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  1500 //milli second 
#define CWND 10

int next_seqno=0;
int send_base=0;
//int window_base;
int window_size = 1;
int unackPackets;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
tcp_packet *cwnd_buffer[10];
sigset_t sigmask;    


typedef struct node_packet{
    tcp_packet *packet;
    struct node_packet *nextPacket;
} node_packet;

node_packet *cwnd_begin = 0;   
node_packet *cwnd_end;

/* .pushes a packet onto the cwnd lifo. Because the
    cwnd is a fixed size window, it is our
    responsibility to ensure that we don't cross
    the cwnd boundary.
*/
void push_packet(tcp_packet *packet){
    if (cwnd_begin == 0){
            cwnd_begin = malloc(sizeof(node_packet));
            cwnd_begin-> packet = packet;
            cwnd_begin-> nextPacket = 0;
            cwnd_end = cwnd_begin;
    }
    else{
        cwnd_end -> nextPacket = malloc(sizeof(node_packet));
        cwnd_end = cwnd_end -> nextPacket;
        cwnd_end -> packet = packet;
        cwnd_end -> nextPacket = 0;
    }
    
    
}

/* pop frees the address pointed to by begin and reassigns it
    to the next element in LIFO.
    */
void pop_packet(){
    if (cwnd_begin == 0){
        return;
    }
    else{
        node_packet *temp = cwnd_begin;
        cwnd_begin = cwnd_begin -> nextPacket;
        free(temp);
        return;
    }
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        if(sendto(sockfd, sndpkt, sizeof(*sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timeer
 * delay: delay in milli seconds
 * sig_handler: signal handler function for resending unacknoledge packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

/* Sleep for given number of seconds. Can be interrupted by
 singnals.
*/
void waitFor (unsigned int secs) {
    unsigned int retTime = time(0) + secs;   // Get finishing time.
    while (time(0) < retTime);               // Loop until it arrives.
}

int main (int argc, char **argv)
{
    int portno, len, fileEnd = 0;
    int next_seqno;
    int i, j;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    while (1)
    {   
        if (fileEnd == 1){
            break; //. We shouldn't and needn't do anything else. 
        }

        // . fill the cwnd buffer.]
        unackPackets = 0;
        //window_base = next_seqno;

        for (i = 0; i < CWND; i++){
            len = fread(buffer, 1, DATA_SIZE, fp);
            if ( len <= 0)
            {
            VLOG(INFO, "End Of File has been reached");
            sndpkt = make_packet(0);
            cwnd_buffer[i] = sndpkt;
            push_packet(sndpkt);
            unackPackets += 1;

            if (unackPackets == 1){
                fileEnd = 1;
            }
            //sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
            //        (const struct sockaddr *)&serveraddr, serverlen);
            break;
            }

            send_base = next_seqno;
            next_seqno = send_base + len;
            
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = send_base;
            cwnd_buffer[i] = sndpkt;
            push_packet(sndpkt);
            unackPackets += 1;
        }
        //Wait for ACK

        //waitFor(1); // . Wait for a given number of seconds between every trans.

        
        //realease all the packets.
        /*
        for (j=0; j<unackPackets; j++){
            VLOG(DEBUG, "Sending packet %d with size %lu to %s", 
                j, sizeof(*(cwnd_buffer[j])), inet_ntoa(serveraddr.sin_addr));


            if(sendto(sockfd, cwnd_buffer[j], sizeof(*(cwnd_buffer[j])), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
        }
        
        */
    for (j=0; j<unackPackets; j++){
            VLOG(DEBUG, "Sending packet %d with size %lu to %s", 
                j, sizeof(*(cwnd_begin -> packet)), inet_ntoa(serveraddr.sin_addr));


            if(sendto(sockfd, cwnd_begin -> packet, sizeof(*(cwnd_begin -> packet)), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            pop_packet();
        }

        
        while (unackPackets != 0){
            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;

            // .Simple print statement to expose every ACK
            
            if (recvpkt->hdr.ackno < send_base){
                VLOG(DEBUG, "DOUBLE ACK seqno.");
                continue;
            }
            VLOG(DEBUG, "ACKed packet with seq no. %d", recvpkt->hdr.ackno);
            --unackPackets;
        }
        

        /*
        do {
            VLOG(DEBUG, "Sending packet %d to %s", 
                    send_base, inet_ntoa(serveraddr.sin_addr));


            if(sendto(sockfd, cwnd_buffer[9], sizeof(*(cwnd_buffer[9])), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            start_timer();

 //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                        //struct sockaddr *src_addr, socklen_t *addrlen);


            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;

            // .Simple print statement to expose every ACK
            

            VLOG(DEBUG, "ACKed packet with seq no. %d", recvpkt->hdr.ackno);

            stop_timer();

            // resend pack if dont recv ack 
        } while(recvpkt->hdr.ackno != next_seqno);

        free(sndpkt);
        

    */
        

    }

    return 0;

}



