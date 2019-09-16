#include <stdio.h> 
#include <stdlib.h> 
#include <errno.h> 
#include <string.h> 
#include <netdb.h> 
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>


#define BUFFER 1024
#define DEFAULT_PORT 12345
#define h_addr h_addr_list[0] 

uint16_t PORT;
char *ADDRESS;

void signal_handler(int signum);

int sockfd;
struct hostent *he;
struct sockaddr_in their_addr; /* connector's address information */
int recv_bytes;
char socksend[BUFFER];
char sockrecv[BUFFER];

int main(int argc, char *argv[]) {

    printf("\n\nConnecting...\n\n");

    /* Get port number for server to listen on */
    switch (argc) {
        case 1:
            printf("\nfew arguments provided!\n");
            exit(0);
        case 2:
            printf("\nfew arguments provided!\n");
            exit(0);
            break;
        case 3:
            ADDRESS = argv[1];
            PORT = atoi(argv[2]);
            break;
        default:
            perror("\ntoo many arguments!\n\n");
            exit(0);
    }

	if (argc != 3) {
		fprintf(stderr,"usage: client_hostname port_number\n");
		exit(0);
	}

	if ((he = gethostbyname(argv[1])) == NULL) {  /* get the host info */
		herror("gethostbyname");
		exit(0);
	}

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(0);
	}

	their_addr.sin_family = AF_INET;      /* host byte order */
	their_addr.sin_port = htons(atoi(argv[2]));    /* short, network byte order */
	their_addr.sin_addr = *((struct in_addr *)he->h_addr);
	bzero(&(their_addr.sin_zero), 8);     /* zero the rest of the struct */

	if (connect(sockfd, (struct sockaddr *)&their_addr, sizeof(struct sockaddr)) == -1) {
		perror("connect");
		exit(0);
	}

    while (1) {

        signal(SIGINT, signal_handler);

        while ((recv_bytes = read(sockfd, sockrecv, BUFFER)) > 0) {
            sockrecv[recv_bytes] = '\0';
            // exit
            if (strstr(sockrecv, "exitgame") != NULL) {
                printf("\n\nThanks for playing...\nSee you later!\n\n");
                exit(0);
            }
            // clear screen
            else if (strstr(sockrecv, "clearscreen") != NULL) {
                system("clear");
            } 
            else {
                // print the message
                printf("%s", sockrecv);
            }
            
            // if the serve need input from the client
            if (strstr(sockrecv, ": ") != NULL) {
                scanf("%s", socksend);
                fflush(stdin);
                write(sockfd, socksend, strlen(socksend));
            }
        }
        printf("\noff!\n\n\n\n");
        exit(0);
    }
    close(sockfd);

	return 0;
}

void signal_handler(int signum) {
    printf("\n\nctrl-c (SIGINT)\n");
    // Terminate program
	exit(signum);
}

