#include <iostream>
#include <cstring>
#include <time.h>
#include "json.hpp"
#include <aws/lambda-runtime/runtime.h>

#include "ow-crypt.h"
#include "rand.hpp"

using json = nlohmann::json;
using aws_response = aws::lambda_runtime::invocation_response;
#define CRYPT_OUTPUT_SIZE		(7 + 22 + 31 + 1)
#define CRYPT_GENSALT_OUTPUT_SIZE	(7 + 22 + 1)
randctx ctx;

void seed_prng() {
    // SEED ISACC SECURE P.R.N.G
    // seed isacc with x+=(x*x) | 5 a well known small p.r.n.g;
    // https://blog.demofox.org/2013/07/07/a-super-tiny-random-number-generator/
    // ----------------------------
    ctx.randa = ctx.randb = ctx.randc = (ub4)0;
    int arbitrary = (int)time(NULL);
    for(int i = 0; i < 24; i++)
        arbitrary += (arbitrary * arbitrary) | 5;

    for (int i=0; i<256; ++i) {
        ctx.randrsl[i] = (ub4)arbitrary;
        arbitrary += (arbitrary * arbitrary) | 5;
    }
    randinit(&ctx, TRUE);
}

void get64rnd(unsigned char* buf) {
    // 16 * 4 == 64
    int cursor = 0;
    for(int i = 0; i < 16; i++) {
        ub4 tmp = rand(&ctx);

        buf[cursor++] = (unsigned char)(tmp & 0xFF);
        buf[cursor++] = (unsigned char)((tmp >> 8) & 0xFF);
        buf[cursor++] = (unsigned char)((tmp >> 16) & 0xFF);
        buf[cursor++] = (unsigned char)((tmp >> 24) & 0xFF);
    }
}

char* gensalt(unsigned long cpu_cost) {
    static char hash[CRYPT_GENSALT_OUTPUT_SIZE] = {0};
    unsigned char rnd_buf[64];
    get64rnd(&rnd_buf[0]);
    crypt_gensalt_rn("$2b$", cpu_cost, (const char*)rnd_buf, 64, hash, CRYPT_GENSALT_OUTPUT_SIZE);
    return hash;
}

char* hash(const char* str, unsigned long cpu_cost) {
    static char hash[CRYPT_OUTPUT_SIZE] = {0};
    crypt_rn(str, gensalt(cpu_cost), hash, CRYPT_OUTPUT_SIZE);
    return hash;
}

bool compare(const char* raw, const char* salted_hash) {
    // hash is always 28 characters long
    char salt[61] = {0};
    strcpy(salt, salted_hash);
    salt[29] = 0;

    static char newhash[CRYPT_OUTPUT_SIZE] = {0};
    crypt_rn(raw, salt, newhash, CRYPT_OUTPUT_SIZE);
    return strcmp(newhash, salted_hash) == 0;
}

static aws::lambda_runtime::invocation_response
    aws_handler(aws::lambda_runtime::invocation_request const& request) {
    if(request.payload.length() > 200) {
        return aws_response::failure("{\"Error\": Too long}", "application/json");
    }

    // Verify action exists
    auto jsonData = json::parse(request.payload);
    if(jsonData.count("action") != 1) {
        return aws_response::failure("{\"Error\": Invalid request}", "application/json");
    }

    // Switch on action
    std::string action = jsonData.at("action");
    if(action == "hash") {
        // Verify text input exists
        if(jsonData.count("text") != 1) {
            return aws_response::failure("{\"Error\": Invalid request}", "application/json");
        }
        std::string rawText = jsonData.at("text");
        char* newHash = hash(rawText.c_str(), 12);

        json response = {
                {"hash", newHash}
        };
        return aws_response::success(response.dump(),  "application/json");
    } else if (action == "compare") {
        // Verify text and hash exists
        if(jsonData.count("text") != 1 || jsonData.count("hash") != 1) {
            return aws_response::failure("{\"Error\": Invalid request}", "application/json");
        }

        std::string rawText = jsonData.at("text");
        std::string cmpHash = jsonData.at("hash");
        bool same = compare(rawText.c_str(), cmpHash.c_str());

        json response = {
                {"match", same}
        };
        return aws_response::success(response.dump(), "application/json");
    }

    return aws_response::failure("{\"Error\": Invalid request}", "application/json");
}

int main() {
    seed_prng();
    run_handler(&aws_handler);

//    char* h = hash("hash me fam", 12);
//    std::cout << "Hash: " << h << std::endl;
//    std::cout << "Are Same: " << compare("hash me fam", h) << std::endl;
    return 0;
}