/* dec.c - Real-World Huffman File Decompressor */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#define PQUEUE_IMPLEMENTATION
#include "pqueue.h"

#define MAXN 256
#define MAGIC_NUMBER 0x48554646  /* "HUFF" */

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
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint8_t bit_buffer;
    uint8_t bits_available;
} BitReader;

/* Bit reader for binary decompression */
static BitReader *bitReaderInit(const uint8_t *data, size_t size) {
    BitReader *reader = malloc(sizeof(BitReader));
    if (!reader) return NULL;
    
    reader->data = data;
    reader->size = size;
    reader->pos = 0;
    reader->bit_buffer = 0;
    reader->bits_available = 0;
    return reader;
}

static int bitReaderReadBit(BitReader *reader) {
    if (reader->bits_available == 0) {
        if (reader->pos >= reader->size) return -1;
        
        reader->bit_buffer = reader->data[reader->pos++];
        reader->bits_available = 8;
    }
    
    int bit = (reader->bit_buffer >> 7) & 1;
    reader->bit_buffer <<= 1;
    reader->bits_available--;
    return bit;
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

/* Read and validate header */
static int readHeader(FILE *fp, HuffHeader *header) {
    if (fread(header, sizeof(HuffHeader), 1, fp) != 1) {
        return 0;
    }
    
    if (header->magic != MAGIC_NUMBER) {
        fprintf(stderr, "Error: Not a valid Huffman compressed file\n");
        return 0;
    }
    
    if (header->version > 1) {
        fprintf(stderr, "Error: Unsupported file version %d\n", header->version);
        return 0;
    }
    
    return 1;
}

/* Read frequency table from binary format */
static int readFreqTable(FILE *fp, uint32_t freq[], uint16_t tree_size) {
    memset(freq, 0, MAXN * sizeof(uint32_t));
    
    for (uint16_t i = 0; i < tree_size; i++) {
        FreqEntry entry;
        if (fread(&entry, sizeof(FreqEntry), 1, fp) != 1) {
            return 0;
        }
        freq[entry.ch] = entry.freq;
    }
    
    return 1;
}

/* Rebuild Huffman tree from frequencies */
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

/* Fast decompression using bit reader */
static uint8_t *decompress(BitReader *reader, Node *root, uint64_t original_size, uint8_t padding_bits) {
    if (!root || original_size == 0) return NULL;
    
    uint8_t *output = malloc(original_size);
    if (!output) return NULL;
    
    Node *current = root;
    size_t output_pos = 0;
    
    /* Handle single symbol tree */
    if (!root->left && !root->right) {
        for (uint64_t i = 0; i < original_size; i++) {
            output[i] = root->ch;
        }
        return output;
    }
    
    /* Decompress bit by bit */
    while (output_pos < original_size) {
        int bit = bitReaderReadBit(reader);
        if (bit < 0) {
            /* Check if we're at the end and within padding */
            size_t bits_processed = (reader->pos - 1) * 8 + (8 - reader->bits_available);
            size_t total_bits = reader->size * 8;
            if (total_bits - bits_processed <= padding_bits) {
                break; /* End of valid data */
            }
            fprintf(stderr, "Unexpected end of data during decompression\n");
            free(output);
            return NULL;
        }
        
        if (bit == 0) {
            current = current->left;
        } else {
            current = current->right;
        }
        
        if (!current) {
            fprintf(stderr, "Invalid path in Huffman tree\n");
            free(output);
            return NULL;
        }
        
        /* Reached leaf node */
        if (!current->left && !current->right) {
            if (output_pos >= original_size) {
                break; /* Prevent buffer overflow */
            }
            output[output_pos++] = current->ch;
            current = root;
        }
    }
    
    return output;
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

/* Get file size */
static long getFileSize(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    int verbose = 0;
    int force = 0;
    int verify = 1;
    char *input_file = NULL;
    char *output_file = NULL;
    
    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "--no-verify") == 0) {
            verify = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options] <input_file> [output_file]\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose      Show decompression progress\n");
            printf("  -f, --force        Overwrite existing files\n");
            printf("  --no-verify        Skip checksum verification\n");
            printf("  -h, --help         Show this help\n");
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
        char *ext = strstr(input_file, ".huff");
        if (ext && ext[5] == '\0') {
            /* Remove .huff extension */
            output_file = malloc(ext - input_file + 1);
            strncpy(output_file, input_file, ext - input_file);
            output_file[ext - input_file] = '\0';
        } else {
            /* Add .dec extension */
            output_file = malloc(strlen(input_file) + 5);
            sprintf(output_file, "%s.dec", input_file);
        }
    }
    
    /* Check if output file exists */
    if (!force && getFileSize(output_file) >= 0) {
        fprintf(stderr, "Error: Output file '%s' already exists (use -f to overwrite)\n", output_file);
        return 1;
    }
    
    init_crc32();
    
    /* Open compressed file */
    FILE *infile = fopen(input_file, "rb");
    if (!infile) {
        perror("Error opening input file");
        return 1;
    }
    
    long file_size = getFileSize(input_file);
    if (file_size <= sizeof(HuffHeader)) {
        fprintf(stderr, "Error: File too small to be valid compressed file\n");
        fclose(infile);
        return 1;
    }
    
    /* Read and validate header */
    HuffHeader header;
    if (!readHeader(infile, &header)) {
        fclose(infile);
        return 1;
    }
    
    if (verbose) {
        fprintf(stderr, "File info:\n");
        fprintf(stderr, "  Original size: %lu bytes\n", header.original_size);
        fprintf(stderr, "  Compressed size: %lu bytes\n", header.compressed_size);
        fprintf(stderr, "  Tree entries: %d\n", header.tree_size);
        showProgress("Reading", 0, header.compressed_size);
    }
    
    /* Read frequency table */
    uint32_t freq[MAXN];
    if (!readFreqTable(infile, freq, header.tree_size)) {
        fprintf(stderr, "Error: Could not read frequency table\n");
        fclose(infile);
        return 1;
    }
    
    /* Read compressed data */
    uint8_t *compressed_data = malloc(header.compressed_size);
    if (!compressed_data) {
        fprintf(stderr, "Error: Cannot allocate memory for compressed data\n");
        fclose(infile);
        return 1;
    }
    
    if (fread(compressed_data, 1, header.compressed_size, infile) != header.compressed_size) {
        fprintf(stderr, "Error: Could not read compressed data\n");
        free(compressed_data);
        fclose(infile);
        return 1;
    }
    fclose(infile);
    
    /* Rebuild Huffman tree */
    Node *root = buildHuffmanTree(freq);
    if (!root) {
        fprintf(stderr, "Error: Could not rebuild Huffman tree\n");
        free(compressed_data);
        return 1;
    }
    
    if (verbose) showProgress("Decompressing", 0, header.original_size);
    
    /* Decompress data */
    BitReader *reader = bitReaderInit(compressed_data, header.compressed_size);
    if (!reader) {
        fprintf(stderr, "Error: Could not initialize bit reader\n");
        free(compressed_data);
        freeTree(root);
        return 1;
    }
    
    uint8_t *decompressed = decompress(reader, root, header.original_size, header.padding_bits);
    if (!decompressed) {
        fprintf(stderr, "Error: Decompression failed\n");
        free(reader);
        free(compressed_data);
        freeTree(root);
        return 1;
    }
    
    /* Verify checksum if requested */
    if (verify) {
        if (verbose) showProgress("Verifying", 0, header.original_size);
        uint32_t calculated_checksum = crc32(decompressed, header.original_size);
        if (calculated_checksum != header.checksum) {
            fprintf(stderr, "Error: Checksum verification failed!\n");
            fprintf(stderr, "Expected: 0x%08X, Calculated: 0x%08X\n", 
                    header.checksum, calculated_checksum);
            free(reader);
            free(compressed_data);
            free(decompressed);
            freeTree(root);
            return 1;
        }
        if (verbose) fprintf(stderr, "\rChecksum verified successfully\n");
    }
    
    /* Write decompressed file */
    FILE *outfile = fopen(output_file, "wb");
    if (!outfile) {
        perror("Error opening output file");
        free(reader);
        free(compressed_data);
        free(decompressed);
        freeTree(root);
        return 1;
    }
    
    if (fwrite(decompressed, 1, header.original_size, outfile) != header.original_size) {
        fprintf(stderr, "Error: Could not write decompressed data\n");
        fclose(outfile);
        return 1;
    }
    fclose(outfile);
    
    if (verbose) {
        fprintf(stderr, "\rDecompression complete!\n");
        fprintf(stderr, "Output file: '%s'\n", output_file);
        fprintf(stderr, "Decompressed %lu bytes successfully\n", header.original_size);
    }
    
    /* Cleanup */
    free(reader);
    free(compressed_data);
    free(decompressed);
    freeTree(root);
    
    if (output_file != argv[argc-1]) free(output_file);
    
    return 0;
}
