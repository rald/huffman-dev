/* pqueue.h - Single Header Priority Queue Library for Huffman Compression
 * 
 * Usage: 
 *   #define PQUEUE_IMPLEMENTATION
 *   #include "pqueue.h"
 */

#ifndef PQUEUE_H
#define PQUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Node structure for Huffman tree */
typedef struct node {
    uint32_t freq;
    uint8_t ch;
    struct node *left, *right;
} Node;

/* Priority Queue structure */
typedef struct {
    Node **heap;
    int n;
    int maxN;
} PQ;

/* Priority Queue Interface */
PQ *PQinit(int maxN);
void PQinsert(PQ *pq, Node *item);
Node *PQdelmin(PQ *pq);
int PQempty(PQ *pq);
void PQfree(PQ *pq);

/* Node Operations Interface */
Node *newNode(uint8_t ch, uint32_t freq, Node *l, Node *r);
void freeTree(Node *root);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * IMPLEMENTATION
 * ============================================================================ */

#ifdef PQUEUE_IMPLEMENTATION

PQ *PQinit(int maxN) {
    PQ *pq = malloc(sizeof(PQ));
    if (!pq) return NULL;
    
    pq->heap = malloc((maxN + 1) * sizeof(Node*));
    if (!pq->heap) {
        free(pq);
        return NULL;
    }
    
    pq->n = 0;
    pq->maxN = maxN;
    return pq;
}

void PQinsert(PQ *pq, Node *item) {
    if (!pq || pq->n >= pq->maxN) return;
    
    int k = ++pq->n;
    
    /* Percolate up - maintain min-heap property */
    while (k > 1 && pq->heap[k/2]->freq > item->freq) {
        pq->heap[k] = pq->heap[k/2];
        k = k/2;
    }
    pq->heap[k] = item;
}

Node *PQdelmin(PQ *pq) {
    if (!pq || pq->n == 0) return NULL;
    
    Node *min = pq->heap[1];
    Node *last = pq->heap[pq->n--];
    
    if (pq->n == 0) return min;
    
    /* Percolate down - maintain min-heap property */
    int k = 1;
    while (2*k <= pq->n) {
        int j = 2*k;
        /* Find smaller child */
        if (j < pq->n && pq->heap[j]->freq > pq->heap[j+1]->freq) j++;
        /* If last element is smaller than child, we're done */
        if (last->freq <= pq->heap[j]->freq) break;
        pq->heap[k] = pq->heap[j];
        k = j;
    }
    pq->heap[k] = last;
    return min;
}

int PQempty(PQ *pq) {
    return !pq || pq->n == 0;
}

void PQfree(PQ *pq) {
    if (pq) {
        free(pq->heap);
        free(pq);
    }
}

Node *newNode(uint8_t ch, uint32_t freq, Node *l, Node *r) {
    Node *node = malloc(sizeof(Node));
    if (!node) return NULL;
    
    node->ch = ch;
    node->freq = freq;
    node->left = l;
    node->right = r;
    return node;
}

void freeTree(Node *root) {
    if (root) {
        freeTree(root->left);
        freeTree(root->right);
        free(root);
    }
}

#endif /* PQUEUE_IMPLEMENTATION */

#endif /* PQUEUE_H */
