#include "gitnano.h"
#include <openssl/evp.h>

int sha1_file(const char *path, char *sha1_out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("ERROR: fopen: %d\n", -1);
        return -1;
    }

    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        printf("ERROR: EVP_MD_CTX_new: %d\n", -1);
        fclose(fp);
        return -1;
    }

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        printf("ERROR: EVP_DigestInit_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        fclose(fp);
        return -1;
    }

    unsigned char buffer[8192];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (EVP_DigestUpdate(md_ctx, buffer, bytes_read) != 1) {
            printf("ERROR: EVP_DigestUpdate: %d\n", -1);
            EVP_MD_CTX_free(md_ctx);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
        printf("ERROR: EVP_DigestFinal_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    EVP_MD_CTX_free(md_ctx);

    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(sha1_out + (i * 2), "%02x", digest[i]);
    }
    sha1_out[SHA1_HEX_SIZE - 1] = '\0';

    return 0;
}

int sha1_data(const void *data, size_t size, char *sha1_out) {
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (!md_ctx) {
        printf("ERROR: EVP_MD_CTX_new: %d\n", -1);
        return -1;
    }

    if (EVP_DigestInit_ex(md_ctx, EVP_sha1(), NULL) != 1) {
        printf("ERROR: EVP_DigestInit_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    if (EVP_DigestUpdate(md_ctx, data, size) != 1) {
        printf("ERROR: EVP_DigestUpdate: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(md_ctx, digest, &digest_len) != 1) {
        printf("ERROR: EVP_DigestFinal_ex: %d\n", -1);
        EVP_MD_CTX_free(md_ctx);
        return -1;
    }

    EVP_MD_CTX_free(md_ctx);

    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(sha1_out + (i * 2), "%02x", digest[i]);
    }
    sha1_out[SHA1_HEX_SIZE - 1] = '\0';

    return 0;
}