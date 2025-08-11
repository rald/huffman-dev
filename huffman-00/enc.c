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
void storeCodes(Node* root, char* codes[], char current_code[], int top);
void freeTree(Node* root);

// Main Encoder Function
void encodeFile(const char* input_path, const char* output_path);


// --- Main Driver ---

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_to_compress>\n", argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];
    char encoded_filename[256];

    // Create output filename
    snprintf(encoded_filename, sizeof(encoded_filename), "%s.enc", input_filename);

    printf("--- Huffman File Encoder ---\n");
    printf("Input file:    %s\n", input_filename);
    printf("Output file:   %s\n", encoded_filename);
    
    encodeFile(input_filename, encoded_filename);
    
    printf("Encoding complete.\n");

    return 0;
}


// --- Encoder Implementation ---

void encodeFile(const char* input_path, const char* output_path) {
    // 1. Calculate character frequencies from the input file
    unsigned freq[MAX_CHARS] = {0};
    long original_file_size = 0;
    FILE* input_file = fopen(input_path, "rb");
    if (!input_file) {
        perror("Error opening input file");
        exit(1);
    }
    int c;
    while ((c = fgetc(input_file)) != EOF) {
        freq[c]++;
        original_file_size++;
    }
    rewind(input_file);

    // 2. Build Huffman Tree and generate codes
    Node* root = buildHuffmanTree(freq, MAX_CHARS);
    char* codes[MAX_CHARS];
    char current_code[MAX_CHARS];
    for(int i = 0; i < MAX_CHARS; ++i) codes[i] = NULL;
    storeCodes(root, codes, current_code, 0);

    // 3. Write header and compressed data to the output file
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        perror("Error opening output file");
        exit(1);
    }

    // Write header: first, the original file size, then the frequency table
    fwrite(&original_file_size, sizeof(long), 1, output_file);
    fwrite(freq, sizeof(unsigned), MAX_CHARS, output_file);

    // Write body: the compressed bitstream
    unsigned char buffer = 0;
    int bit_count = 0;
    while ((c = fgetc(input_file)) != EOF) {
        char* code = codes[c];
        if (code) { // Ensure code exists
            while (*code) {
                buffer <<= 1;
                if (*code == '1') {
                    buffer |= 1;
                }
                bit_count++;
                if (bit_count == 8) {
                    fwrite(&buffer, sizeof(unsigned char), 1, output_file);
                    buffer = 0;
                    bit_count = 0;
                }
                code++;
            }
        }
    }

    // Write any remaining bits in the buffer (padding with 0s)
    if (bit_count > 0) {
        buffer <<= (8 - bit_count);
        fwrite(&buffer, sizeof(unsigned char), 1, output_file);
    }

    // --- Cleanup ---
    fclose(input_file);
    fclose(output_file);
    freeTree(root);
    for (int i = 0; i < MAX_CHARS; i++) {
        if (codes[i]) free(codes[i]);
    }
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
        Node* single_node = minHeap->array[0]; // Don't extract, just use
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

void storeCodes(Node* root, char* codes[], char current_code[], int top) {
    if (root->left) {
        current_code[top] = '0';
        storeCodes(root->left, codes, current_code, top + 1);
    }
    if (root->right) {
        current_code[top] = '1';
        storeCodes(root->right, codes, current_code, top + 1);
    }
    if (!root->left && !root->right) {
        current_code[top] = '\0';
        codes[(unsigned char)root->data] = strdup(current_code);
    }
}

void freeTree(Node* root) {
    if (root == NULL) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
}

