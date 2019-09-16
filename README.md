# minesweeper game

This project is for developing a minesweeper gaame in c programming lamguage. It involves creating a server and a client communication using sockets. 

#


### To compile the server:

```c
gcc -o server server.c -lpthread
```

### To run the server:

```c
./server (default port 12345) 
```
OR
```C
./server PORT
```

#

### To compile the client:

```c
gcc -o client client.c
```

### To run the client:

```c
./client 127.0.0.1 PORT
```