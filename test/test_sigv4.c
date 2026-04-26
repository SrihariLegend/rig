/* test_sigv4.c — tests for src/ai/providers/sigv4.c */
#include "test.h"
#include "ai/providers/sigv4.h"
#include <stdlib.h>
#include <string.h>

/* ========== Basic signing ========== */

TEST(sign_basic) {
    const char *headers[] = {
        "Content-Type: application/json",
        NULL,
    };

    SigV4Request req = {
        .method = "POST",
        .url = "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/invoke",
        .headers = headers,
        .body = "{\"test\":true}",
        .body_len = 13,
        .region = "us-east-1",
        .service = "bedrock",
        .access_key = "AKIAIOSFODNN7EXAMPLE",
        .secret_key = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
        .session_token = NULL,
    };

    SigV4Headers out;
    int ret = sigv4_sign_request(&req, &out);
    ASSERT_EQ(ret, 0);

    /* Authorization header must start with correct prefix */
    ASSERT_NOT_NULL(out.authorization);
    ASSERT_TRUE(strstr(out.authorization, "Authorization: AWS4-HMAC-SHA256") != NULL);
    ASSERT_TRUE(strstr(out.authorization, "Credential=AKIAIOSFODNN7EXAMPLE/") != NULL);
    ASSERT_TRUE(strstr(out.authorization, "SignedHeaders=") != NULL);
    ASSERT_TRUE(strstr(out.authorization, "Signature=") != NULL);

    /* X-Amz-Date must be present and in correct format */
    ASSERT_NOT_NULL(out.x_amz_date);
    ASSERT_TRUE(strstr(out.x_amz_date, "X-Amz-Date: ") != NULL);
    /* Format: YYYYMMDDTHHMMSSZ — 16 chars after prefix */
    const char *date_val = out.x_amz_date + strlen("X-Amz-Date: ");
    ASSERT_EQ((int)strlen(date_val), 16);
    ASSERT_TRUE(date_val[8] == 'T');
    ASSERT_TRUE(date_val[15] == 'Z');

    /* Content SHA256 must be present — 64 hex chars */
    ASSERT_NOT_NULL(out.x_amz_content_sha256);
    ASSERT_TRUE(strstr(out.x_amz_content_sha256, "X-Amz-Content-Sha256: ") != NULL);
    const char *sha_val = out.x_amz_content_sha256 + strlen("X-Amz-Content-Sha256: ");
    ASSERT_EQ((int)strlen(sha_val), 64);

    /* No security token */
    ASSERT_NULL(out.x_amz_security_token);

    sigv4_headers_free(&out);
}

/* ========== With session token ========== */

TEST(sign_with_session_token) {
    const char *headers[] = {
        "Content-Type: application/json",
        NULL,
    };

    SigV4Request req = {
        .method = "POST",
        .url = "https://bedrock-runtime.us-west-2.amazonaws.com/model/test/invoke",
        .headers = headers,
        .body = "{}",
        .body_len = 2,
        .region = "us-west-2",
        .service = "bedrock",
        .access_key = "ASIAXXX",
        .secret_key = "secretXXX",
        .session_token = "FwoGZXIvYXdzEBAaDHtest123",
    };

    SigV4Headers out;
    int ret = sigv4_sign_request(&req, &out);
    ASSERT_EQ(ret, 0);

    ASSERT_NOT_NULL(out.x_amz_security_token);
    ASSERT_TRUE(strstr(out.x_amz_security_token, "X-Amz-Security-Token: FwoGZXIvYXdzEBAaDHtest123") != NULL);

    /* Authorization should include security token in signed headers */
    ASSERT_TRUE(strstr(out.authorization, "x-amz-security-token") != NULL);

    sigv4_headers_free(&out);
}

/* ========== Empty body ========== */

TEST(sign_empty_body) {
    SigV4Request req = {
        .method = "GET",
        .url = "https://s3.us-east-1.amazonaws.com/my-bucket",
        .headers = NULL,
        .body = NULL,
        .body_len = 0,
        .region = "us-east-1",
        .service = "s3",
        .access_key = "AKID",
        .secret_key = "SECRET",
        .session_token = NULL,
    };

    SigV4Headers out;
    int ret = sigv4_sign_request(&req, &out);
    ASSERT_EQ(ret, 0);
    ASSERT_NOT_NULL(out.authorization);

    /* Empty body should hash to the SHA256 of empty string */
    /* e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    ASSERT_TRUE(strstr(out.x_amz_content_sha256,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") != NULL);

    sigv4_headers_free(&out);
}

/* ========== Null request ========== */

TEST(sign_null_request) {
    SigV4Headers out;
    ASSERT_EQ(sigv4_sign_request(NULL, &out), -1);
    ASSERT_EQ(sigv4_sign_request(NULL, NULL), -1);
}

/* ========== Headers free is idempotent ========== */

TEST(headers_free_null) {
    sigv4_headers_free(NULL); /* must not crash */
}

TEST(headers_free_zeroed) {
    SigV4Headers h = {0};
    sigv4_headers_free(&h); /* must not crash */
}

/* ========== Merge headers ========== */

TEST(merge_headers_basic) {
    const char *orig[] = {
        "Content-Type: application/json",
        "Accept: */*",
        NULL,
    };

    SigV4Headers signed_h = {
        .authorization = "Authorization: AWS4-HMAC-SHA256 ...",
        .x_amz_date = "X-Amz-Date: 20260101T000000Z",
        .x_amz_content_sha256 = "X-Amz-Content-Sha256: abc123",
        .x_amz_security_token = NULL,
    };

    int count = 0;
    const char **merged = sigv4_merge_headers(orig, &signed_h, &count);
    ASSERT_NOT_NULL(merged);
    ASSERT_EQ(count, 5); /* 2 original + 3 signed */
    ASSERT_STR_EQ(merged[0], "Content-Type: application/json");
    ASSERT_STR_EQ(merged[1], "Accept: */*");
    ASSERT_STR_EQ(merged[2], "Authorization: AWS4-HMAC-SHA256 ...");
    ASSERT_NULL(merged[count]); /* NULL terminated */
    free(merged);
}

TEST(merge_headers_with_token) {
    const char *orig[] = { NULL };
    SigV4Headers signed_h = {
        .authorization = "Authorization: test",
        .x_amz_date = "X-Amz-Date: test",
        .x_amz_content_sha256 = "X-Amz-Content-Sha256: test",
        .x_amz_security_token = "X-Amz-Security-Token: tok",
    };

    int count = 0;
    const char **merged = sigv4_merge_headers(orig, &signed_h, &count);
    ASSERT_EQ(count, 4); /* 0 original + 4 signed */
    free(merged);
}

/* ========== Credential scope format ========== */

TEST(sign_credential_scope_format) {
    const char *headers[] = { "Content-Type: application/json", NULL };
    SigV4Request req = {
        .method = "POST",
        .url = "https://bedrock-runtime.eu-west-1.amazonaws.com/model/test/invoke",
        .headers = headers,
        .body = "{}",
        .body_len = 2,
        .region = "eu-west-1",
        .service = "bedrock",
        .access_key = "AKID123",
        .secret_key = "SECRET456",
    };

    SigV4Headers out;
    int ret = sigv4_sign_request(&req, &out);
    ASSERT_EQ(ret, 0);

    /* Credential should contain region and service */
    ASSERT_TRUE(strstr(out.authorization, "eu-west-1/bedrock/aws4_request") != NULL);

    sigv4_headers_free(&out);
}

int main(void) {
    TEST_SUITE("SigV4: Basic Signing");
    RUN_TEST(sign_basic);
    RUN_TEST(sign_with_session_token);
    RUN_TEST(sign_empty_body);

    TEST_SUITE("SigV4: Edge Cases");
    RUN_TEST(sign_null_request);
    RUN_TEST(headers_free_null);
    RUN_TEST(headers_free_zeroed);

    TEST_SUITE("SigV4: Header Merging");
    RUN_TEST(merge_headers_basic);
    RUN_TEST(merge_headers_with_token);

    TEST_SUITE("SigV4: Credential Format");
    RUN_TEST(sign_credential_scope_format);

    TEST_REPORT();
}
