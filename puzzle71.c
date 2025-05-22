
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/ripemd.h>

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

const char target_wallet[] = "f6f5431d25bbf7b12e8add9af5e3475c44a0a5b8"; // RIPEMD160 (H160) target

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
    key.high = (xorshift128plus() & 0x3F) | 0x40; // high 7 bits in 0x40–0x7F
    key.low = xorshift128plus();
    return key;
}

void print_key(uint71_t key) {
    printf("%02lx%016lx", key.high, key.low);
}

void save_key_to_file(uint71_t key) {
    FILE *fp = fopen("FOUND.txt", "w");
    if (fp == NULL) {
        perror("Error opening file");
        return;
    }
    fprintf(fp, "%02lx%016lx\n", key.high, key.low);
    fclose(fp);
}

void generate_rmd160(uint71_t key, char* address_out) {
    unsigned char pubkey_bytes[9];
    pubkey_bytes[0] = (uint8_t)key.high;
    for (int i = 1; i < 9; i++) {
        pubkey_bytes[i] = (key.low >> (8 * (i - 1))) & 0xFF;
    }

    unsigned char sha256_hash[SHA256_DIGEST_LENGTH];
    SHA256(pubkey_bytes, sizeof(pubkey_bytes), sha256_hash);

    unsigned char ripemd160_hash[RIPEMD160_DIGEST_LENGTH];
    unsigned int ripemd160_len;

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        strcpy(address_out, "EVP_FAIL");
        return;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_ripemd160(), NULL) != 1 ||
        EVP_DigestUpdate(mdctx, sha256_hash, SHA256_DIGEST_LENGTH) != 1 ||
        EVP_DigestFinal_ex(mdctx, ripemd160_hash, &ripemd160_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        strcpy(address_out, "EVP_FAIL");
        return;
    }
    EVP_MD_CTX_free(mdctx);

    for (unsigned int i = 0; i < ripemd160_len; i++) {
        sprintf(address_out + i * 2, "%02x", ripemd160_hash[i]);
    }
    address_out[40] = '\0';
}

void* thread_func(void* arg) {
    seed_rng((uintptr_t)arg ^ time(NULL));

    while (!found) {
        uint71_t key = random_71bit_key();

        char address[41];
        generate_rmd160(key, address);

        if ((atomic_load(&attempts) % 200000) == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start_time.tv_sec) + (now.tv_nsec - start_time.tv_nsec) / 1e9;
            double speed = atomic_load(&attempts) / (elapsed * 1e6);
            printf("\033[2J\033[H");
            printf("\r\033[4A");
            printf("* 71-Bit Bitcoin Puzzle Solver\n");
            printf("* Range: 0x400000000000000000 to 0x7FFFFFFFFFFFFFFFFF\n");
            printf("* Decimal: 1180591620717411303424 to 2361183241434822606847\n");
            printf("* Target Wallet (RMD160): %s\n", target_wallet);
            printf("* Attempts: %lu\n", atomic_load(&attempts));
            printf("* Current Key: 0x");
            print_key(key);
            printf(" | Current RMD160: %s\n", address);
            printf("* Speed: %.2f Mkeys/sec | Threads: %d\n", speed, NUM_THREADS);
        }

        if (strcmp(address, target_wallet) == 0) {
            if (!found) {
                pthread_mutex_lock(&found_mutex);
                if (!found) {
                    found_key = key;
                    found = true;
                    save_key_to_file(key);
                    printf("\nFOUND! Private Key: ");
                    print_key(key);
                    printf("\n");
                }
                pthread_mutex_unlock(&found_mutex);
            }
            break;
        }

        atomic_fetch_add(&attempts, 1);
    }
    return NULL;
}

int main() {
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    pthread_t threads[NUM_THREADS];
    for (long i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_func, (void*)i);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Total attempts: %lu\n", attempts);
    printf("Elapsed time: %.2f seconds\n", elapsed);
    return 0;
}
