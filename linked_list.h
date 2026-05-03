#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdio.h>
#include <stdlib.h>

/* --- Structure Definition --- */
typedef struct Node {
    char **data;           // Pointer to an array of strings
    int argc;              // Number of arguments in this command
    struct Node *next;     // Pointer to the next node
} Node;

/* --- Function: Create a new node --- */
static inline Node* create_node(char **data, int argc) {
    Node *new_node = (Node *)malloc(sizeof(Node));
    if (!new_node) {
        fprintf(stderr, "Allocation error in linked list\n");
        exit(EXIT_FAILURE);
    }
    new_node->data = data;
    new_node->argc = argc; // Store the argument count
    new_node->next = NULL;
    return new_node;
}

/* --- Function: Insert at the end of the list --- */
static inline void insert_at_end(Node **head, char **data, int argc) {
    Node *new_node = create_node(data, argc);

    if (*head == NULL) {
        *head = new_node;
        return;
    }

    Node *temp = *head;
    while (temp->next != NULL) {
        temp = temp->next;
    }

    temp->next = new_node;
}

/* --- Function: Print all elements --- */
static inline void print_list(Node *head) {
    Node *temp = head;
    int node_count = 0;

    printf("\n--- Linked List Contents ---\n");
    while (temp != NULL) {
        // Print the node number AND the argument count
        printf("Node %d (argc: %d): [ ", node_count, temp->argc);

        if (temp->data != NULL) {
            // We can now safely use a for-loop with our argc!
            for (int i = 0; i < temp->argc; i++) {
                printf("\"%s\" ", temp->data[i]);
            }
        }

        printf("] -> \n");
        temp = temp->next;
        node_count++;
    }
    printf("NULL\n----------------------------\n\n");
}

/* --- Function: Free the list memory --- */
static inline void free_list(Node *head) {
    Node *temp;
    while (head != NULL) {
        temp = head;
        head = head->next;
        free(temp);
    }
}

#endif // LINKED_LIST_H