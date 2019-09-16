#ifndef __APPLE__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#ifdef __APPLE__
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
pthread_mutex_t scores_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#else
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_mutex_t scores_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

/* global condition variable for our program. assignment initializes it. */
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

#define BUFFER 1024
#define MAX_USERS 10
#define BACKLOG 10
#define DEFAULT_PORT 12345
#define GRID_SIZE 10
#define NUM_TILES 9
#define NUM_MINES 10
#define RANDOM_NUMBER_SEED 42

/* Tile - Play field structure */
typedef struct {
    char cover;
    int adjacent_mines;
    bool revealed;
    bool is_mine;
    bool is_flag;
} Tile;

/* User Structure */
typedef struct user {
    char username[BUFFER];
    char password[BUFFER];
    int games_won;
    int games_played;
    int best_time_achieved;
    int mines_left;
    bool exit_flag;
    bool game_over;
    Tile tiles[NUM_TILES][NUM_TILES];
} user;
user users[MAX_USERS];

/* Global Variables */
uint16_t PORT;
int current_num_of_clients = 0;
int *client_list;
int sockfd;
char *exit_msg = "exitgame";
char *clear_msg = "clearscreen";
bool leaderBoard_empty = true; // when someone win, make it false;

int arr_NUM[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
char arr_char[] = {'A','B','C','D','E','F','G','H','I'};
char arr_charNUM[] = {'1','2','3','4','5','6','7','8','9'};

int num_requests = 0; /* number of pending requests, initially none */
struct request { /* format of a single request. */
    int number; /* number of the request*/
    int socket_id;	
    struct request* next; /* pointer to next request, NULL if none. */ 
};
struct request* requests = NULL; /* head of linked list of requests. */
struct request* last_request = NULL; /* pointer to last request. */

/* FUNCTION DECLARATIONS */
void signal_handler(int signum);
void add_request(int request_num, int new_fd, pthread_mutex_t* p_mutex, pthread_cond_t*  p_cond_var);
struct request* get_request(pthread_mutex_t* p_mutex);
void handle_request(struct request* a_request, int thread_id);
void* handle_requests_loop(void* data);
void credentials(); 
void play(int new_fd);
int authenticate(int new_fd, char *username, char *password);
void mainMenu(int new_fd, int userIndex);
void Minesweeper(int new_fd, int userIndex);
void leaderBoard(int new_fd, int userIndex);
char add(char *num1, char *num2);
int asciiValue(char *s);
void close_client(int new_fd);
void exit_game(int new_fd);
void clear_screen(int new_fd);
int xcoord(char xcoord);
int ycoord(char ycoord);
void setup_game(int new_fd, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]);
void reveal(int new_fd, int x, int y, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]);
void flag(int new_fd, int x, int y, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]);
void reveal_mines(Tile tiles[NUM_TILES][NUM_TILES]);
int adjacent_mines(int x, int y, Tile tiles[NUM_TILES][NUM_TILES]);
void place_mines(Tile tiles[NUM_TILES][NUM_TILES]);
bool already_mined(int x, int y, Tile tiles[NUM_TILES][NUM_TILES]);
void build_diaplayBoard(int new_fd, int userIndex, Tile display_board[NUM_TILES][NUM_TILES]);
int compUser(const void* a, const void* b);

/* MAIN */
int main(int argc, char *argv[]) {

    srand(RANDOM_NUMBER_SEED);

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
		printf("\nsignal handler\n");
		return EXIT_FAILURE;
	}
    credentials();
    int new_fd;
	struct sockaddr_in my_addr;    /* my address information */
	struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;
    int thr_id[BACKLOG];      /* thread IDs            */
    pthread_t p_threads[BACKLOG];   /* thread's structures   */
    client_list = malloc(sizeof(int)*MAX_USERS);
    int counter = 0;

    switch (argc) {
        case 1:
            PORT = DEFAULT_PORT;
            break;
        case 2:
            PORT = atoi(argv[1]);
            break;
        default:
            printf("\ntoo many arguments!\n\n");
            exit(0);
    }

    if (PORT < 1 || PORT > 65535) {
        printf("PORT %d is Invalid, Please provide a port between 1 and 65535", PORT);
        return 1;
    }

    /* generate the socket */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(0);
	}

    /* generate the end point */
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons((PORT));
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* bind the socket to the end point */
	if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
		perror("\nbind\n");
		exit(0);
	}

    /* start listnening */
    if (listen(sockfd, 3)) {
        perror("\nlisten\n");
        return -1;
    }

    printf("server starts listnening on port %d...\n", PORT);

    for (int i = 0; i < BACKLOG; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop, (void *) &thr_id[i]);
    }
    printf("\n\nnumber of threads created: %d\n\n", BACKLOG);

    /* repeat: accept, send, close the connection */
    while (1) {
        if (current_num_of_clients < MAX_USERS) {
            sin_size = sizeof(struct sockaddr_in);
            if ((new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size)) == -1) {
                perror("\naccept\n");
                continue;
            }
            if (counter < MAX_USERS) {
                client_list = (int*)realloc(client_list, sizeof(int)* MAX_USERS);
                counter += 1;
		    }
		    current_num_of_clients += 1;
		    client_list[current_num_of_clients] = new_fd;
            printf("socketID: %s is connected\n", inet_ntoa(their_addr.sin_addr));
            add_request(current_num_of_clients, new_fd, &request_mutex, &got_request);
            printf("current number of clients: %d\n", current_num_of_clients);
        }
    }
    free(client_list);
    shutdown(sockfd, 2);
    return 0;
}

/*
 * function signal_handler(): handle Ctrl + C
 * input:     signal
 * output:    clean threads, free client list, shutdown server.
 */
void signal_handler(int signum) {
	printf("\n\nctrl-c (SIGINT)\n");
	if ( current_num_of_clients >= 0 ) {
		for (int i = 0; i <= current_num_of_clients; i++) {
			printf("closing thread: %d - id: %d\n", i, client_list[i]);
			close(client_list[i]);
		}
	}
	/* Cleanup and close up stuff here  */
	// 1) dynmic memory
	free(client_list);
	// 2) close the server socket
	printf("server socket is closed.\n\n");
	close(sockfd);
	// Terminate program
	exit(signum);
}

/*
 * function add_request(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    none.
 */
void add_request(int request_num, int new_fd, pthread_mutex_t* p_mutex, pthread_cond_t*  p_cond_var) {
    struct request* a_request = (struct request*)malloc(sizeof(struct request));

    if (!a_request) { /* malloc failed?? */
        fprintf(stderr, "add_request: out of memory\n");
        exit(1);
    }
    a_request->number = request_num;
    a_request->socket_id = new_fd;
    a_request->next = NULL;
    pthread_mutex_lock(p_mutex);

    if (num_requests == 0) { /* special case - list is empty */
        requests = a_request;
        last_request = a_request;
    }
    else {
        last_request->next = a_request;
        last_request = a_request;
    }
    num_requests++;
    pthread_mutex_unlock(p_mutex);
    pthread_cond_signal(p_cond_var);
}

/*
 * function get_request(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
struct request* get_request(pthread_mutex_t* p_mutex) {
    struct request* a_request;      /* pointer to request.                 */
    pthread_mutex_lock(p_mutex);
    if (num_requests > 0) {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL) { /* this was the last request on the list */
            last_request = NULL;
        }
        num_requests--;
    }
    else { /* requests list is empty */
        a_request = NULL;
    }
    pthread_mutex_unlock(p_mutex);
    return a_request;
}

/*
 * function handle_request(): handle a single given request.
 * algorithm: prints a message stating that the given thread handled
 *            the given request.
 * input:     request pointer, id of calling thread.
 * output:    none.
 */
void handle_request(struct request* a_request, int thread_id) {
    if (a_request) {
        printf("Thread '%d' handled request '%d'\n\n", thread_id, a_request->number);
        fflush(stdout);
        play(a_request->socket_id);
    }
}

void* handle_requests_loop(void* data) {
    struct request* a_request;      /* pointer to a request.               */
    int thread_id = *((int*)data);  /* thread identifying number           */
    pthread_mutex_lock(&request_mutex);
    while (1) {

        if (num_requests > 0) { /* a request is pending */
            a_request = get_request(&request_mutex);
            if (a_request) { /* got a request - handle it and free it */
                pthread_mutex_unlock(&request_mutex);
                handle_request(a_request, thread_id);
                free(a_request);
                pthread_mutex_lock(&request_mutex);
            }
        }
        else {
            pthread_cond_wait(&got_request, &request_mutex);
        }
    }
}

/*
 * function credentials(): Read the Authentication.txt and store
 *                         the username and passwords in user 
 *                         struct
 * algorithm: Use standard C File handling 
 * input:     Authentication.txt
 * output:    user.
 */
void credentials() {
    char username[BUFFER];
    char password[BUFFER];
    FILE *auth = fopen("Authentication.txt", "r");
    fscanf(auth, "%s %s",username ,password);
    int i = 0;
	while (!feof(auth) && fscanf(auth, "%s %s", username, password) == 2) {
        strncpy(users[i].username, username, BUFFER);
		strncpy(users[i].password, password, BUFFER);
        i++;
	};
    fclose(auth);
};

/*
 * function play(): Game Entry, User Validation
 * input:     User name and passwrods from client.
 * output:    Display main menu if authenticateds.
 */
void play(int new_fd) {

    clear_screen(new_fd);
    char username[BUFFER];
    memset(username, 0, BUFFER);
    char password[BUFFER];
    memset(password, 0, BUFFER);
    bool disconnected = false;
    char *welcome_message = 
            "\n\n========================================================================\n\n"
			"           Welcome to the online Minesweeper gaming system\n"
            "\n========================================================================\n\n"
			"You are required to log on with your registered username and password.\n\n";
    write(new_fd, welcome_message, strlen(welcome_message));

    char *username_message = "Username: ";
    write(new_fd, username_message, strlen(username_message));
    if ((read(new_fd, username, sizeof(username))) == 0) {
        disconnected = true;
        close_client(new_fd);
    }

    char *passowrd_message = "Password: ";
    write(new_fd, passowrd_message, strlen(passowrd_message));
    if ((read(new_fd, password, sizeof(password))) == 0) {
        if (!disconnected) {
            disconnected = true;
            close_client(new_fd);
        }   
    }

    clear_screen(new_fd);
    if (authenticate(new_fd, username, password) > -1) {
            char *welcome_msg = "\n\nWelcome to the Minesweeper gaming system";
            write(new_fd, welcome_msg, strlen(welcome_msg));
            mainMenu(new_fd, authenticate(new_fd, username, password));
    } else {
        char *disconnect_message = "\n\nYou entered either an incorrect username or password - disconnecting.\n";
        write(new_fd, disconnect_message, strlen(disconnect_message));
        if (!disconnected) {
            disconnected = true;
            close_client(new_fd);
        }  
    }
}

/*
 * function authenticate(): Check if the username and password
 *                          passed in the parameter are equal the
 *                          ones stored in users struct.
 * input:     username, password
 * output:    user index in users array.
 */
int authenticate(int new_fd, char *username, char *password) {
    int i;
    for (i = 0; i < MAX_USERS; i++) {
        if (strcmp(username, users[i].username) == 0 && strcmp(password, users[i].password) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * function mainMenu(): Display the main menu and the options
 * input:     user option
 * output:    display the selected option.
 */
void mainMenu(int new_fd, int userIndex) {
    bool disconnected = false;
    char menu_option[BUFFER];
    memset(menu_option, 0, BUFFER);
    char *game_message = 
        "\n\nPlease, enter a selection:"
        "\n<1> Play Minesweeper"
        "\n<2> Show Leaderboard"
        "\n<3> Quit"
        "\n\nSelection option (1-3): ";
    bool invalid = true;
    while (invalid) {
        memset(menu_option, 0, BUFFER);
        write(new_fd, game_message, strlen(game_message));
        if (read(new_fd, menu_option, sizeof(menu_option)) == 0) {
            disconnected = true;
            close_client(new_fd);
            invalid = false;
            break;
        }

        switch (asciiValue(menu_option)) {
            case '1':
                Minesweeper(new_fd, userIndex);
                invalid = false;
                break;
            case '2':
                leaderBoard(new_fd, userIndex);
                invalid = false;
                break;
            case '3':
                if (!disconnected) {
                    exit_game(new_fd);
                    close_client(new_fd);
                    invalid = false;
                    break;
                }   
            default:
                memset(menu_option, 0, BUFFER);
                clear_screen(new_fd);
                if (!disconnected) {
                    write(new_fd, "\n\nPlease make a valid choice\n\n" , strlen("\nPlease make a valid choice\n\n"));
                    invalid = true;
                }
        }
    }
}

/*
 * function Minesweeper(): Do tile revealation and Tile flaging
 * input:     user options, user coordinates input
 * output:    display matrix corresponding to the user input.
 */
void Minesweeper(int new_fd, int userIndex) {

    // timer and number of games played
    int diff = 0;
    time_t start;
    time_t stop;
    time(&start);
    pthread_mutex_lock(&scores_mutex);
    users[userIndex].games_played++; // mutex
    pthread_mutex_unlock(&scores_mutex);

    int i;
    char coord[BUFFER];
    int x, y;

    pthread_mutex_lock(&scores_mutex);
    x = rand() % NUM_TILES;
    y = rand() % NUM_TILES;
    pthread_mutex_unlock(&scores_mutex);

    char game_option[BUFFER];
    memset(game_option, 0, BUFFER);
    bool game_over = false;
    char *winning_msg = "\n\nCongratulations! You have located all the mines.\n\n";
    char *already_reveald_msg = "\nThe Tile is already revealed, Please enter different coordinates\n";
    char time_msg[BUFFER];
    memset(time_msg, 0, BUFFER);

    //TEST
    setup_game(new_fd, userIndex, users[userIndex].tiles);

    do {
        memset(game_option, 0, BUFFER);
        memset(coord, 0, BUFFER);
        char *option_msg = "\n\nChoose an option:\n<R> Reveal tile\n<P> Place flag\n<Q> Quit game\n\nOption (R,P,Q): ";                
        write(new_fd, option_msg, strlen(option_msg));
        if ((read(new_fd, game_option, sizeof(game_option))) == 0) {
            game_over = true;
            close_client(new_fd);
            break;
        }

        switch (asciiValue(game_option)) {
            case 'R':
                //Error Handling needed
                write(new_fd, "Enter the Coordinates: ", strlen("Enter the Coordinates: "));
                if ((read(new_fd, coord, sizeof(coord))) == 0) {
                    game_over = true;
                    close_client(new_fd);
                    break;
                }

                x = xcoord(coord[1]); 
                y = ycoord(coord[0]); 
                //validating x and y
                if (x < 0 || y < 0 || strlen(coord) > 2) {
                    memset(coord, 0, BUFFER);
                    build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                    write(new_fd, "\n\nPlease make a valid choice\n\n" , strlen("\nPlease make a valid choice\n\n"));
                    game_over = false;
                    break;
                }

                if (users[userIndex].tiles[y-1][x-1].revealed) {
                    build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                    write(new_fd, already_reveald_msg, strlen(already_reveald_msg));
                }
                else{
                    reveal(new_fd, x - 1, y - 1, userIndex, users[userIndex].tiles);
    
                    game_over = users[userIndex].exit_flag;

                    if (game_over) {
                        build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                        char *losing_msg = "\n\nGame Over! You hit a mine.\n\n";
                        write(new_fd, losing_msg, strlen(losing_msg));
                        mainMenu(new_fd, userIndex);
                        break;
                    }
                    else {
                        build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                    }
                }
                break;
            case 'P':
                write(new_fd, "Enter the Coordinates: ", strlen("Enter the Coordinates: "));
                if ((read(new_fd, coord, sizeof(coord))) == 0) {
                    game_over = true;
                    close_client(new_fd);
                    break;
                }
                x = xcoord(coord[1]); 
                y = ycoord(coord[0]);
                
                if (x < 0 || y < 0 || strlen(coord) > 2) {
                    memset(coord, 0, BUFFER);
                    build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                    write(new_fd, "\n\nPlease make a valid choice\n\n" , strlen("\nPlease make a valid choice\n\n"));
                    game_over = false;
                    break;
                }
                
                flag(new_fd, x - 1, y - 1, userIndex, users[userIndex].tiles);
                game_over = users[userIndex].exit_flag;
                if (game_over) {
                    leaderBoard_empty = false;
                    pthread_mutex_lock(&scores_mutex);
                    users[userIndex].games_won++; // mutex
                    pthread_mutex_unlock(&scores_mutex);
                    write(new_fd, winning_msg, strlen(winning_msg));
                    time(&stop);
                    diff = difftime(stop, start);
                    if (users[userIndex].games_won == 1) {
                        pthread_mutex_lock(&scores_mutex);
                        users[userIndex].best_time_achieved = diff; // mutex
                        pthread_mutex_unlock(&scores_mutex);
                    }
                    else if (users[userIndex].games_won > 1) {
                        if (users[userIndex].best_time_achieved > diff) {
                            pthread_mutex_lock(&scores_mutex);
                            users[userIndex].best_time_achieved = diff; // mutex
                            pthread_mutex_unlock(&scores_mutex);
                        }  
                    }
                    snprintf(time_msg, sizeof(time_msg), "You have won in %d seconds!", diff);
                    write(new_fd, time_msg, strlen(time_msg));
                    mainMenu(new_fd,userIndex);
                } 
                break;
            case 'Q':
                game_over = true;
                clear_screen(new_fd);
                mainMenu(new_fd, userIndex);
                break;
            default:
                memset(game_option, 0, BUFFER);
                build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
                write(new_fd, "\n\nPlease make a valid choice\n\n" , strlen("\nPlease make a valid choice\n\n"));
                game_over = false;
        }
    } while (!game_over);
}

/*
 * function flag(): change Tile cover to flag and check for win condition. 
 * input:     user optons, user coordiates
 * output:    flag character revealed, Win message.
 */
void flag(int new_fd, int x, int y, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]) {

    char *noMine_msg = "\n\nNo mines! Try again.\n\n";
    char *alreadyFlag_msg = "\n\nAlready flaged! Try another coordinates.\n\n";

    if (tiles[y][x].is_mine && !(tiles[y][x].revealed)) {
        users[userIndex].mines_left--;
        if (users[userIndex].mines_left == 0) {
            users[userIndex].exit_flag = true;
        }
        tiles[y][x].revealed = true;
        tiles[y][x].is_flag = true;
        build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
    }
    else if (tiles[y][x].is_mine && tiles[y][x].revealed) {
        build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
        write(new_fd, alreadyFlag_msg, strlen(alreadyFlag_msg));
         users[userIndex].exit_flag = false;
    }
    else {
        build_diaplayBoard(new_fd, userIndex, users[userIndex].tiles);
        write(new_fd, noMine_msg, strlen(noMine_msg));
        users[userIndex].exit_flag = false;
    }
}

/*
 * function build_diaplayBoard(): Display the initlial and  modified matrix
 * input:     user option
 * output:    display the Main matrix.
 */
void build_diaplayBoard(int new_fd, int userIndex, Tile display_board[NUM_TILES][NUM_TILES]) {

    clear_screen(new_fd);

     char remainingMines_msg[BUFFER];
     memset(remainingMines_msg, 0, BUFFER);
     write(new_fd, "\n\n\nRemaining mines:- ", strlen("\n\n\nRemaining mines:- "));
     snprintf(remainingMines_msg, BUFFER, "%d\n\n\n\n", users[userIndex].mines_left);
     write(new_fd, remainingMines_msg, strlen(remainingMines_msg));

    int i, j, row, col;

    for (i = 0; i < NUM_TILES; i++) {
        for (j = 0; j < NUM_TILES; j++) {
            if (display_board[i][j].revealed) {
                if (display_board[i][j].is_flag) {
                    display_board[i][j].cover = '+';
                }
                else if (display_board[i][j].is_mine) {
                    display_board[i][j].cover = '*';
                }
                else if (!display_board[i][j].is_mine) {
                    display_board[i][j].cover = display_board[i][j].adjacent_mines + '0';
                }
            }
            else {
                display_board[i][j].cover = ' ';
            }
        }
    }
    //--------------------------------
    // Print board
    //--------------------------------
    char col_msg[BUFFER];
    char matrix_msg[BUFFER];
    char row_msg[BUFFER];

    write(new_fd, "      ", strlen("      "));
    for (col = 0; col < NUM_TILES; col++) {
        snprintf(col_msg, sizeof(col_msg), "%d ",col+1);
        write(new_fd, col_msg, strlen(col_msg));
    }
    write(new_fd, "\n  ------------------------", strlen("\n------------------------"));
    write(new_fd, "\n", strlen("\n"));
    for (row = 0; row < NUM_TILES; row++) {
        //char row_msg[BUFFER];
        snprintf(row_msg, sizeof(row_msg), "  %c | ",arr_char[row]);
        write(new_fd, row_msg, strlen(row_msg));
        
        for (col = 0; col < NUM_TILES; col++) {
            snprintf(matrix_msg, sizeof(matrix_msg), "%c ",display_board[row][col].cover);
            write(new_fd, matrix_msg, strlen(matrix_msg));
        }
        write(new_fd, "\n", strlen("\n"));
    }
}

/*
 * function setup_game(): initiliase the game-start condiitons
 * input:     user option
 * output:    none
 */
void setup_game(int new_fd, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]) {

    users[userIndex].exit_flag = false;
    users[userIndex].mines_left = NUM_MINES;
    int i, j;

    for (i = 0; i < NUM_TILES; i++) {
        for (j = 0; j < NUM_TILES; j++) {
            tiles[i][j].revealed = false;
            tiles[i][j].is_mine = false;
            tiles[i][j].is_flag = false;
        }
    }
    place_mines(tiles);
    build_diaplayBoard(new_fd, userIndex, tiles);
}

/*
 * function place_mines(): generate random mine locations assign it to main matrix
 * input:     user option, Tile matrix
 * output:    mine locations.
 */
void place_mines(Tile tiles[NUM_TILES][NUM_TILES]) {
    int i;
    for (i = 0; i < NUM_MINES; i++) {
        int x, y;
        do {
            pthread_mutex_lock(&scores_mutex);
            x = rand() % NUM_TILES;
            y = rand() % NUM_TILES;
            pthread_mutex_unlock(&scores_mutex);
        } while (already_mined(y, x, tiles));
        tiles[y][x].is_mine = true;
    }
}

/*
 * function already_mined(): check for existing mines
 * input:     Tile coordinates, Tile matrix
 * output:    True/False.
 */
bool already_mined(int y, int x, Tile tiles[NUM_TILES][NUM_TILES]) {
    if (tiles[y][x].is_mine)
        return true;
    else
        return false;
}

/*
 * function reveal_mines(): reveal all the mines (ONLY)
 * input:     Tile Matrix
 */
void reveal_mines(Tile tiles[NUM_TILES][NUM_TILES]) {
    int i, j;
    for (i = 0; i < NUM_TILES; i++) {
        for (j = 0; j < NUM_TILES; j++) {
            if (tiles[i][j].is_mine) {
                tiles[i][j].revealed = true;
            }
            else {
                tiles[i][j].revealed = false;
            }
        }
    }
}

/*
 * function adjacent_mines(): calculate the number of neighbouring mines
 * input:     Tile coordinates, Tile Matrix
 * output:    Number of neighbouring mines
 */
int adjacent_mines(int x, int y, Tile tiles[NUM_TILES][NUM_TILES]) {
    //find the number of neighbouring mines and return the total;
    int i = 0, total_mines = 0;

    if (x == 0 && y == 0) {
        //if in top-left corners
        total_mines = tiles[y + 1][x].is_mine + tiles[y + 1][x + 1].is_mine + tiles[y][x + 1].is_mine;
    }
    else if (x == 8 && y == 0) {
        //top-right
        total_mines = tiles[y + 1][x].is_mine + tiles[y][x - 1].is_mine + tiles[y + 1][x - 1].is_mine;
    }
    else if (y == 0 && x > 0 && x < 8) {
        //if on Top boarder
        total_mines = tiles[y][x + 1].is_mine + tiles[y][x - 1].is_mine + tiles[y + 1][x].is_mine + tiles[y + 1][x + 1].is_mine + tiles[y + 1][x - 1].is_mine;
    }
    else if (x == 0 && y == 8) {
        //bottom-left
        total_mines = tiles[y][x + 1].is_mine + tiles[y - 1][x + 1].is_mine + tiles[y - 1][x].is_mine;
    }
    else if (x == 0 && y > 0 && y < 8) {
        //if on left boarder
        total_mines = tiles[y + 1][x + 1].is_mine + tiles[y - 1][x + 1].is_mine + tiles[y - 1][x].is_mine + tiles[y + 1][x].is_mine + tiles[y][x + 1].is_mine;
    }
    else if (x == 8 && y > 0 && y < 8) {
        //if on right boarder
        total_mines = tiles[y + 1][x].is_mine + tiles[y - 1][x].is_mine + tiles[y][x - 1].is_mine + tiles[y - 1][x - 1].is_mine + tiles[y + 1][x - 1].is_mine;
    }
    else if (y == 8 && x > 0 && x < 8) {
        //if on bottom boarder
        total_mines = tiles[y - 1][x].is_mine + tiles[y - 1][x - 1].is_mine + tiles[y][x + 1].is_mine + tiles[y][x - 1].is_mine + tiles[y - 1][x + 1].is_mine;
    }
    else if (x == 8 && y == 8) {
        //bottom-right
        total_mines = tiles[y - 1][x - 1].is_mine + tiles[y][x - 1].is_mine + tiles[y - 1][x].is_mine;
    }
    else {
        //if in center
        total_mines = tiles[y + 1][x].is_mine + tiles[y - 1][x].is_mine + tiles[y][x + 1].is_mine + tiles[y][x - 1].is_mine + tiles[y + 1][x + 1].is_mine + tiles[y - 1][x - 1].is_mine + tiles[y - 1][x + 1].is_mine + tiles[y + 1][x - 1].is_mine;
    }

    return total_mines;
}

/*
 * function reveal(): reveal a tile
 * input:     user option, Tile coordinates, Tile Matrix
 */
void reveal(int new_fd, int x, int y, int userIndex, Tile tiles[NUM_TILES][NUM_TILES]) {

    if (!tiles[y][x].revealed) {
        if (tiles[y][x].is_mine) {
            //reveal the mines and hide everything else;
            reveal_mines(tiles);
            users[userIndex].exit_flag = true;        
        }
        else {
            tiles[y][x].revealed = true;
            tiles[y][x].adjacent_mines = adjacent_mines(x, y, tiles);

            if (tiles[y][x].adjacent_mines == 0) {
                //call this function for all the neighbours
                //if on left boarder
                if (x == 0 && y > 0 && y < 8) {
                    reveal(new_fd, x + 1, y + 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x, y - 1, userIndex, tiles);
                    reveal(new_fd, x, y + 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                }
                else if (x == 8 && y > 0 && y < 8) {
                    //if on right boarder
                    reveal(new_fd, x, y + 1, userIndex, tiles);
                    reveal(new_fd, x, y - 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x - 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y + 1, userIndex, tiles);
                }
                else if (y == 0 && x > 0 && x < 8) {
                    //if on Top boarder
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x, y + 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y + 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y + 1, userIndex, tiles);
                }
                else if (y == 8 && x > 0 && x < 8) {
                    //if on bottom boarder
                    reveal(new_fd, x, y - 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x + 1, y - 1, userIndex, tiles);
                }
                else if (x == 0 && y == 0) {
                    //if in top-left corners
                    reveal(new_fd, x, y + 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y + 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                }
                else if (x == 8 && y == 0) {
                    //top-right
                    reveal(new_fd, x, y + 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x - 1, y + 1, userIndex, tiles);
                }
                else if (x == 8 && y == 8) {
                    //bottom-right
                    reveal(new_fd, x - 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x, y - 1, userIndex, tiles);
                }
                else if (x == 0 && y == 8) {
                    //bottom-left
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                    reveal(new_fd, x + 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x, y - 1, userIndex, tiles);
                }
                else {
                    //if in center
                    reveal(new_fd,x, y + 1, userIndex, tiles);
                    reveal(new_fd,x, y - 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y, userIndex, tiles);
                    reveal(new_fd, x - 1, y, userIndex, tiles);
                    reveal(new_fd, x + 1, y + 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x + 1, y - 1, userIndex, tiles);
                    reveal(new_fd, x - 1, y + 1, userIndex, tiles);
                }
            }
            users[userIndex].exit_flag = false;
        }
    }
    
}

/*
 * function leaderBoard(): Display the leaderboard data if available
 * output:    display the players win history sorted as required.
 */
void leaderBoard(int new_fd, int userIndex) {

    char username_msg[BUFFER];
    char best_time_achieved_msg[BUFFER];
    char games_won_msg[BUFFER];
    char games_played_msg[BUFFER];
    memset(username_msg, 0, BUFFER), memset(best_time_achieved_msg, 0, BUFFER); 
    memset(games_won_msg, 0, BUFFER), memset(games_played_msg, 0, BUFFER);

    clear_screen(new_fd);
    char *dash_msg= "\n===============================================================================================================\n\n";
    char *leaderbodr_message = 
        "\n\n===============================================================================================================\n\n"
		"                                                      LeaderBoard                                                      "
        "\n\n===============================================================================================================\n\n";

    write(new_fd, leaderbodr_message, strlen(leaderbodr_message));

    if (leaderBoard_empty) {
        char *noinfo_message = 
            "\n\n               There is no information currently stored in the leaderboard. Try again later\n\n"
            "\n\n===============================================================================================================\n\n";
        write(new_fd, noinfo_message, strlen(noinfo_message));
    } 
    else {

        qsort(users, MAX_USERS, sizeof *users, compUser); // sort the users

        int i = 0;
        for (i = 0; i < MAX_USERS; i++) {
            if (users[i].games_won > 0) {
                write(new_fd, "\n", strlen("\n"));
                snprintf(username_msg, BUFFER, "%-12s", users[i].username);
                write(new_fd, username_msg, strlen(username_msg));
                write(new_fd, "\t\t\t\t", strlen("\t\t\t\t"));
                snprintf(best_time_achieved_msg, BUFFER, "%8d", users[i].best_time_achieved);
                write(new_fd, best_time_achieved_msg, strlen(best_time_achieved_msg));
                write(new_fd, " seconds\t\t\t", strlen(" seconds\t\t\t"));
                snprintf(games_won_msg, BUFFER, "%2d", users[i].games_won);
                write(new_fd, games_won_msg, strlen(games_won_msg));
                write(new_fd, " games won, ", strlen(" games won, "));
                snprintf(games_played_msg, BUFFER, "%2d", users[i].games_played);
                write(new_fd, games_played_msg, strlen(games_played_msg));
                write(new_fd, " games played", strlen(" games played"));
            }
        }
        write(new_fd, "\n\n", strlen("\n\n"));
        write(new_fd, dash_msg, strlen(dash_msg));
    }
    mainMenu(new_fd, userIndex);
}

/*
 * function asciiValue(): return the sum of ascii value of passed string.
 */
int asciiValue(char *s) {
    int len = strlen(s);
    int sum = 0;
    for (int i = 0; i < len; i++) {
        sum = sum + s[i];
    }
    return sum;
}

/*
 * function close_client(): reduce the number of current connected clients.
 *                          and close the client socket
 */
void close_client(int new_fd) {
    current_num_of_clients--;
    printf("\n\ncurrent number of clients: %d\n\n", current_num_of_clients);
    close(new_fd);
}

/*
 * function xcoord(): format the x coordinate of the user input 
 *                    when placing a flag or revealing a tile.
 * return the index of the character if exist, else return -1: 
 */
int xcoord(char xcoord) {
    int i;
    for (i = 0; i < 9; i++) {
        if (xcoord == arr_charNUM[i]) {
            return arr_NUM[i];
        }
    }
    return -1;
}

/*
 * function ycoord(): format the y coordinate of the user input 
 *                    when placing a flag or revealing a tile.
 * return the index of the character if exist, else return -1: 
 */
int ycoord(char ycoord) {
    int i;
    for (i = 0; i < 9; i++) {
        if (ycoord == arr_char[i]) {
            return arr_NUM[i];
        }
    }
    return -1;
}

/*
 * function clear_screen(): clear the screen on the client side 
 */
void clear_screen(int new_fd) {
    write(new_fd, clear_msg, strlen(clear_msg));
    usleep(25*1);
}

/*
 * function exit_game(): exit the game when the user lose or choose quit game 
 */
void exit_game(int new_fd) {
    clear_screen(new_fd);
    write(new_fd, exit_msg, strlen(exit_msg));
};

/*
 * function compUser(): comparison function used in qsort function to
 *                      sort the players' leaderboard data as required.
 *                      The best player will be the last index in the users aray.
 */
int compUser(const void* a, const void* b) {
    const user *user1 = a;
    const user *user2 = b;
    if (user1->best_time_achieved > user2->best_time_achieved) return -1;
    else if (user1->best_time_achieved < user2->best_time_achieved) return 1;
    else {
        if (user1->games_won < user2->games_won) return -1;
        else if (user1->games_won > user2->games_won) return 1;
        else {
            return strcmp(user1->username, user2->username);
        }
    }
}