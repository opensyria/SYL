#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include "randomx.h"

std::atomic<int> passed{0};
std::atomic<int> failed{0};

void hash_worker(int id, randomx_cache* cache, const char* input, const char* expected) {
    randomx_vm* vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT, cache, nullptr);
    if (!vm) {
        failed++;
        return;
    }
    
    unsigned char hash[32];
    randomx_calculate_hash(vm, input, strlen(input), hash);
    
    char result[65] = {0};
    for (int j = 0; j < 32; j++) {
        sprintf(result + j*2, "%02x", hash[j]);
    }
    
    if (strcmp(result, expected) == 0) {
        passed++;
    } else {
        failed++;
        printf("Thread %d: MISMATCH!\n", id);
    }
    
    randomx_destroy_vm(vm);
}

int main() {
    printf("\n=== ThreadSanitizer Concurrent RandomX Test ===\n\n");
    
    randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
    randomx_init_cache(cache, "test key 000", 12);
    
    printf("Running 8 concurrent hash computations...\n");
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back(hash_worker, i, cache, 
            "This is a test",
            "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f");
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    randomx_release_cache(cache);
    
    printf("\nResults: %d passed, %d failed\n", passed.load(), failed.load());
    
    if (failed == 0 && passed == 8) {
        printf("\nSUCCESS: No data races detected by ThreadSanitizer\n");
        return 0;
    } else {
        printf("\nFAILED: Issues detected\n");
        return 1;
    }
}
