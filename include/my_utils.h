#ifndef __MY_UTILS_
#define __MY_UTILS_

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "uthash.h"
#include "log.h"
#include "chirc.h"

pthread_mutex_t user_node_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t connection_node_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sockfd_nick_node_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct user_node
{
    char name[128];
    chirc_message_t *msg;
    struct user_node *next;
} user_node_t;

typedef struct connection_map
{
    char name[128];
    int fd;
    chirc_message_t *msg;

    UT_hash_handle hh;
}connection_map_t;

typedef struct sockfd_nick_map
{
    int fd;
    char name[128];

    UT_hash_handle hh;
}sockfd_nick_map_t;

void add_user_node(user_node_t* head, user_node_t* node)
{
    pthread_mutex_lock(&user_node_mutex);

    while (head != NULL && head->next != NULL)
    {
        head = head->next;
    }

    head->next = node;

    pthread_mutex_unlock(&user_node_mutex);
}

void del_user_node(user_node_t* head, char *name)
{
    pthread_mutex_lock(&user_node_mutex);
    int len = strlen(name);
    user_node_t *prev = NULL;

    while (head != NULL)
    {
        if(strlen(head->name) == len)
        {
            if(0 == strncmp(head->name, name, len))
            {
                if(NULL != prev)
                {
                    prev->next = head->next;
                    free(head);
                    break;
                }
                else
                {
                    free(head);
                    break;
                }
            }
        }

        prev = head;
        head = head->next;
    }

    pthread_mutex_unlock(&user_node_mutex);
}


user_node_t* find_user_node(user_node_t* head, char *name)
{
    pthread_mutex_lock(&user_node_mutex);

    int len = strlen(name);
    user_node_t *node = NULL;

    while (head != NULL)
    {
        if(strlen(head->name) == len)
        {
            if(0 == strncmp(head->name, name, len))
            {
                node = head;
                break;
            }
        }

        head = head->next;
    }

    pthread_mutex_unlock(&user_node_mutex);
    return node;
}

user_node_t* fuzzy_find_user_node(user_node_t* head, char *name)
{
    int len = strlen(name);
    user_node_t *node = NULL;
    int max_cnt = -1;

    pthread_mutex_lock(&user_node_mutex);
    while (head != NULL)
    {
        if(strlen(head->name) == len)
        {
            int cnt = 0;
            for (int i = 0; i < len; ++i)
            {
                if(name[i] == head->name[i])
                {
                    cnt++;
                }
            }

            if(cnt > max_cnt)
            {
                max_cnt = cnt;
                node = head;
            }
        }

        head = head->next;
    }

    pthread_mutex_unlock(&user_node_mutex);
    return node;
}

user_node_t* get_least_user_node(user_node_t* head)
{
    pthread_mutex_lock(&user_node_mutex);

    while (head != NULL && head->next != NULL)
    {
        head = head->next;
    }

    pthread_mutex_unlock(&user_node_mutex);

    return head;
}

void print_user_node(user_node_t* head)
{
    while(NULL != head)
    {
        chilog(INFO, "node->name is %s", head->name);
        head = head->next;
    }
}

void free_user_node(user_node_t* node)
{
    if(NULL == node)
    {
        return;
    }

    free_user_node(node->next);

    free(node);
}

void add_connection_map_node(connection_map_t** connection_hash, connection_map_t* node)
{
    pthread_mutex_lock(&connection_node_mutex);

    HASH_ADD_STR(*connection_hash, name, node);

    pthread_mutex_unlock(&connection_node_mutex);
}

connection_map_t* find_connection_map_node(connection_map_t* connection_hash, char* name)
{
    pthread_mutex_lock(&connection_node_mutex);

    connection_map_t* node = NULL;
    HASH_FIND_STR(connection_hash, name, node);

    pthread_mutex_unlock(&connection_node_mutex);

    return node;
}

void del_connection_map_node(connection_map_t** connection_hash, connection_map_t* node)
{
    pthread_mutex_lock(&connection_node_mutex);

    HASH_DEL(*connection_hash, node);

    pthread_mutex_unlock(&connection_node_mutex);
}

void free_connection_map_node(connection_map_t** connection_hash)
{
    connection_map_t* node = NULL, *tmp = NULL;
    HASH_ITER(hh, *connection_hash, node, tmp) 
    {
        HASH_DEL(*connection_hash, node);  
    }
}

void print_connection_map_node(connection_map_t* connection_hash)
{
    connection_map_t* node = NULL, *tmp = NULL;
    HASH_ITER(hh, connection_hash, node, tmp) 
    {
        chilog(INFO, "node->name is %s, node->fd is %d", node->name, node->fd);
    }   
}

int get_connection_map_node_size(connection_map_t* connection_hash)
{
    connection_map_t* node = NULL, *tmp = NULL;
    int size = 0;
    HASH_ITER(hh, connection_hash, node, tmp)
    {
        size++;
    }

    return size;
}


void add_sockfd_nick_map_node(sockfd_nick_map_t** sockfd_nick_hash, sockfd_nick_map_t* node)
{
    pthread_mutex_lock(&sockfd_nick_node_mutex);

    HASH_ADD_INT(*sockfd_nick_hash, fd, node);

    pthread_mutex_unlock(&sockfd_nick_node_mutex);
}

sockfd_nick_map_t* find_sockfd_nick_map_node(sockfd_nick_map_t* sockfd_nick_hash, int fd)
{
    pthread_mutex_lock(&sockfd_nick_node_mutex);

    sockfd_nick_map_t* node = NULL;
    HASH_FIND_INT(sockfd_nick_hash, &fd, node);

    pthread_mutex_unlock(&sockfd_nick_node_mutex);

    return node;
}

void del_sockfd_nick_map_node(sockfd_nick_map_t** sockfd_nick_hash, sockfd_nick_map_t* node)
{
    if(NULL == node) return;

    pthread_mutex_lock(&sockfd_nick_node_mutex);

    HASH_DEL(*sockfd_nick_hash, node);
    free(node);

    pthread_mutex_unlock(&sockfd_nick_node_mutex);
}

void free_sockfd_nick_map_node(sockfd_nick_map_t** sockfd_nick_hash)
{
    sockfd_nick_map_t* node = NULL, *tmp = NULL;
    HASH_ITER(hh, *sockfd_nick_hash, node, tmp) 
    {
        HASH_DEL(*sockfd_nick_hash, node);  
    }
}

void print_sockfd_nick_map_node(sockfd_nick_map_t* sockfd_nick_hash)
{
    sockfd_nick_map_t* node = NULL, *tmp = NULL;
    HASH_ITER(hh, sockfd_nick_hash, node, tmp) 
    {
        chilog(INFO, "node->name is %s, node->fd is %d", node->name, node->fd);
    }   
}

int get_sockfd_nick_map_node_size(sockfd_nick_map_t* sockfd_nick_hash)
{
    sockfd_nick_map_t* node = NULL, *tmp = NULL;
    int size = 0;
    HASH_ITER(hh, sockfd_nick_hash, node, tmp)
    {
        size++;
    }

    return size;  
}

#endif