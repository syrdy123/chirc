#ifndef __UTILS_LIST_
#define __UTILS_LIST_

#include <string.h>
#include <stdlib.h>

typedef struct user_list
{
    struct user_list* next;
    char user[256];
}user_list_node;

void insert_user_list_node(user_list_node* head, user_list_node* node)
{
        
    while(head != NULL && head->next != NULL)
    {
        head = head->next;
    }

    head->next = node;
}

user_list_node* find_user_list_node(user_list_node* head, char* user)
{
    int len = strlen(user);
    user_list_node* node = NULL;
    int max_cnt = -1;

    while(head)
    {
        int m = strlen(head->user);
        if(m == len)
        {
            int cnt = 0;
            for(int i = 0; i < len; ++i)
            {
                if(head->user[i] == user[i])
                {
                    ++cnt;
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

void del_user_list_node(user_list_node* head, char* user)
{
    user_list_node* prev = NULL;
    int len = strlen(user);

    while(head)
    {
        int m = strlen(head->user);
        if(m == len && 0 == strncmp(head->user, user, len))
        {
            if(prev != NULL)
            {
                prev->next = head->next;
            }

            free(head);
            head = NULL;
        }
    }
}

void free_user_list_node(user_list_node* node)
{
    if(NULL == node) return;

    free_user_list_node(node->next);

    free(node);
}

#endif