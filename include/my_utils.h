#ifndef __MY_UTILS_
#define __MY_UTILS_

#include <stdlib.h>
#include <string.h>

#include "uthash.h"
#include "log.h"



typedef struct user_node
{
    char name[128];
    struct user_node *next;
} user_node_t;

typedef struct connection_map
{
    char name[128];
    int fd;

    UT_hash_handle hh;
}connection_map_t;

void add_user_node(user_node_t* head, user_node_t* node)
{
    while(head != NULL && head->next != NULL)
    {
        head = head->next;
    }

    head->next = node;
}

void del_user_node(user_node_t* head, char *name)
{
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
}


user_node_t* find_user_node(user_node_t* head, char *name)
{
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

    return node;
}

user_node_t* fuzzy_find_user_node(user_node_t* head, char *name)
{
    int len = strlen(name);
    user_node_t *node = NULL;
    int max_cnt = -1;

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

    return node;
}

user_node_t* get_least_user_node(user_node_t* head)
{
    while(head != NULL && head->next != NULL)
    {
        head = head->next;
    }

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
    HASH_ADD_STR(*connection_hash, name, node);
}

connection_map_t* find_connection_map_node(connection_map_t* connection_hash, char* name)
{
    connection_map_t* node = NULL;
    HASH_FIND_STR(connection_hash, name, node);

    return node;
}

void del_connection_map_node(connection_map_t** connection_hash, connection_map_t* node)
{
    HASH_DEL(*connection_hash, node);
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

#endif