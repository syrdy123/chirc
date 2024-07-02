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
#include <stdatomic.h>
#include <stdbool.h>

#include "chirc.h"
#include "ctx.h"
#include "log.h"
#include "connection.h"
#include "utils.h"
#include "utils_list.h"
#include "my_utils.h"
#include "reply.h"

#define IP_SIZE 20
#define HOST_SIZE 256

#define MOTD_FILE "/home/wurusai/irc/chirc/motd.txt"

extern pthread_mutex_t user_node_mutex;
extern pthread_mutex_t connection_node_mutex;
extern pthread_mutex_t sockfd_nick_node_mutex;

atomic_int connection_count = ATOMIC_VAR_INIT(0);
atomic_int registered_connection_count = ATOMIC_VAR_INIT(0);

typedef struct thread_data
{
    int sockfd;
    chirc_ctx_t *ctx;

} thread_data_t;

static sockfd_nick_map_t *sockfd_nick_hash = NULL;
static connection_map_t *connection_hash = NULL;
static user_node_t *nick_head = NULL;
static user_node_t *user_head = NULL;

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

void trim_space(const char *src, int len, char *out)
{
    int idx = 0;
    for (int i = 0; i < len; ++i)
    {
        if (' ' != src[i])
        {
            int j = i;
            while (j < len && ' ' != src[j])
            {
                out[idx++] = src[j];
                j++;
            }

            i = j;
            if (i < len)
            {
                // 分配一个空格
                out[idx++] = ' ';
            }
        }
    }

    out[idx - 1] = '\n';
    out[idx - 2] = '\r';
}

void response_PING(chirc_ctx_t *ctx, char *nickname, int sockfd)
{
    char *response_str;
    char buf[1024] = {0};
    chirc_message_t *msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
    chirc_connection_t *connection = (chirc_connection_t *)malloc(sizeof(chirc_connection_t));
    chirc_user_t *user = (chirc_user_t *)malloc(sizeof(chirc_user_t));

    user->nick = strdup(nickname);
    connection->peer.user = user;
    connection->type = CONN_TYPE_QUIT;

    chirc_message_construct_reply(msg, ctx, connection, "PONG");
    chirc_message_add_parameter(msg, "xixi", false);

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void response_WHOIS(chirc_ctx_t *ctx, char *nickname, int sockfd)
{
    connection_map_t *connection_node = find_connection_map_node(connection_hash, nickname);
    sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);

    if (NULL == connection_node)
    {
        my_construct_user_WHOIS_NOSUCHNICK_reply(ctx, ERR_NOSUCHNICK, "No such nick/channel", sockfd_nick_node->name, nickname, sockfd);
    }
    else
    {
        my_construct_user_WHOIS_reply(ctx, RPL_WHOISUSER, connection_node->msg, NULL, sockfd_nick_node->name, nickname, sockfd);
        my_construct_user_WHOIS_WHOISSERVER_reply(ctx, RPL_WHOISSERVER, sockfd_nick_node->name, nickname, sockfd);
        my_construct_user_WHOIS_ENDOFWHOIS_reply(ctx, RPL_ENDOFWHOIS, sockfd_nick_node->name, sockfd_nick_node->name, "End of WHOIS list", sockfd);
    }
}

void my_construct_user_WHOIS_ENDOFWHOIS_reply(chirc_ctx_t *ctx, char *code, char *nickname, char* cmd,  char* extra, int sockfd)
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
    chirc_message_add_parameter(msg, cmd, true);
    chirc_message_add_parameter(msg, extra, true);

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void my_construct_user_WHOIS_WHOISSERVER_reply(chirc_ctx_t *ctx, char *code, char *nickname, char* extra, int sockfd)
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
    chirc_message_add_parameter(msg, extra, false);
    chirc_message_add_parameter(msg, ctx->network.this_server->servername, false);
    chirc_message_add_parameter(msg, "Ubuntu", true);

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void my_construct_user_WHOIS_reply(chirc_ctx_t *ctx, char *code, chirc_message_t *user_msg, char *long_param_re, char *nickname, char *extra, int sockfd)
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

    if (NULL != user_msg)
    {
        chirc_message_add_parameter(msg, user_msg->cmd, false);
        
        for (int i = 0; i < user_msg->nparams; ++i)
        {
            
            if (i != user_msg->nparams - 1)
            {
                chirc_message_add_parameter(msg, user_msg->params[i], false);
            }
            else
            {
                chirc_message_add_parameter(msg, user_msg->params[i], true);
            }
        }
    }
    else if (long_param_re != NULL)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }
    else
    {
        chirc_message_add_parameter(msg, ctx->network.this_server->servername, false);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    chirc_message_free(user_msg);
    free(user->nick);
    free(user);
    free(connection);
}

void my_construct_user_WHOIS_NOSUCHNICK_reply(chirc_ctx_t *ctx, char *code, char *long_param_re, char *nickname, char *extra, int sockfd)
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

    if(extra != NULL)
    {
        chirc_message_add_parameter(msg, extra, false);
    }

    if (long_param_re != NULL)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void my_construct_user_UNKNOWN_reply(chirc_ctx_t *ctx, char *code, char *nickname, char *cmd, char *long_param_re, int sockfd)
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

    if (NULL != cmd)
    {
        chirc_message_add_parameter(msg, cmd, true);
    }

    if (NULL != long_param_re)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void response_QUIT(chirc_ctx_t *ctx, char *long_param_re, int sockfd, char *extra)
{
    my_construct_user_QUIT_reply(ctx, "ERROR", long_param_re, "", sockfd);
}

void my_construct_user_QUIT_reply(chirc_ctx_t *ctx, char *cmd, char *long_param_re, char *nickname, int sockfd)
{
    char *response_str;
    char buf[1024] = {0};
    chirc_message_t *msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
    chirc_connection_t *connection = (chirc_connection_t *)malloc(sizeof(chirc_connection_t));
    chirc_user_t *user = (chirc_user_t *)malloc(sizeof(chirc_user_t));

    user->nick = strdup(nickname);
    connection->peer.user = user;
    connection->type = CONN_TYPE_QUIT;

    chirc_message_construct_reply(msg, ctx, connection, cmd);

    if (NULL != long_param_re)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void response_MOTD(chirc_ctx_t *ctx, char *nickname, int sockfd, bool expect_motd)
{
    char buf[1024] = {0}, line[256] = {0};
    FILE *file = NULL;

    if (!expect_motd)
    {
        sprintf(line, "MOTD File is missing");
        my_construct_user_MOTD_reply(ctx, ERR_NOMOTD, line, nickname, sockfd);
        return;
    }

    // open file
    file = fopen(MOTD_FILE, "r");
    if (file == NULL)
    {
        chilog(ERROR, "cannot open file: %s", MOTD_FILE);
        sprintf(line, "MOTD File is missing");
        my_construct_user_MOTD_reply(ctx, ERR_NOMOTD, line, nickname, sockfd);
        return;
    }

    sprintf(line, "- .* Message of the day - ");
    my_construct_user_reply(ctx, RPL_MOTDSTART, line, NULL, nickname, sockfd);

    while (fgets(line, sizeof(line), file))
    {
        int n = strlen(line);
        if (line[n] == '\n')
        {
            line[n] = '\0';
        }

        memset(buf, 0, sizeof buf);
        sprintf(buf, "- %s", line);
        my_construct_user_MOTD_reply(ctx, RPL_MOTD, buf, nickname, sockfd);
        memset(line, 0, sizeof line);
    }

    // close file
    fclose(file);
    memset(line, 0, sizeof line);
    sprintf(line, "End of MOTD command");
    my_construct_user_MOTD_reply(ctx, RPL_ENDOFMOTD, line, nickname, sockfd);
}

void my_construct_user_MOTD_reply(chirc_ctx_t *ctx, char *code, char *long_param_re, char *nickname, int sockfd)
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

    if (NULL != long_param_re)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void response_LUSERS(chirc_ctx_t *ctx, char *nickname, int sockfd)
{
    char buf[1024] = {0}, extra[48] = {0};
    sprintf(buf, "There are %d users and 0 services on 1 servers", get_connection_map_node_size(connection_hash));
    my_construct_user_LUSERS_reply(ctx, RPL_LUSERCLIENT, buf, nickname, NULL, sockfd);

    memset(buf, 0, sizeof buf);
    sprintf(buf, "operator(s) online");
    sprintf(extra, "0");
    my_construct_user_LUSERS_reply(ctx, RPL_LUSEROP, buf, nickname, extra, sockfd);

    memset(buf, 0, sizeof buf);
    memset(extra, 0, sizeof extra);
    sprintf(buf, "unknown connection(s)");
    int unknown_cnt = atomic_load(&connection_count) - get_connection_map_node_size(connection_hash);
    sprintf(extra, "%d", unknown_cnt);
    my_construct_user_LUSERS_reply(ctx, RPL_LUSERUNKNOWN, buf, nickname, extra, sockfd);

    memset(buf, 0, sizeof buf);
    memset(extra, 0, sizeof extra);
    sprintf(buf, "channels formed");
    sprintf(extra, "0");
    my_construct_user_LUSERS_reply(ctx, RPL_LUSERCHANNELS, buf, nickname, extra, sockfd);

    memset(buf, 0, sizeof buf);
    sprintf(buf, "I have %d clients and 1 servers", get_sockfd_nick_map_node_size(connection_hash));
    my_construct_user_LUSERS_reply(ctx, RPL_LUSERME, buf, nickname, NULL, sockfd);
}

void my_construct_user_LUSERS_reply(chirc_ctx_t *ctx, char *code, char *long_param_re, char *nickname, char *extra, int sockfd)
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

    if (NULL != extra)
    {
        chirc_message_add_parameter(msg, extra, false);
    }

    if (NULL != long_param_re)
    {
        chirc_message_add_parameter(msg, long_param_re, true);
    }

    chirc_message_to_string(msg, &response_str);

    chilog(INFO, "response_msg is %s", response_str);

    char *p = strstr(response_str, "\r\n");
    int len = (p - response_str) + 2;
    write(sockfd, response_str, len);

    chirc_message_free(msg);
    free(user->nick);
    free(user);
    free(connection);
}

void my_construct_user_RPL_MYINFO_reply(chirc_ctx_t *ctx, char *code, char *response_msg, char *nickname,
                                        int fd, char *version, char *user_mode, char *channel_mode)
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
    if (NULL != response_msg)
        chirc_message_add_parameter(msg, response_msg, true);
    chirc_message_add_parameter(msg, ctx->network.this_server->servername, false);
    chirc_message_add_parameter(msg, version, false);
    chirc_message_add_parameter(msg, user_mode, false);
    chirc_message_add_parameter(msg, channel_mode, false);

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

void my_construct_user_reply(chirc_ctx_t *ctx, char *code, char *response_msg, char *extra, char *nickname, int fd)
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
    if (NULL != extra)
    {
        chirc_message_add_parameter(msg, extra, false);
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

    char temp_command[1024] = {0}, full_command[1024] = {0}, buf[1024] = {0}, bak[1024] = {0};
    int ret = 0, pos = 0;

    while (1)
    {
        ret = read(sockfd, buf + pos, sizeof(buf) - pos);
        if (ret < 0)
        {
            chilog(INFO, "the other side has disconnected!");
            sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);
            del_sockfd_nick_map_node(&sockfd_nick_hash, sockfd_nick_node);

            close(sockfd);
            atomic_fetch_sub(&connection_count, 1);
            atomic_fetch_sub(&registered_connection_count, 1);
            break;
        }
        else if (0 == ret)
        {
            chilog(INFO, "the other side has disconnected!");
            sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);
            del_sockfd_nick_map_node(&sockfd_nick_hash, sockfd_nick_node);

            close(sockfd);
            atomic_fetch_sub(&connection_count, 1);
            atomic_fetch_sub(&registered_connection_count, 1);
            break;
        }

        pos += ret;
        char *p = NULL;

        while (NULL != (p = strstr(buf, "\r\n")))
        {
            int len = (p - buf) + 2;

            memset(temp_command, 0, sizeof(temp_command));
            memcpy(temp_command, buf, len);

            memset(full_command, 0, sizeof(full_command));
            trim_space(temp_command, len, full_command);

            memset(bak, 0, sizeof bak);
            memcpy(bak, buf + len, sizeof(buf) - len);

            memset(buf, 0, sizeof buf);
            memcpy(buf, bak, sizeof(bak));
            pos -= len;

            p = strstr(full_command, "\r\n");
            len = (p - full_command) + 2;
            if (len <= 2)
            {
                continue;
            }

            msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
            chirc_message_from_string(msg, full_command);
            char *name = msg->params[0];

            chilog(INFO, "name: %s", name);
            if(4 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "PING", 4))
            {
                response_PING(ctx, "", sockfd);
            }
            else if(4 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "PONG", 4))
            {

            }
            else if (4 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "QUIT", 4))
            {
                memset(buf, 0, sizeof buf);
                user_node_t *nick_node = get_least_user_node(nick_head);
                if (1 == msg->nparams)
                {
                    sprintf(buf, "Closing Link: %s (%s)", (nick_node != NULL ? nick_node->name : "*"), msg->params[0]);
                }
                else
                {
                    sprintf(buf, "Closing Link: %s (Client Quit)", (nick_node != NULL ? nick_node->name : "*"));
                }

                response_QUIT(ctx, buf, sockfd, NULL);
                close(sockfd);
                atomic_fetch_sub(&connection_count, 1);
                atomic_fetch_sub(&registered_connection_count, 1);
            }
            else if (6 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "LUSERS", 6))
            {
                sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);
                response_LUSERS(ctx, (sockfd_nick_node != NULL ? sockfd_nick_node->name : ""), sockfd);
            }
            else if (4 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "MOTD", 4))
            {
                sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);
                response_MOTD(ctx, (sockfd_nick_node != NULL ? sockfd_nick_node->name : ""), sockfd, true);
            }
            else if (5 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "WHOIS", 5))
            {
                if (1 == msg->nparams)
                {
                    response_WHOIS(ctx, name, sockfd);
                }
            }
            else if (7 == strlen(msg->cmd) && 0 == strncmp(msg->cmd, "PRIVMSG", 7))
            {
                connection_map_t *node = find_connection_map_node(connection_hash, name);
                if (NULL == node)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "You have not registered");
                    user_node_t *nick_node = get_least_user_node(nick_head);
                    my_construct_user_reply(ctx, ERR_NOTREGISTERED, buf, NULL, (NULL != nick_node ? nick_node->name : "*"), sockfd);
                }

                for (int i = 0; i < msg->nparams; ++i)
                {
                    chilog(INFO, "msg->params[%d]: %s", i, msg->params[i]);
                }
            }
            else if (0 == strncmp(msg->cmd, "NICK", 4))
            {
                atomic_fetch_and(&registered_connection_count, 1);
                if (0 == msg->nparams)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "No nickname given");
                    user_node_t *node = get_least_user_node(nick_head);
                    my_construct_user_reply(ctx, ERR_NONICKNAMEGIVEN, buf, NULL, (node != NULL ? node->name : "*"), sockfd);
                }
                else if (1 == msg->nparams)
                {

                    connection_map_t *connection_node = find_connection_map_node(connection_hash, name);
                    if (NULL != connection_node)
                    {
                        // nick is already in use
                        memset(buf, 0, sizeof(buf));
                        sprintf(buf, "Nickname is already in use");
                        my_construct_user_reply(ctx, ERR_NICKNAMEINUSE, buf, name, "*", sockfd);
                        continue;
                    }

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

                        user_node_t *user_node = find_user_node(user_head, name);

                        if (user_node != NULL)
                        {
                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Welcome to the Internet Relay Network %s!%s@%s", nick_node->name, user_node->name, ctx->network.this_server->servername);
                            connection_map_t *connection_node = (connection_map_t *)malloc(sizeof(connection_map_t));
                            memset(connection_node->name, 0, sizeof(connection_node->name));
                            memcpy(connection_node->name, nick_node->name, strlen(nick_node->name));
                            connection_node->msg = NULL;
                            connection_node->msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
                            chirc_message_construct(connection_node->msg, NULL, user_node->msg->cmd);
                            for (int i = 0; i < user_node->msg->nparams; ++i)
                            {
                                if (i != user_node->msg->nparams - 1)
                                {
                                    chirc_message_add_parameter(connection_node->msg, user_node->msg->params[i], false);
                                }
                                else
                                {
                                    chirc_message_add_parameter(connection_node->msg, user_node->msg->params[i], true);
                                }
                            }

                            connection_node->fd = sockfd;
                            add_connection_map_node(&connection_hash, connection_node);
                            my_construct_user_reply(ctx, RPL_WELCOME, buf, NULL, name, sockfd);

                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Your host is %s, running version 1.0", ctx->network.this_server->servername);
                            my_construct_user_reply(ctx, RPL_YOURHOST, buf, NULL, name, sockfd);

                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "This server was created 20240701");
                            my_construct_user_reply(ctx, RPL_CREATED, buf, NULL, name, sockfd);

                            my_construct_user_RPL_MYINFO_reply(ctx, RPL_MYINFO, NULL, name, sockfd, "1.0", "ao", "mtov");

                            response_LUSERS(ctx, name, sockfd);

                            response_MOTD(ctx, name, sockfd, false);

                            sockfd_nick_map_t *sockfd_nick_node = (sockfd_nick_map_t *)malloc(sizeof(sockfd_nick_map_t));
                            memset(sockfd_nick_node->name, 0, sizeof(sockfd_nick_node->name));
                            memcpy(sockfd_nick_node->name, nick_node->name, strlen(nick_node->name));
                            sockfd_nick_node->fd = sockfd;
                            add_sockfd_nick_map_node(&sockfd_nick_hash, sockfd_nick_node);
                        }
                    }
                }
            }
            else if (0 == strncmp(msg->cmd, "USER", 4))
            {
                user_node_t *user_node = find_user_node(user_head, name);
                user_node_t *nick_node = NULL;

                if (msg->nparams < 4)
                {
                    memset(buf, 0, sizeof(buf));
                    sprintf(buf, "Not enough parameters");
                    nick_node = get_least_user_node(nick_head);
                    my_construct_user_reply(ctx, ERR_NEEDMOREPARAMS, buf, "USER", (nick_node != NULL ? nick_node->name : "*"), sockfd);
                    continue;
                }

                if (user_node == NULL)
                {
                    user_node = (user_node_t *)malloc(sizeof(user_node_t));
                    memset(user_node->name, 0, sizeof(user_node->name));
                    memcpy(user_node->name, name, strlen(name));
                    user_node->msg = NULL;
                    user_node->msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
                    chirc_message_construct(user_node->msg, NULL, msg->cmd);

                    for (int i = 0; i < msg->nparams; ++i)
                    {
                        if (i != msg->nparams - 1)
                        {
                            chirc_message_add_parameter(user_node->msg, msg->params[i], false);
                        }
                        else
                        {
                            chirc_message_add_parameter(user_node->msg, msg->params[i], true);
                        }
                    }

                    user_node->next = NULL;
                    if (NULL == user_head)
                    {
                        user_head = user_node;
                    }
                    else
                    {
                        add_user_node(user_head, user_node);
                    }

                    nick_node = find_user_node(nick_head, name);
                    if (nick_node != NULL)
                    {
                        if (4 == msg->nparams)
                        {
                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Welcome to the Internet Relay Network %s!%s@%s", nick_node->name, user_node->name, ctx->network.this_server->servername);
                            connection_map_t *connection_node = (connection_map_t *)malloc(sizeof(connection_map_t));
                            memset(connection_node->name, 0, sizeof(connection_node->name));
                            memcpy(connection_node->name, nick_node->name, strlen(nick_node->name));
                            connection_node->msg = NULL;
                            connection_node->msg = (chirc_message_t *)malloc(sizeof(chirc_message_t));
                            chirc_message_construct(connection_node->msg, NULL, msg->cmd);
                            for (int i = 0; i < msg->nparams; ++i)
                            {
                                if (i != msg->nparams - 1)
                                {
                                    chirc_message_add_parameter(connection_node->msg, msg->params[i], false);
                                }
                                else
                                {
                                    chirc_message_add_parameter(connection_node->msg, msg->params[i], true);
                                }
                            }

                            connection_node->fd = sockfd;
                            add_connection_map_node(&connection_hash, connection_node);
                            my_construct_user_reply(ctx, RPL_WELCOME, buf, NULL, nick_node->name, sockfd);

                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "Your host is %s, running version 1.0", ctx->network.this_server->servername);
                            my_construct_user_reply(ctx, RPL_YOURHOST, buf, NULL, name, sockfd);

                            memset(buf, 0, sizeof(buf));
                            sprintf(buf, "This server was created 20240701");
                            my_construct_user_reply(ctx, RPL_CREATED, buf, NULL, name, sockfd);

                            my_construct_user_RPL_MYINFO_reply(ctx, RPL_MYINFO, NULL, name, sockfd, "1.0", "ao", "mtov");

                            response_LUSERS(ctx, name, sockfd);

                            response_MOTD(ctx, name, sockfd, false);

                            sockfd_nick_map_t *sockfd_nick_node = (sockfd_nick_map_t *)malloc(sizeof(sockfd_nick_map_t));
                            memset(sockfd_nick_node->name, 0, sizeof(sockfd_nick_node->name));
                            memcpy(sockfd_nick_node->name, nick_node->name, strlen(nick_node->name));
                            sockfd_nick_node->fd = sockfd;
                            add_sockfd_nick_map_node(&sockfd_nick_hash, sockfd_nick_node);
                        }
                    }
                }
            }
            else
            {
                sockfd_nick_map_t *sockfd_nick_node = find_sockfd_nick_map_node(sockfd_nick_hash, sockfd);
                if (sockfd_nick_node != NULL)
                {
                    my_construct_user_UNKNOWN_reply(ctx, ERR_UNKNOWNCOMMAND, sockfd_nick_node->name, msg->cmd, "Unknown command", sockfd);
                }
            }

            chirc_message_free(msg);
            free(msg);
            msg = NULL;
        }
    }

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
        if (sockfd < 0)
        {
            chilog(ERROR, "failed to accept!");
            break;
        }

        pthread_t tid;
        thread_data_t *data = (thread_data_t *)malloc(sizeof(thread_data_t));
        data->ctx = ctx;
        data->sockfd = sockfd;
        atomic_fetch_add(&connection_count, 1);

        pthread_create(&tid, NULL, subthread_work, data);
        pthread_detach(tid);
    }

_error:
    chilog(INFO, "program is exited!");
    close(listenfd);

    free_connection_map_node(&connection_hash);
    free_sockfd_nick_map_node(&sockfd_nick_hash);

    pthread_mutex_destroy(&user_node_mutex);
    pthread_mutex_destroy(&connection_node_mutex);
    pthread_mutex_destroy(&sockfd_nick_node_mutex);
    free_user_node(nick_head);
    free_user_node(user_head);

    return ret;
}