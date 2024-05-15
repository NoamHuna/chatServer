#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>
#include "chatServer.h"

struct conn* get_conn(conn_pool_t *, int);
int add_msg_to_conn(struct conn*, struct msg*);
void update_write_set(fd_set*, conn_pool_t*);
void capitalize(char*, int);
static int end_server = 0;

void intHandler(int SIG_INT) {
	end_server = 1;
}

int main (int argc, char *argv[])
{
    if (argc != 2){
        printf("Usage: server <port>");
        exit(EXIT_FAILURE);
    }
    int port = (int)strtoul(argv[1], NULL, 10);
    if (port <= 0 || port >= 65536){
        printf("Usage: server <port>");
        exit(EXIT_FAILURE);
    }
	
	signal(SIGINT, intHandler);
   
	conn_pool_t* pool = (conn_pool_t*)malloc(sizeof(conn_pool_t));
	initPool(pool);
   
	/*************************************************************/
	/* Create an AF_INET stream socket to receive incoming      */
	/* connections on                                            */
	/*************************************************************/
	int welcome_socket;
    if ((welcome_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        perror("socket");
        exit(EXIT_FAILURE);
    }
   
   
	/*************************************************************/
	/* Set socket to be nonblocking. All the sockets for      */
	/* the incoming connections will also be nonblocking since   */
	/* they will inherit that state from the listening socket.   */
	/*************************************************************/
    int on = 1;
    if(ioctl(welcome_socket, (int)FIONBIO, (char *)&on) < 0){
        perror("iocntl");
    }
	/*************************************************************/
	/* Bind the socket                                           */
	/*************************************************************/

    // create our server
    struct sockaddr_in* chat_server = (struct sockaddr_in*)malloc(sizeof (struct sockaddr_in));
    memset(chat_server, 0, sizeof(struct sockaddr_in));

    chat_server ->sin_family = AF_INET;
    chat_server -> sin_port = htons(port);
    chat_server -> sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(welcome_socket, (struct sockaddr*)chat_server, sizeof (struct sockaddr_in)) == -1){
        perror("bind");
        goto free_resources;
    }

	/*************************************************************/
	/* Set the listen backlog                                   */
	/*************************************************************/
	if(listen(welcome_socket, 5) == -1){
        perror("listen");
        free(chat_server);
        exit(EXIT_FAILURE);
    }

	/*************************************************************/
	/* Initialize fd_sets  			                             */
	/*************************************************************/
	pool -> maxfd = welcome_socket;
    FD_ZERO(&pool -> ready_read_set);
    FD_ZERO(&pool -> ready_write_set);
    FD_SET(welcome_socket, &pool -> ready_read_set);
    char buffer[BUFFER_SIZE] = {0};
	/*************************************************************/
	/* Loop waiting for incoming connections, for incoming data or  */
	/* to write data, on any of the connected sockets.           */
	/*************************************************************/
	int count, validation;
    int threshold;
    do
	{
        /* Copy the original fd_set over to the working fd_set.     */
        memcpy(&pool -> read_set, &pool -> ready_read_set, sizeof(pool -> read_set));
        memcpy(&pool -> write_set, &pool -> ready_write_set, sizeof(pool -> ready_write_set));
        threshold = pool -> maxfd + 1;
        count = 0; validation = 0;
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
		if((pool -> nready = select(pool -> maxfd + 1, &pool -> ready_read_set, &pool ->ready_write_set,
                  NULL, NULL)) < 0){
            perror("select");
            continue;
        }
		/* One or more descriptors are readable or writable.      */
		/* Need to determine which ones they are.                 */

		for (int i=0; i < threshold; i++)
		{
			/* Each time a ready descriptor is found, one less has  */
			/* to be looked for. This is being done so that we     */
			/* can stop looking at the working set once we have     */
			/* found all of the descriptors that were ready         */

			/* Check to see if this descriptor is ready for read   */
			if (FD_ISSET(i, &pool -> ready_read_set))
			{
				/* A descriptor was found that was readable		   */
				/* if this is the listening socket, accept one      */
				/* incoming connection that is queued up on the     */
				/*  listening socket before we loop back and call   */
				/* select again. 						            */
                if(i == welcome_socket) {
                    int new_con = accept(welcome_socket, NULL, NULL);
                    if(new_con == -1){
                        continue;
                    }
                    addConn(new_con, pool);
                    printf("New incoming connection on sd %d\n", new_con);
                    FD_SET(new_con, &pool -> read_set);
                    //FD_SET(i, &pool -> write_set);
                    continue;
                }

				/* If this is not the listening socket, an 			*/
				/* existing connection must be readable				*/
				/* Receive incoming data his socket             */
                size_t msg_size = read(i, buffer, BUFFER_SIZE);
				/* If the connection has been closed by client 		*/
                /* remove the connection (removeConn(...))    		*/
				if(msg_size <= 0){
                    removeConn(i, pool);
                    FD_CLR(i, &pool -> read_set);
                    printf("Connection closed for sd %d\n", i);
                } else {
                    printf("Descriptor %d is readable\n", i);
                    addMsg(i, buffer, (int)msg_size, pool);
                }
				/* Data was received, add msg to all the other    */
				/* connections					  			  */

		                  
				validation ++;
			} /* End of if (FD_ISSET()) */
			/* Check to see if this descriptor is ready for writing  */
			if (FD_ISSET(i, &pool -> ready_write_set)) {
				/* try to write all msgs in queue to sd */
				writeToClient(i, pool);
                validation ++;
		 	}
		 /*******************************************************/
        if(validation >= 1){
            count ++;
        }
        if(count == pool -> nready){
            break;
        }
		 
      } /* End of loop through selectable descriptors */
      update_write_set(&pool -> write_set, pool);
      memcpy(&pool -> ready_read_set, &pool -> read_set, sizeof(pool -> read_set));
      memcpy(&pool -> ready_write_set, &pool -> write_set, sizeof(pool -> write_set));

   } while (end_server == 0);

	/*************************************************************/
	/* If we are here, Control-C was typed,						 */
	/* clean up all open connections					         */
	/*************************************************************/
	struct conn* p = pool -> conn_head;
    struct conn* temp;
    while(p){
        temp = p -> next;
        removeConn(p -> fd, pool);
        p = temp;
    }
    free_resources:
    free(chat_server);
    free(pool);
	return 0;
}


int initPool(conn_pool_t* pool) {
    if(pool) {
        pool->maxfd = -1;
        pool->conn_head = NULL;
        pool->nr_conns = 0;
    }
	return 0;
}

int addConn(int sd, conn_pool_t* pool) {
	/*
	 * 1. allocate connection and init fields
	 * 2. add connection to pool
	 * */
    struct conn* new_con = (struct conn*)malloc(sizeof(struct conn));
    if(!new_con){
        return -1;
    }
    new_con -> fd = sd;
    new_con -> write_msg_head = NULL;
    new_con -> write_msg_tail = NULL;
    new_con -> next = NULL;
    new_con -> prev = NULL;
    struct conn* p = pool -> conn_head;
    if(!p){
        pool -> conn_head = new_con;
    }
    else{
        while(p -> next && p -> next -> fd < sd) {
            p = p->next;
        }
        new_con -> next = p -> next;
        new_con -> prev = p;
        if(p -> next){
            p -> next -> prev = new_con;
        }
        p -> next = new_con;
    }
    pool -> nr_conns ++;
    pool -> maxfd = pool -> maxfd > sd ? pool -> maxfd : sd;
	return 0;
}


int removeConn(int sd, conn_pool_t* pool) {
	/*
	* 1. remove connection from pool 
	* 2. deallocate connection 
	* 3. remove from sets 
	* 4. update max_fd if needed 
	*/
    struct conn* p = pool -> conn_head;
    while (p && p -> fd != sd){
        p = p -> next;
    }
    //the specified fd is not part of the connection list
    if(!p){
        return -1;
    }
    //remove and free message queue for this connection
    struct msg* to_free;
    while ((to_free = p -> write_msg_head)){
        free(to_free -> message);
        p -> write_msg_head = to_free -> next;
        free(to_free);
    }
    p -> write_msg_tail = NULL;
    struct conn* temp = p -> next; // save a pointer to the next element in the list
    //temp may be Null
    if(!temp){ // specified fd is the last element in the list
        if(p -> prev) { // isn't the first element
            p->prev->next = NULL;
            pool->maxfd = p->prev->fd;
            goto end_remove_con;
        }
        else{ // specified fd is also the first element in the list
            pool -> conn_head = NULL;
            goto end_remove_con;
        }
    }
    else {
        if(!p -> prev){
            pool -> conn_head = p -> next;
            pool -> conn_head -> prev = NULL;
        }
        else {
            p->prev->next = temp;
            temp->prev = p->prev;
        }
    }
    end_remove_con:
    pool -> nr_conns --;
    if(pool -> nr_conns == 0){
        pool -> conn_head = NULL;
    }
    free(p);
    close(sd);
    printf("removing connection with sd %d \n", sd);
	return 0;
}

int addMsg(int sd,char* buffer,int len,conn_pool_t* pool) {
	
	/*
	 * 1. add msg_t to write queue of all other connections 
	 * 2. set each fd to check if ready to write 
	 */

	struct conn* connection = get_conn(pool, sd);
    if(!connection){
        return -1; // the client who trying to write some message isn't a valid connection
    }
    struct msg* new_msg =(struct msg*) malloc(sizeof(struct msg));
    new_msg -> message = (char *) malloc(len + 1);
    memcpy(new_msg -> message, buffer, len); // todo: capitalize the letters
    new_msg -> size = len;
    new_msg -> next = NULL;
    struct conn* p = pool -> conn_head;
    while(p){
        if(p == connection){
            p = p -> next;
            continue;
        }
        else{
            add_msg_to_conn(p, new_msg);
            p= p -> next;
        }
    }
    free(new_msg -> message);
    free(new_msg);
    printf("%d bytes received from sd %d\n", len, sd);
	return 0;
}

int writeToClient(int sd,conn_pool_t* pool) {
	
	/*
	 * 1. write all msgs in queue 
	 * 2. deallocate each written msg
	 * 3. if all msgs were written successfully, there is nothing else to write to this fd... */
	struct conn* client = get_conn(pool, sd);
    if(!client) return -1;
    size_t bytes_written = 0;
    struct msg* msg_to_write = client -> write_msg_head;
    while(msg_to_write){
        capitalize(msg_to_write -> message, msg_to_write -> size);
        bytes_written += write(client -> fd, msg_to_write -> message, msg_to_write -> size);
        if(bytes_written == (size_t)msg_to_write -> size){
            client -> write_msg_head = msg_to_write -> next;
            //struct msg* temp = msg_to_write -> next;
            free(msg_to_write -> message);
            free(msg_to_write);
            msg_to_write = client -> write_msg_head;
            bytes_written = 0;
        }
        else if(bytes_written == 0){
            return -1;
        }
    }
    if(!msg_to_write){
        client -> write_msg_tail = NULL;
    }
	return 0;
}

void update_write_set(fd_set* write_Set, conn_pool_t* pool){
    // pass over all the connection and look for some connections whose have some unwritten messages
    struct conn* p = pool -> conn_head;
    while(p != NULL){
        if(p -> write_msg_head){
            FD_SET(p -> fd, write_Set);
        }
        else{
            FD_CLR(p -> fd, write_Set);
        }
        p = p -> next;
    }
}

struct conn* get_conn(conn_pool_t * pool, int sd){
    struct conn* p = pool -> conn_head;
    while(p && p -> fd != sd){
        p = p -> next;
    }
    return p;
}

int add_msg_to_conn(struct conn* connection, struct msg* msg){
    struct msg* msg_copy = (struct msg*) malloc(sizeof(struct msg));
    msg_copy -> message = (char *) malloc(msg -> size);
    memcpy(msg_copy -> message, msg -> message, msg -> size);
    msg_copy -> size = msg -> size;
    msg_copy -> next = msg -> next;
    msg_copy -> prev = msg -> prev;
    if(connection -> write_msg_tail) {
        connection->write_msg_tail->next = msg_copy;
        msg -> prev = connection -> write_msg_tail;
        connection->write_msg_tail = msg_copy;

    }
    else{
        connection -> write_msg_tail = connection -> write_msg_head = msg_copy;
    }
    return 0;

}

void capitalize(char *str, int length) {
    for (int i = 0; i < length; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            str[i] = (char)(str[i] - ('a' - 'A')); // Convert a lowercase letter to uppercase
        }
    }
}