#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>

// You would typically include headers for a cryptography library here
// For example:
// #include <openssl/ec.h>
// #include <openssl/sha.h>
// #include <openssl/ripemd.h>

#define NUM_THREADS 8

typedef struct {
    uint64_t high;
    uint64_t low;
} uint71_t;

// 71-bit range: 0x400000000000000000 to 0x7FFFFFFFFFFFFFFFFF
const uint71_t KEY_MIN = {0x40, 0x0000000000000000};
const uint71_t KEY_MAX = {0x7F, 0xFFFFFFFFFFFFFFFF};

atomic_uint_fast64_t attempts = 0;
atomic_bool found = false;
uint71_t found_key;
struct timespec start_time;

// This would be your actual target Bitcoin address
const char target_wallet[] = "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU";
int puzzle_number = 71;

// Thread-local RNG
__thread uint64_t s[2];
__thread uint64_t local_attempts = 0;

pthread_mutex_t found_mutex = PTHREAD_MUTEX_INITIALIZER;

void seed_rng(uint64_t seed) {
    s[0] = seed;
    s[1] = seed ^ 0xdeadbeefcafebabeULL;
}

uint64_t xorshift128plus() {
    uint64_t x = s[0];
    const uint64_t y = s[1];
    s[0] = y;
    x ^= x << 23;
    s[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
    return s[1] + y;
}

uint71_t random_71bit_key() {
    uint71_t key;
    // Generate high 7 bits (0x40-0x7F)
    key.high = (xorshift128plus() & 0x3F) | 0x40;
    // Generate low 64 bits
    key.low = xorshift128plus();
    return key;
}

void print_key(uint71_t key) {
    printf("%02lx%016lx", key.high, key.low);
}

/**
 * @brief Placeholder function for actual Bitcoin address generation.
 * This function needs to be replaced with a real cryptographic implementation.
 *
 * @param key The 71-bit private key.
 * @param address A buffer to store the generated Bitcoin address (P2PKH).
 * @return True if address generation is successful, false otherwise.
 */
bool generate_bitcoin_address(uint71_t key, char* address) {
    //
    // !!! IMPORTANT: THIS IS A PLACEHOLDER !!!
    //
    // To make this functional, you need to implement the full Bitcoin address
    // derivation process here. This involves:
    // 1. Converting the 71-bit key to a 256-bit private key (e.g., extend with leading zeros).
    // 2. Performing elliptic curve multiplication (private key * G) to get the public key.
    // 3. Hashing the public key (SHA-256 then RIPEMD-160).
    // 4. Adding the network byte (0x00 for mainnet P2PKH).
    // 5. Calculating the checksum (double SHA-256 of the previous result).
    // 6. Appending the checksum.
    // 7. Base58Check encoding the final bytes.
    //
    // Libraries like OpenSSL or libsecp256k1 can help with ECC and hashing.
    // Base58Check encoding would need to be implemented or found in a library.
    //

    // For demonstration, let's use the provided mock address generation but
    // make it clear it's not real Bitcoin address generation.
    // This is still NOT a real Bitcoin address.
    const char* base58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint64_t hash = key.high ^ key.low;

    address[0] = '1'; // P2PKH address prefix

    for (int i = 1; i < 34; i++) {
        hash = hash * 0x5BD1E995 + 0x7FFFFFFF;
        address[i] = base58[(hash >> (i % 8)) % 58];
    }
    address[34] = '\0';

    // If you were to implement actual Bitcoin address generation,
    // you would return true on success, false on failure.
    return true; // Always true for this mock version
}


bool check_key(uint71_t key, const char* target_address) {
    char generated_address[35]; // Bitcoin addresses are typically 26-35 chars long, plus null terminator

    if (!generate_bitcoin_address(key, generated_address)) {
        return false; // Failed to generate address
    }

    return strcmp(generated_address, target_address) == 0;
}

void save_key_to_file(uint71_t key, const char* final_address) {
    FILE *fp = fopen("FOUND.txt", "w");
    if (fp == NULL) {
        perror("Error opening file");
        return;
    }
    fprintf(fp, "Found 71-bit puzzle key:\n");
    fprintf(fp, "Wallet: %s\n", target_wallet);
    fprintf(fp, "Key (hex): %02lx%016lx\n", key.high, key.low);
    fprintf(fp, "Address: %s\n", final_address);

    fclose(fp);
}

void* hunt_keys(void* arg) {
    long thread_id = (long)arg;
    uint64_t seed = ((uint64_t)time(NULL) << 32) ^ (uintptr_t)&seed_rng ^ thread_id;
    seed_rng(seed);

    //char current_generated_address[35];

    while (!atomic_load(&found)) {
        uint71_t key = random_71bit_key();
        local_attempts++;
        atomic_fetch_add(&attempts, 1);

        // Generate the address for the current key
        //if (!generate_bitcoin_address(key, current_generated_address)) {
            // Handle error in address generation if necessary
        //    continue;
        //}

        if (check_key(key, target_wallet)) {
            pthread_mutex_lock(&found_mutex);
            if (!atomic_load(&found)) {
                atomic_store(&found, true);
                found_key = key;
                printf("\n\n*** 71-BIT KEY FOUND! ***\n");
                printf("Key (hex): ");
                print_key(key);
                printf("\n");
                ///printf("Generated Address: %s\n", current_generated_address);
                //save_key_to_file(key, current_generated_address);
            }
            pthread_mutex_unlock(&found_mutex);
            break;
        }

        if (local_attempts % 1000000 == 0 && !atomic_load(&found)) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start_time.tv_sec) + (now.tv_nsec - start_time.tv_nsec) / 1e9;
            double speed = atomic_load(&attempts) / elapsed / 1e6;

            printf("\033[2J\033[H");
            printf("\r\033[4A");
            printf("* 71-Bit Bitcoin Puzzle Solver\n");
            printf("* Range: 0x400000000000000000 to 0x7FFFFFFFFFFFFFFFFF\n");
            printf("* Decimal: 1180591620717411303424 to 2361183241434822606847\n");
            printf("* Target Wallet: %s\n", target_wallet);
            printf("* Attempts: %lu\n", atomic_load(&attempts));
            printf("* Current Key: 0x");
            print_key(key);
            //printf(" | Current Address: %s\n", current_generated_address);
            printf("\n* Speed: %.2f Mkeys/sec | Threads: %d\n", speed, NUM_THREADS);
            fflush(stdout);
        }
    }
    return NULL;
}

int main() {
    printf("71-Bit Bitcoin Puzzle Solver\n");
    printf("Range: 0x400000000000000000 to 0x7FFFFFFFFFFFFFFFFF\n");
    printf("Decimal: 1180591620717411303424 to 2361183241434822606847\n");
    printf("Target: %s\n", target_wallet);

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    pthread_t threads[NUM_THREADS];
    for (long i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, hunt_keys, (void*)i);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    if (atomic_load(&found)) {
        printf("\nKey found and saved to FOUND.txt\n");
    } else {
        printf("\nSearch stopped\n");
    }

    pthread_mutex_destroy(&found_mutex);
    return 0;
}
