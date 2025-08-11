#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHARS 256 // Using extended ASCII range

// --- Data Structures ---

// A node in the Huffman Tree
typedef struct Node {
    char data;
    unsigned freq;
    struct Node *left, *right;
} Node;

// A Min Heap: used as a priority queue for nodes
typedef struct MinHeap {
    unsigned size;
    unsigned capacity;
    Node** array;
} MinHeap;

// --- Function Prototypes ---

// Huffman Tree and Min Heap Helpers
Node* newNode(char data, unsigned freq);
MinHeap* createMinHeap(unsigned capacity);
void swapNodes(Node** a, Node** b);
void minHeapify(MinHeap* minHeap, int idx);
Node* extractMin(MinHeap* minHeap);
void insertMinHeap(MinHeap* minHeap, Node* node);
void buildMinHeap(MinHeap* minHeap);
Node* buildHuffmanTree(unsigned freq[], int size);
void freeTree(Node* root);

// Main Decoder Function
void decodeFile(const char* input_path, const char* output_path);


// --- Main Driver ---

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_to_decompress.enc>\n", argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];
    char decoded_filename[256];

    // Create output filename by replacing .enc with .dec
    strncpy(decoded_filename, input_filename, sizeof(decoded_filename) - 1);
    decoded_filename[sizeof(decoded_filename) - 1] = '\0'; // Ensure null termination
    
    char* ext = strstr(decoded_filename, ".enc");
    if (ext != NULL && strlen(ext) == 4) { // Make sure it's at the end
        strcpy(ext, ".dec");
    } else {
        // If .enc is not found, just append .dec
        strncat(decoded_filename, ".dec", sizeof(decoded_filename) - strlen(decoded_filename) - 1);
    }


    printf("--- Huffman File Decoder ---\n");
    printf("Input file:    %s\n", input_filename);
    printf("Output file:   %s\n", decoded_filename);
    
    decodeFile(input_filename, decoded_filename);
    
    printf("Decoding complete.\n");

    return 0;
}


// --- Decoder Implementation ---

void decodeFile(const char* input_path, const char* output_path) {
    FILE* input_file = fopen(input_path, "rb");
    if (!input_file) {
        perror("Error opening encoded file");
        exit(1);
    }

    // 1. Read header to get original file size and frequency table
    long original_file_size;
    unsigned freq[MAX_CHARS] = {0};
    
    if (fread(&original_file_size, sizeof(long), 1, input_file) != 1) {
        fprintf(stderr, "Error: Could not read file size from header.\n");
        fclose(input_file);
        exit(1);
    }
    if (fread(freq, sizeof(unsigned), MAX_CHARS, input_file) != MAX_CHARS) {
        fprintf(stderr, "Error: Could not read frequency table from header.\n");
        fclose(input_file);
        exit(1);
    }

    // 2. Rebuild the Huffman Tree from the frequency table
    Node* root = buildHuffmanTree(freq, MAX_CHARS);
    if (root == NULL || (root->left == NULL && root->right == NULL && root->freq == 0)) {
        // Handle empty or single-character file case
        FILE* output_file = fopen(output_path, "wb");
        if(output_file) {
            if(original_file_size > 0 && root && root->freq > 0) {
                 for(long i = 0; i < original_file_size; ++i) {
                    fputc(root->data, output_file);
                }
            }
            fclose(output_file);
        }
        freeTree(root);
        fclose(input_file);
        return;
    }
    Node* current_node = root;

    // 3. Read compressed data and write decoded data
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        perror("Error opening decoded output file");
        freeTree(root);
        fclose(input_file);
        exit(1);
    }

    int c;
    long decoded_count = 0;
    while (decoded_count < original_file_size && (c = fgetc(input_file)) != EOF) {
        for (int i = 7; i >= 0; i--) {
            if (decoded_count >= original_file_size) break;

            int bit = (c >> i) & 1;
            if (bit == 0) {
                current_node = current_node->left;
            } else {
                current_node = current_node->right;
            }

            if (current_node == NULL) {
                 fprintf(stderr, "Error: Invalid Huffman tree path.\n");
                 goto cleanup;
            }

            // If it's a leaf node
            if (current_node->left == NULL && current_node->right == NULL) {
                fputc(current_node->data, output_file);
                decoded_count++;
                current_node = root; // Go back to the root for the next character
            }
        }
    }

cleanup:
    // --- Cleanup ---
    fclose(input_file);
    fclose(output_file);
    freeTree(root);
}


// --- Helper and Utility Functions ---

Node* newNode(char data, unsigned freq) {
    Node* temp = (Node*)malloc(sizeof(Node));
    temp->left = temp->right = NULL;
    temp->data = data;
    temp->freq = freq;
    return temp;
}

MinHeap* createMinHeap(unsigned capacity) {
    MinHeap* minHeap = (MinHeap*)malloc(sizeof(MinHeap));
    minHeap->size = 0;
    minHeap->capacity = capacity;
    minHeap->array = (Node**)malloc(minHeap->capacity * sizeof(Node*));
    return minHeap;
}

void swapNodes(Node** a, Node** b) {
    Node* t = *a;
    *a = *b;
    *b = t;
}

void minHeapify(MinHeap* minHeap, int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    if (left < minHeap->size && minHeap->array[left]->freq < minHeap->array[smallest]->freq)
        smallest = left;

    if (right < minHeap->size && minHeap->array[right]->freq < minHeap->array[smallest]->freq)
        smallest = right;

    if (smallest != idx) {
        swapNodes(&minHeap->array[smallest], &minHeap->array[idx]);
        minHeapify(minHeap, smallest);
    }
}

Node* extractMin(MinHeap* minHeap) {
    Node* temp = minHeap->array[0];
    minHeap->array[0] = minHeap->array[minHeap->size - 1];
    --minHeap->size;
    minHeapify(minHeap, 0);
    return temp;
}

void insertMinHeap(MinHeap* minHeap, Node* node) {
    ++minHeap->size;
    int i = minHeap->size - 1;
    while (i && node->freq < minHeap->array[(i - 1) / 2]->freq) {
        minHeap->array[i] = minHeap->array[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    minHeap->array[i] = node;
}

void buildMinHeap(MinHeap* minHeap) {
    int n = minHeap->size - 1;
    for (int i = (n - 1) / 2; i >= 0; --i)
        minHeapify(minHeap, i);
}

Node* buildHuffmanTree(unsigned freq[], int size) {
    MinHeap* minHeap = createMinHeap(size);
    minHeap->size = 0;
    for (int i = 0; i < size; ++i) {
        if (freq[i] > 0) {
            minHeap->array[minHeap->size++] = newNode((char)i, freq[i]);
        }
    }
    
    if (minHeap->size == 0) {
        free(minHeap->array);
        free(minHeap);
        return newNode('$', 0); 
    }
     if (minHeap->size == 1) {
        Node* single_node = minHeap->array[0];
        Node* root = newNode('$', single_node->freq);
        root->left = single_node;
        // Don't free the heap array as single_node points into it
        free(minHeap);
        return root;
    }

    buildMinHeap(minHeap);
    
    while (minHeap->size != 1) {
        Node* left = extractMin(minHeap);
        Node* right = extractMin(minHeap);
        Node* top = newNode('$', left->freq + right->freq);
        top->left = left;
        top->right = right;
        insertMinHeap(minHeap, top);
    }

    Node* root = extractMin(minHeap);
    free(minHeap->array);
    free(minHeap);
    return root;
}

void freeTree(Node* root) {
    if (root == NULL) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
}

