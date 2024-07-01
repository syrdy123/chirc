/*! \file main.c
 *  \brief main() function for chirc server
 *
 *  This module provides the main() function for the server,
 *  which parses the command-line arguments to the chirc executable.
 *
 *  Code related to running the server should go in the chirc_run function
 *  (found below the main() function)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>

#include "chirc.h"
#include "ctx.h"
#include "log.h"
#include "connection.h"
#include "utils.h"
#include "utils_list.h"
#include "my_utils.h"
#include "common.h"

#define IP_SIZE 20
#define HOST_SIZE 256


typedef struct thread_data
{
    int sockfd;
    chirc_ctx_t *ctx;

} thread_data_t;

connection_map_t *connection_hash = NULL;
user_node_t *nick_head = NULL, *user_head = NULL;

/* Forward declaration of chirc_run */
int chirc_run(chirc_ctx_t *ctx);

bool is_running = true;
void sig_handler(int sig)
{
    is_running = false;
    chilog(INFO, "SIGINT is comming!");
}

/* DO NOT modify the contents of the main() function.
 * Add your code in the chirc_run function found below
 * the main() function. */
int main(int argc, char *argv[])
{
    /* Parse command-line parameters */
    int opt;
    sds port = NULL, passwd = NULL, servername = NULL, network_file = NULL;
    int verbosity = 0;

    while ((opt = getopt(argc, argv, "p:o:s:n:vqh")) != -1)
        switch (opt)
        {
        case 'p':
            port = sdsnew(optarg);
            break;
        case 'o':
            passwd = sdsnew(optarg);
            break;
        case 's':
            servername = sdsnew(optarg);
            break;
        case 'n':
            if (access(optarg, R_OK) == -1)
            {
                printf("ERROR: No such file: %s\n", optarg);
                exit(-1);
            }
            network_file = sdsnew(optarg);
            break;
        case 'v':
            verbosity++;
            break;
        case 'q':
            verbosity = -1;
            break;
        case 'h':
            printf("Usage: chirc -o OPER_PASSWD [-p PORT] [-s SERVERNAME] [-n NETWORK_FILE] [(-q|-v|-vv)]\n");
            exit(0);
            break;
        default:
            fprintf(stderr, "ERROR: Unknown option -%c\n", opt);
            exit(-1);
        }

    if (!passwd)
    {
        fprintf(stderr, "ERROR: You must specify an operator password\n");
        exit(-1);
    }

    if (network_file && !servername)
    {
        fprintf(stderr, "ERROR: If specifying a network file, you must also specify a server name.\n");
        exit(-1);
    }

    /* Set logging level based on verbosity */
    switch (verbosity)
    {
    case -1:
        chirc_setloglevel(QUIET);
        break;
    case 0:
        chirc_setloglevel(INFO);
        break;
    case 1:
        chirc_setloglevel(DEBUG);
        break;
    case 2:
        chirc_setloglevel(TRACE);
        break;
    default:
        chirc_setloglevel(TRACE);
        break;
    }

    /* Create server context */
    chirc_ctx_t ctx;
    chirc_ctx_init(&ctx);
    ctx.oper_passwd = passwd;

    if (!network_file)
    {
        /* If running in standalone mode, we have an IRC Network with
         * just one server. We only initialize ctx.network.this_server */
        char hbuf[NI_MAXHOST];
        gethostname(hbuf, sizeof(hbuf));

        ctx.network.this_server = calloc(1, sizeof(chirc_server_t));

        ctx.network.this_server->servername = sdsnew(hbuf);
        ctx.network.this_server->hostname = sdsnew(hbuf);
        ctx.network.this_server->passwd = NULL;
        ctx.network.this_server->conn = NULL;

        if (port)
        {
            ctx.network.this_server->port = port;
        }
        else
        {
            ctx.network.this_server->port = sdsnew("6667");
        }

        serverlog(INFO, NULL, "%s: standalone mode (port %s)", ctx.version, ctx.network.this_server->port);
    }
    else
    {
        /* If running in network mode, we load the network specification from the network file
         * specified with the -n parameter */
        if (chirc_ctx_load_network(&ctx, network_file, servername) == CHIRC_FAIL)
        {
            serverlog(CRITICAL, NULL, "Could not load network file.");
            exit(-1);
        }

        serverlog(INFO, NULL, "%s: IRC network mode", ctx.version);

        for (chirc_server_t *s = ctx.network.servers; s != NULL; s = s->hh.next)
        {
            bool cur_server = (strcmp(s->servername, ctx.network.this_server->servername) == 0);
            serverlog(INFO, NULL, "  %s (%s:%s) %s", s->servername, s->hostname, s->port,
                      cur_server ? " <--" : "");
        }
    }

    /* Run the server */
    return chirc_run(&ctx);
}

void my_construct_user_reply(chirc_ctx_t *ctx, char *code, char *response_msg, char *nickname, int fd)
{
    char *response_str;
    char buf[1024] = {0};
    chirc_message_t *msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
    chirc_connection_t *connection = (chirc_connection_t *)malloc(sizeof(chirc_connection_t));
    chirc_user_t *user = (chirc_user_t *)malloc(sizeof(chirc_user_t));

    user->nick = strdup(nickname);
    connection->peer.user = user;
    connection->type = CONN_TYPE_USER;

    chirc_message_construct_reply(msg, ctx, connection, code);
    if (0 == strncmp(code, "443", 3))
    {
        chirc_message_add_parameter(msg, "*", false);
    }

    chirc_message_add_parameter(msg, response_msg, true);
    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;

    write(fd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void *subthread_work(void *args)
{
    chirc_message_t *msg = NULL;
    thread_data_t *data = (thread_data_t *)args;
    int sockfd = data->sockfd;
    chirc_ctx_t *ctx = data->ctx;

    char full_command[1024] = {0}, buf[1024] = {0};
    int ret = 0, pos = 0, total_read = 0;

    while (1)
    {
        ret = read(sockfd, buf + pos, sizeof(buf) - total_read);
        if (ret < 0)
        {
            chilog(INFO, "the other side has disconnected!");
            close(sockfd);
        }
        else if (0 == ret)
        {
            chilog(INFO, "the other side has disconnected!");
            close(sockfd);
        }

        pos += ret;
        total_read += ret;
        char *p = NULL;

        while (NULL != (p = strstr(buf, "\r\n")))
        {
            int len = (p - buf) + 2;
            memset(full_command, 0, sizeof(full_command));
            memcpy(full_command, buf, len);
            memset(buf, 0, len);
            memmove(buf, buf + len, sizeof(buf) - len);
            pos -= len;
            total_read -= len;

            chilog(INFO, "full_command: %s", full_command);
            chilog(INFO, "buf: %s", buf);

            msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
            chirc_message_from_string(msg, full_command);
            char *name = msg->params[0];

            chilog(INFO, "name: %s", name);
            

            if (7 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "PRIVMSG", 7))
            {
                connection_map_t *node = find_connection_map_node(connection_hash, name);
                if (NULL == node)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "You have not registered");
                    my_construct_user_reply(ctx, ERR_NOTREGISTERED, buf, "*", sockfd);
                }
            }
            else if (0 == strncmp(msg->cmd, "NICK", 4))
            {
                if (0 == msg->nparams)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "No nickname given");
                    user_node_t *node = get_least_user_node(nick_head);
                    my_construct_user_reply(ctx, ERR_NONICKNAMEGIVEN, buf, (node != NULL ? node->name: "*"), sockfd);
                }
                else if (1 == msg->nparams)
                {
                    user_node_t *nick_node = find_user_node(nick_head, name);
                    
                    if (nick_node == NULL)
                    {
                        nick_node = (user_node_t *)malloc(sizeof(user_node_t));
                        memset(nick_node->name, 0, sizeof(nick_node->name));
                        memcpy(nick_node->name, name, strlen(name));
                        nick_node->next = NULL;
                        if (nick_head == NULL)
                        {
                            nick_head = nick_node;
                        }
                        else
                        {
                            add_user_node(nick_head, nick_node);
                        }

                        user_node_t *user_node = fuzzy_find_user_node(user_head, name);

                        if (user_node != NULL)
                        {
                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Welcome to the Internet Relay Network %s!%s@%s", nick_node->name, user_node->name, ctx->network.this_server->servername);
                            connection_map_t *connection_node = (connection_map_t *)malloc(sizeof(connection_map_t));
                            memset(connection_node->name, 0, sizeof(connection_node->name));
                            memcpy(connection_node->name, nick_node->name, strlen(nick_node->name));
                            connection_node->fd = sockfd;
                            add_connection_map_node(&connection_hash, connection_node);
                            my_construct_user_reply(ctx, RPL_WELCOME, buf, name, sockfd);
                        }
                    }
                    else
                    {
                        connection_map_t *connection_node = find_connection_map_node(connection_hash, name);
                        if(NULL != connection_node)
                        {
                            // nick is already in use
                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Nickname is already in use.");
                            my_construct_user_reply(ctx, "433", buf, name, sockfd);
                        }
                    }
                }
            }
            else if (0 == strncmp(msg->cmd, "USER", 4))
            {

                user_node_t *user_node = find_user_node(user_head, name);
                user_node_t *nick_node = fuzzy_find_user_node(nick_head, name);

                if (msg->nparams < 4)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "Not enough parameters");
                    my_construct_user_reply(ctx, ERR_NEEDMOREPARAMS, buf, (nick_node != NULL ? nick_node->name: "*"), sockfd);
                    continue;
                }

                if (user_node == NULL)
                {
                    user_node = (user_node_t *)malloc(sizeof(user_node_t));
                    memset(user_node->name, 0, sizeof(user_node->name));
                    memcpy(user_node->name, name, strlen(name));
                    user_node->next = NULL;
                    if (NULL == user_head)
                    {
                        user_head = user_node;
                    }
                    else
                    {
                        add_user_node(user_head, user_node);
                    }

                    chilog(INFO, "msg->nparams = %d", msg->nparams);
                    

                    if (nick_node != NULL)
                    {
                        if (4 == msg->nparams)
                        {
                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Welcome to the Internet Relay Network %s!%s@%s", nick_node->name, user_node->name, ctx->network.this_server->servername);
                            connection_map_t *connection_node = (connection_map_t *)malloc(sizeof(connection_map_t));
                            memset(connection_node->name, 0, sizeof(connection_node->name));
                            memcpy(connection_node->name, nick_node->name, strlen(nick_node->name));
                            connection_node->fd = sockfd;
                            add_connection_map_node(&connection_hash, connection_node);
                            my_construct_user_reply(ctx, RPL_WELCOME, buf, nick_node->name, sockfd);
                        }
                    }
                }
            }

            chirc_message_free(msg);

            chilog(INFO, "nick_head====================================nick_head");
            print_user_node(nick_head);
            chilog(INFO, "=====================================================");
            chilog(INFO, "user_head====================================user_head");
            print_user_node(user_head);
            chilog(INFO, "======================================================");
            msg = NULL;
        }
    }

    free_user_node(nick_head);
    free_user_node(user_head);
    free_connection_map_node(&connection_hash);
    free(data);

    return NULL;
}

/*!
 * \brief Runs the chirc server
 *
 * This function starts the chirc server and listens for new
 * connections. Each time a new connection is established,
 * a new thread is created to handle that connection
 * (by calling create_connection_thread)
 *
 * In this function, you can assume the ctx parameter is a fully
 * initialized chirc_ctx_t struct. Most notably, ctx->network.this_server->port
 * will contain the port the server must listen on.
 *
 * \param ctx Server context
 * \return 0 on success, non-zero on failure.
 */
int chirc_run(chirc_ctx_t *ctx)
{
    /* Your code goes here */

    signal(SIGINT, sig_handler);

    int ret = 0, listenfd, sockfd;
    int total_read = 0, pos = 0;
    int port = atoi(ctx->network.this_server->port);
    struct sockaddr_in addr, client_addr, local_addr;
    char client_host[HOST_SIZE] = {0}, server_host[HOST_SIZE] = {0}, buf[600] = {0}, full_command[600] = {0};
    socklen_t client_addr_len = sizeof(client_addr);
    chirc_message_t *msg = NULL, *response_msg = NULL;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        chilog(ERROR, "failed to create listenfd: %d!", listenfd);
        return listenfd;
    }

    ret = bind(listenfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        chilog(ERROR, "failed to bind!");
        goto _error;
    }

    ret = listen(listenfd, 128);
    if (ret < 0)
    {
        chilog(ERROR, "failed to listen!");
        goto _error;
    }

    while (is_running)
    {
        sockfd = accept(listenfd, NULL, NULL);
        if(sockfd == -1)
        {
            chilog(ERROR, "failed to accept!");
            break;
        }

        pthread_t tid;
        thread_data_t *data = (thread_data_t *)malloc(sizeof(thread_data_t));
        data->ctx = ctx;
        data->sockfd = sockfd;

        pthread_create(&tid, NULL, subthread_work, data);
        pthread_detach(tid);

    }

_error:
    chilog(INFO, "program is exited!");
    close(listenfd);

    return ret;
}