#include <cstdio>
#include <cstring>
#include "randomx.h"

int main() {
    const char* expected_hashes[] = {
        "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f",
        "300a0adb47603dedb42228ccb2b211104f4da45af709cd7547cd049e9489c969",
        "c36d4ed4191e617309867ed66a443be4075014e2b061bcdaf9ce7b721d2b77a8",
    };
    
    const char* inputs[] = {
        "This is a test",
        "Lorem ipsum dolor sit amet",
        "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua"
    };
    
    unsigned char hash[32];
    
    printf("============================================================\n");
    printf("x86_64 LINUX - RANDOMX OFFICIAL TEST VECTORS\n");
    printf("============================================================\n\n");
    
    randomx_flags flags = randomx_get_flags();
    printf("RandomX flags: 0x%08x\n", flags);
    printf("  HAVE_AES: %s\n", (flags & RANDOMX_FLAG_HARD_AES) ? "yes" : "no");
    printf("  HAVE_SSSE3: %s\n", (flags & RANDOMX_FLAG_ARGON2_SSSE3) ? "yes" : "no");
    printf("  HAVE_AVX2: %s\n", (flags & RANDOMX_FLAG_ARGON2_AVX2) ? "yes" : "no");
    printf("  JIT: %s\n", (flags & RANDOMX_FLAG_JIT) ? "yes" : "no");
    printf("\n");
    
    randomx_cache* cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT);
    randomx_init_cache(cache, "test key 000", 12);
    randomx_vm* vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT, cache, nullptr);
    
    printf("Testing official RandomX vectors (key='test key 000'):\n\n");
    
    int passed = 0;
    for (int i = 0; i < 3; i++) {
        randomx_calculate_hash(vm, inputs[i], strlen(inputs[i]), hash);
        
        char result[65] = {0};
        for (int j = 0; j < 32; j++) {
            sprintf(result + j*2, "%02x", hash[j]);
        }
        
        bool match = (strcmp(result, expected_hashes[i]) == 0);
        printf("Test %d: %s\n", i+1, match ? "PASS" : "FAIL");
        printf("  Expected: %s\n", expected_hashes[i]);
        printf("  Got:      %s\n", result);
        printf("  Status:   %s\n\n", match ? "IDENTICAL" : "MISMATCH!");
        
        if (match) passed++;
    }
    
    randomx_destroy_vm(vm);
    randomx_release_cache(cache);
    
    printf("===========================================\n");
    printf("Results: %d/3 tests passed\n", passed);
    printf("===========================================\n");
    
    if (passed == 3) {
        printf("\nSUCCESS: x86_64 produces IDENTICAL hashes to official vectors!\n");
    }
    
    return (passed == 3) ? 0 : 1;
}
