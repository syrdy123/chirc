#ifndef __MY_UTILS_
#define __MY_UTILS_

#include <stdlib.h>
#include <string.h>

#include "uthash.h"



typedef struct user_map
{
    char name[128];
    int val;

    UT_hash_handle hh;
}user_map_t;

void add_user_map_node(user_map_t* user_hash, user_map_t* node)
{
    HASH_ADD_PTR(user_hash, name, node);
}

user_map_t* find_user_map_node(user_map_t* user_hash, char* name)
{
    user_map_t* node = NULL;
    HASH_FIND_PTR(user_hash, name, node);

    return node;
}

user_map_t* fuzzy_find_user_map_node(user_map_t* user_hash, char* name)
{
    user_map_t* node = NULL, *tmp = NULL;
    int len = strlen(name), max_cnt = -1;

    HASH_ITER(hh, user_hash, node, tmp) 
    {
        int m = strlen(node->name);
        if(len == m)
        {
            int cnt = 0;
            for(int i = 0; i < len; ++i)
            {
                if(name[i] == node->name[i])
                {
                    ++cnt;
                }
            }

            if(cnt > max_cnt)
            {
                max_cnt = cnt;
                return node;
            }
        }
    }

    return NULL;
}

void del_user_map_node(user_map_t* user_hash, user_map_t* node)
{
    HASH_DEL(user_hash, node);
    free(node);
}

void free_user_map_node(user_map_t* user_hash)
{
    user_map_t* node = NULL, *tmp = NULL;
    HASH_ITER(hh, user_hash, node, tmp) 
    {
        HASH_DEL(user_hash, node);  
    }
}

#endif