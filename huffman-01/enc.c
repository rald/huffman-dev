/* enc.c - Real-World Huffman File Compressor */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#define PQUEUE_IMPLEMENTATION
#include "pqueue.h"

#define MAXN 256
#define MAXCODE 64
#define MAGIC_NUMBER 0x48554646  /* "HUFF" */
#define VERSION 1
#define BLOCK_SIZE 65536

/* Binary file format structures */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint64_t original_size;
    uint64_t compressed_size;
    uint32_t checksum;
    uint16_t tree_size;
    uint8_t padding_bits;
    uint8_t reserved;
} __attribute__((packed)) HuffHeader;

typedef struct {
    uint8_t ch;
    uint32_t freq;
} __attribute__((packed)) FreqEntry;

typedef struct {
    uint8_t *code;
    uint8_t len;
} CodeEntry;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint8_t bit_buffer;
    uint8_t bits_used;
} BitBuffer;

/* Bit buffer operations for binary compression */
static BitBuffer *bitBufferInit(size_t initial_size) {
    BitBuffer *buf = malloc(sizeof(BitBuffer));
    if (!buf) return NULL;
    
    buf->data = malloc(initial_size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->size = 0;
    buf->capacity = initial_size;
    buf->bit_buffer = 0;
    buf->bits_used = 0;
    return buf;
}

static void bitBufferEnsure(BitBuffer *buf, size_t needed) {
    if (buf->size + needed >= buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        while (new_cap < buf->size + needed) new_cap *= 2;
        buf->data = realloc(buf->data, new_cap);
        buf->capacity = new_cap;
    }
}

static void bitBufferWriteBits(BitBuffer *buf, uint32_t bits, int count) {
    for (int i = count - 1; i >= 0; i--) {
        buf->bit_buffer = (buf->bit_buffer << 1) | ((bits >> i) & 1);
        buf->bits_used++;
        
        if (buf->bits_used == 8) {
            bitBufferEnsure(buf, 1);
            buf->data[buf->size++] = buf->bit_buffer;
            buf->bit_buffer = 0;
            buf->bits_used = 0;
        }
    }
}

static void bitBufferFlush(BitBuffer *buf) {
    if (buf->bits_used > 0) {
        buf->bit_buffer <<= (8 - buf->bits_used);
        bitBufferEnsure(buf, 1);
        buf->data[buf->size++] = buf->bit_buffer;
    }
}

/* Fast CRC32 checksum */
static uint32_t crc32_table[256];
static int crc32_initialized = 0;

static void init_crc32() {
    if (crc32_initialized) return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* Optimized frequency counting */
static void buildFreqTable(const uint8_t *data, size_t len, uint32_t freq[]) {
    memset(freq, 0, MAXN * sizeof(uint32_t));
    
    /* Process in chunks for cache efficiency */
    for (size_t i = 0; i < len; i++) {
        freq[data[i]]++;
    }
}

/* Build canonical Huffman tree */
static Node *buildHuffmanTree(uint32_t freq[]) {
    PQ *pq = PQinit(MAXN);
    if (!pq) return NULL;
    
    int symbols = 0;
    for (int i = 0; i < MAXN; i++) {
        if (freq[i] > 0) {
            PQinsert(pq, newNode(i, freq[i], NULL, NULL));
            symbols++;
        }
    }
    
    if (symbols == 0) {
        PQfree(pq);
        return NULL;
    }
    
    if (symbols == 1) {
        Node *single = PQdelmin(pq);
        PQfree(pq);
        return single;
    }
    
    while (symbols > 1) {
        Node *left = PQdelmin(pq);
        Node *right = PQdelmin(pq);
        Node *merged = newNode(0, left->freq + right->freq, left, right);
        PQinsert(pq, merged);
        symbols--;
    }
    
    Node *root = PQdelmin(pq);
    PQfree(pq);
    return root;
}

/* Generate binary codes */
static void generateCodes(Node *root, CodeEntry codes[], uint32_t code, int depth) {
    if (!root) return;
    
    if (!root->left && !root->right) {
        codes[root->ch].code = calloc((depth + 7) / 8, 1);  /* Use calloc to zero-initialize */
        codes[root->ch].len = depth;
        
        /* Pack bits efficiently */
        for (int i = 0; i < depth; i++) {
            if ((code >> (depth - 1 - i)) & 1) {
                codes[root->ch].code[i / 8] |= (1 << (7 - (i % 8)));
            }
        }
        return;
    }
    
    if (root->left) {
        generateCodes(root->left, codes, code << 1, depth + 1);
    }
    if (root->right) {
        generateCodes(root->right, codes, (code << 1) | 1, depth + 1);
    }
}

/* Compress data using generated codes */
static BitBuffer *compress(const uint8_t *data, size_t len, CodeEntry codes[]) {
    BitBuffer *buf = bitBufferInit(len);
    if (!buf) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        CodeEntry *entry = &codes[data[i]];
        
        if (entry->len == 0) {
            /* This character has no code - should not happen */
            fprintf(stderr, "Error: No code for character %d\n", data[i]);
            free(buf->data);
            free(buf);
            return NULL;
        }
        
        /* Write bits from the code */
        for (int bit = 0; bit < entry->len; bit++) {
            int byte_idx = bit / 8;
            int bit_idx = 7 - (bit % 8);
            uint32_t bit_val = (entry->code[byte_idx] >> bit_idx) & 1;
            bitBufferWriteBits(buf, bit_val, 1);
        }
    }
    
    bitBufferFlush(buf);
    return buf;
}

/* Write binary header */
static int writeHeader(FILE *fp, const HuffHeader *header) {
    return fwrite(header, sizeof(HuffHeader), 1, fp) == 1;
}

/* Write frequency table in binary format */
static uint16_t writeFreqTable(FILE *fp, uint32_t freq[]) {
    uint16_t tree_size = 0;
    
    for (int i = 0; i < MAXN; i++) {
        if (freq[i] > 0) {
            FreqEntry entry = { .ch = i, .freq = freq[i] };
            if (fwrite(&entry, sizeof(FreqEntry), 1, fp) != 1) {
                return 0;
            }
            tree_size++;
        }
    }
    return tree_size;
}

/* Get file size */
static long getFileSize(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

/* Progress callback */
static void showProgress(const char *operation, size_t current, size_t total) {
    static time_t last_update = 0;
    time_t now = time(NULL);
    
    if (now > last_update) {
        double percent = (double)current / total * 100.0;
        fprintf(stderr, "\r%s: %.1f%% (%zu/%zu bytes)", 
                operation, percent, current, total);
        fflush(stderr);
        last_update = now;
    }
}

int main(int argc, char *argv[]) {
    int verbose = 0;
    int force = 0;
    char *input_file = NULL;
    char *output_file = NULL;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] <input_file> [output_file]\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose    Show compression statistics\n");
            printf("  -f, --force      Overwrite existing files\n");
            printf("  -h, --help       Show this help\n");
            return 0;
        } else if (!input_file) {
            input_file = argv[i];
        } else if (!output_file) {
            output_file = argv[i];
        }
    }
    
    if (!input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Use %s --help for usage information\n", argv[0]);
        return 1;
    }
    
    /* Generate output filename if not provided */
    if (!output_file) {
        output_file = malloc(strlen(input_file) + 6);
        sprintf(output_file, "%s.huff", input_file);
    }
    
    /* Check if output file exists */
    if (!force && getFileSize(output_file) >= 0) {
        fprintf(stderr, "Error: Output file '%s' already exists (use -f to overwrite)\n", output_file);
        return 1;
    }
    
    init_crc32();
    
    /* Read input file */
    FILE *infile = fopen(input_file, "rb");
    if (!infile) {
        perror("Error opening input file");
        return 1;
    }
    
    long file_size = getFileSize(input_file);
    if (file_size <= 0) {
        fprintf(stderr, "Error: Input file is empty or cannot read size\n");
        fclose(infile);
        return 1;
    }
    
    uint8_t *data = malloc(file_size);
    if (!data) {
        fprintf(stderr, "Error: Cannot allocate memory for file (%ld bytes)\n", file_size);
        fclose(infile);
        return 1;
    }
    
    if (verbose) showProgress("Reading", 0, file_size);
    size_t bytes_read = fread(data, 1, file_size, infile);
    if (bytes_read != file_size) {
        fprintf(stderr, "Error: Could not read entire file\n");
        free(data);
        fclose(infile);
        return 1;
    }
    fclose(infile);
    
    if (verbose) {
        fprintf(stderr, "\rRead %ld bytes from '%s'\n", file_size, input_file);
        showProgress("Analyzing", 0, file_size);
    }
    
    /* Build frequency table */
    uint32_t freq[MAXN];
    buildFreqTable(data, file_size, freq);
    
    /* Build Huffman tree */
    Node *root = buildHuffmanTree(freq);
    if (!root) {
        fprintf(stderr, "Error: Failed to build Huffman tree\n");
        free(data);
        return 1;
    }
    
    /* Generate codes */
    CodeEntry codes[MAXN];
    memset(codes, 0, sizeof(codes));
    
    if (root->left || root->right) {
        generateCodes(root, codes, 0, 0);
    } else {
        /* Single symbol file */
        codes[root->ch].code = malloc(1);
        codes[root->ch].code[0] = 0;
        codes[root->ch].len = 1;
    }
    
    if (verbose) showProgress("Compressing", 0, file_size);
    
    /* Compress data */
    BitBuffer *compressed = compress(data, file_size, codes);
    if (!compressed) {
        fprintf(stderr, "Error: Compression failed\n");
        free(data);
        freeTree(root);
        return 1;
    }
    
    /* Calculate checksum */
    uint32_t checksum = crc32(data, file_size);
    
    /* Prepare header */
    HuffHeader header = {
        .magic = MAGIC_NUMBER,
        .version = VERSION,
        .original_size = file_size,
        .compressed_size = compressed->size,
        .checksum = checksum,
        .tree_size = 0,  /* Will be updated after writing frequency table */
        .padding_bits = compressed->bits_used > 0 ? (8 - compressed->bits_used) : 0,
        .reserved = 0
    };
    
    /* Write compressed file */
    FILE *outfile = fopen(output_file, "wb");
    if (!outfile) {
        perror("Error opening output file");
        free(data);
        free(compressed->data);
        free(compressed);
        freeTree(root);
        return 1;
    }
    
    /* Write header */
    if (!writeHeader(outfile, &header)) {
        fprintf(stderr, "Error: Could not write header\n");
        fclose(outfile);
        return 1;
    }
    
    /* Write frequency table */
    uint16_t tree_size = writeFreqTable(outfile, freq);
    if (tree_size == 0) {
        fprintf(stderr, "Error: Could not write frequency table\n");
        fclose(outfile);
        return 1;
    }
    
    /* Update header with tree size */
    header.tree_size = tree_size;
    fseek(outfile, 0, SEEK_SET);
    writeHeader(outfile, &header);
    fseek(outfile, 0, SEEK_END);
    
    /* Write compressed data */
    if (fwrite(compressed->data, 1, compressed->size, outfile) != compressed->size) {
        fprintf(stderr, "Error: Could not write compressed data\n");
        fclose(outfile);
        return 1;
    }
    
    fclose(outfile);
    
    /* Show results */
    if (verbose) {
        fprintf(stderr, "\rCompression complete!\n");
        fprintf(stderr, "Original size:    %ld bytes\n", file_size);
        fprintf(stderr, "Compressed size:  %zu bytes\n", compressed->size + sizeof(HuffHeader) + tree_size * sizeof(FreqEntry));
        fprintf(stderr, "Compression ratio: %.2f%%\n", 
                100.0 * (1.0 - (double)(compressed->size + sizeof(HuffHeader) + tree_size * sizeof(FreqEntry)) / file_size));
        fprintf(stderr, "Output file: '%s'\n", output_file);
    }
    
    /* Cleanup */
    for (int i = 0; i < MAXN; i++) {
        free(codes[i].code);
    }
    free(data);
    free(compressed->data);
    free(compressed);
    freeTree(root);
    
    if (output_file != argv[argc-1]) free(output_file);
    
    return 0;
}
