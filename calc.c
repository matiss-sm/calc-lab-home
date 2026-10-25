// Matiss Smits 251RDC067
// https://github.com/matiss-sm/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: OpenAI ChatGPT (GPT-6); assignment specification and sample tests.
// Chat history: https://chatgpt.com/share/6ade4402-2ab4-83eb-8f64-3604c130a3c4
// AI assistance: ChatGPT helped implement and explain the sections marked below.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define STUDENT_NAME "Matiss"
#define STUDENT_LASTNAME "Smits"
#define STUDENT_ID "251RDC067"

typedef struct { double f; long long i; int is_float; size_t start; } Value;
typedef struct { const char *s; size_t pos, end, base, error; } Parser;
typedef struct { char *s; size_t len, cap; } Buffer;

// AI (ChatGPT): prompt "How do I read a whole file into memory in C without knowing its size?"
// The approach uses realloc to double the buffer capacity when needed,
// which avoids resizing for every character on large inputs.
static int reserve(Buffer *b, size_t more) {
    if (more > (size_t)-1 - b->len - 1) return 0;
    size_t needed = b->len + more + 1;
    if (needed <= b->cap) return 1;
    size_t n = b->cap ? b->cap : 256;
    while (n < needed) {
        if (n > (size_t)-1 / 2) { n = needed; break; }
        n *= 2;
    }
    char *p = realloc(b->s, n);
    if (!p) return 0;
    b->s = p; b->cap = n;
    return 1;
}
static int append(Buffer *b, const char *s, size_t len) {
    if (!reserve(b, len)) return 0;
    memcpy(b->s + b->len, s, len);
    b->len += len; b->s[b->len] = 0;
    return 1;
}
// AI (ChatGPT): prompt "How do I track the exact position of an error in a text file?"
// The parser stores the offset of each line in the original file.
// Adding the index inside the line and 1 gives the required 1-based position.
static void whitespace(Parser *p) {
    while (p->pos < p->end && (p->s[p->pos] == ' ' || p->s[p->pos] == '\t')) ++p->pos;
}
static size_t current_position(Parser *p) {
    return p->base + p->pos + 1;
}
static void fail(Parser *p, size_t at) {
    if (!p->error) p->error = at;
}

// AI (ChatGPT): prompt "How do I handle parentheses and powers like 2 ** 3 ** 2?"
// Parentheses call expression() again, allowing nested expressions.
// power() handles exponentiation separately so it can associate to the right.
static Value expression(Parser *p);
static Value primary(Parser *p) {
    Value v = {0};
    whitespace(p);
    v.start = current_position(p);
    if (p->pos == p->end) { fail(p, current_position(p)); return v; }
    if (p->s[p->pos] == '(') {
        ++p->pos;
        v = expression(p);
        if (p->error) return v;
        whitespace(p);
        if (p->pos == p->end || p->s[p->pos] != ')') {
            fail(p, current_position(p)); return v;
        }
        ++p->pos;
        return v;
    }
    if (!isdigit((unsigned char)p->s[p->pos])) {
        fail(p, current_position(p)); return v;
    }
    size_t begin = p->pos;
    long long val = 0;
    while (p->pos < p->end && isdigit((unsigned char)p->s[p->pos])) {
        val = val * 10 + (p->s[p->pos] - '0');
        ++p->pos;
    }
    v.i = val;
    if (p->pos < p->end && p->s[p->pos] == '.' && p->pos + 1 < p->end &&
        isdigit((unsigned char)p->s[p->pos + 1])) {
        ++p->pos;
        while (p->pos < p->end && isdigit((unsigned char)p->s[p->pos])) ++p->pos;
        // Parsing a valid decimal span only; the tokenizer rejects incomplete decimals.
        size_t n = p->pos - begin;
        char small[128];
        char *tmp = n < sizeof small ? small : malloc(n + 1);
        if (!tmp) { fail(p, current_position(p)); return v; }
        memcpy(tmp, p->s + begin, n); tmp[n] = 0;
        v.f = strtod(tmp, NULL);
        if (tmp != small) free(tmp);
        v.is_float = 1;
    }
    return v;
}
// AI (ChatGPT): prompt "How can I use both integers and floats in the same calculator?"
// The Value struct keeps track of whether a number is an integer or a double.
// Arithmetic only switches to double when at least one operand is a float.
static double as_double(Value v) { return v.is_float ? v.f : (double)v.i; }
// The recursive call to power() makes 2 ** 3 ** 2 mean 2 ** (3 ** 2),
// rather than evaluating the powers from left to right.
static Value power(Parser *p) {
    Value a = primary(p);
    if (p->error) return a;
    whitespace(p);
    if (p->pos + 1 < p->end && p->s[p->pos] == '*' && p->s[p->pos + 1] == '*') {
        size_t op = current_position(p);
        p->pos += 2;
        whitespace(p);
        size_t rhs_start = current_position(p);
        Value b = power(p);
        if (p->error) return a;
        if (!b.is_float && b.i < 0 && !a.is_float) { fail(p, rhs_start); return a; }
        if (a.is_float || b.is_float) {
            a.f = pow(as_double(a), as_double(b)); a.is_float = 1;
            if (!isfinite(a.f)) fail(p, op);
        } else {
            long long base = a.i, result = 1;
            unsigned long long exp = (unsigned long long)b.i;
            while (exp) {
                if (exp & 1ULL) result *= base;
                exp >>= 1;
                if (exp) base *= base;
            }
            a.i = result;
        }
    }
    return a;
}
// Division by zero reports the start of the right operand, not the slash.
// Saving its position before parsing keeps the correct error location.
static Value term(Parser *p) {
    Value a = power(p);
    while (!p->error) {
        whitespace(p);
        if (p->pos >= p->end) break;
        char op = p->s[p->pos];
        if (op != '*' && op != '/') break;
        if (op == '*' && p->pos + 1 < p->end && p->s[p->pos+1] == '*') break;
        size_t op_at = current_position(p);
        ++p->pos;
        whitespace(p);
        size_t rhs_at = current_position(p);
        Value b = power(p);
        if (p->error) break;
        if (op == '/' && (b.is_float ? b.f == 0.0 : b.i == 0)) {
            fail(p, rhs_at); break;
        }
        if (a.is_float || b.is_float) {
            a.f = op == '*' ? as_double(a) * as_double(b) : as_double(a) / as_double(b);
            a.is_float = 1;
            if (!isfinite(a.f)) fail(p, op_at);
        } else {
            a.i = op == '*' ? a.i * b.i : a.i / b.i;
        }
    }
    return a;
}
// AI (ChatGPT): prompt "How do I make multiplication and division have higher priority than addition and subtraction in C?"
// expression() calls term() to finish multiplication and division first,
// before it applies addition and subtraction.
static Value expression(Parser *p) {
    Value a = term(p);
    while (!p->error) {
        whitespace(p);
        if (p->pos >= p->end) break;
        char op = p->s[p->pos];
        if (op != '+' && op != '-') break;
        size_t op_at = current_position(p);
        ++p->pos;
        Value b = term(p);
        if (p->error) break;
        if (a.is_float || b.is_float) {
            a.f = op == '+' ? as_double(a) + as_double(b) : as_double(a) - as_double(b);
            a.is_float = 1;
            if (!isfinite(a.f)) fail(p, op_at);
        } else a.i = op == '+' ? a.i + b.i : a.i - b.i;
    }
    return a;
}

// Read the file in blocks and append them into the growing buffer.
// This avoids imposing a fixed maximum input-file size.
static int read_input(const char *path, Buffer *b) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open input %s: %s\n", path, strerror(errno)); return 0; }
    char block[65536]; size_t n;
    while ((n = fread(block, 1, sizeof block, f)) != 0)
        if (!append(b, block, n)) { fprintf(stderr, "Out of memory\n"); fclose(f); return 0; }
    int ok = !ferror(f);
    fclose(f);
    if (!ok) fprintf(stderr, "Cannot read input %s\n", path);
    return ok;
}

// The line offset includes all earlier comments and blank lines, so the
// first error has the correct absolute position. If parsing fails, earlier
// results are cleared because the output must contain only ERROR:<pos>.
// End-of-line errors point to the newline, or EOF+1 if there is no newline.
static int calculate(const Buffer *input, Buffer *out) {
    size_t line = 0;
    while (line < input->len) {
        size_t end = line;
        while (end < input->len && input->s[end] != '\n') ++end;
        Parser p = {input->s + line, 0, end - line, line, 0};
        whitespace(&p);
        if (p.pos < p.end && p.s[p.pos] != '#') {
            Value value = expression(&p);
            if (!p.error) {
                whitespace(&p);
                if (p.pos < p.end) fail(&p, current_position(&p));
            }
            if (p.error) {
                char error[80];
                int n = snprintf(error, sizeof error, "ERROR:%zu\n", p.error);
                out->len = 0;
                if (out->s) out->s[0] = 0;
                return append(out, error, (size_t)n);
            }
            char result[128];
            int n = value.is_float ? snprintf(result, sizeof result, "%.15g\n", value.f)
                                   : snprintf(result, sizeof result, "%lld\n", value.i);
            if (!append(out, result, (size_t)n)) return 0;
        }
        line = end + (end < input->len);
    }
    return 1;
}
static char *combine(const char *a, const char *b, const char *c) {
    size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
    char *s = malloc(n);
    if (s) snprintf(s, n, "%s%s%s", a, b, c);
    return s;
}
static char *stem(const char *path) {
    size_t n = strlen(path);
    while (n && path[n-1] == '/') --n;
    size_t start = n;
    while (start && path[start-1] != '/') --start;
    size_t len = n - start;
    if (len >= 4 && memcmp(path + n - 4, ".txt", 4) == 0) len -= 4;
    char *s = malloc(len + 1);
    if (s) { memcpy(s, path + start, len); s[len] = 0; }
    return s;
}
// mkdir() can fail because the directory already exists; stat() confirms
// that the existing path is actually a directory.
static int ensure_dir(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    }
    fprintf(stderr, "Cannot create output directory %s: %s\n", path, strerror(errno));
    return 0;
}
static int process_file(const char *path, const char *outdir) {
    Buffer in = {0}, out = {0};
    int ok = read_input(path, &in);
    if (ok) ok = calculate(&in, &out);
    char *base = stem(path);
    char *suffix = combine("_" STUDENT_NAME "_" STUDENT_LASTNAME "_" STUDENT_ID, ".txt", "");
    char *name = base && suffix ? combine(base, suffix, "") : NULL;
    char *prefix = name ? combine(outdir, "/", name) : NULL;
    if (ok && prefix) {
        FILE *f = fopen(prefix, "wb");
        if (!f) { fprintf(stderr, "Cannot write %s: %s\n", prefix, strerror(errno)); ok = 0; }
        else {
            if (out.len && fwrite(out.s, 1, out.len, f) != out.len) ok = 0;
            if (fclose(f) != 0) ok = 0;
            if (!ok) fprintf(stderr, "Failed writing %s\n", prefix);
        }
    } else if (ok) { fprintf(stderr, "Out of memory\n"); ok = 0; }
    free(in.s); free(out.s); free(base); free(suffix); free(name); free(prefix);
    return ok;
}
// AI (ChatGPT): prompt "How do I read multiple .txt files from a folder and save the results?"
// opendir() and readdir() visit the directory entries, while stat() ensures
// only regular .txt files are processed and subdirectories are skipped.
static int process_directory(const char *dir, const char *outdir) {
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "Cannot open directory %s: %s\n", dir, strerror(errno)); return 0; }
    int ok = 1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".txt") != 0) continue;
        char *full = combine(dir, "/", e->d_name);
        if (!full) { ok = 0; break; }
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) { free(full); continue; }
        if (!process_file(full, outdir)) ok = 0;
        free(full);
    }
    closedir(d);
    return ok;
}
// AI (ChatGPT): prompt "How do I check if my C code has memory leaks or other errors?"
// Compile with -Wall -Wextra to catch warnings, and use
// -fsanitize=address,undefined to help detect runtime memory issues.
int main(int argc, char **argv) {
    const char *dir = NULL, *file = NULL, *output = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--dir")) {
            if (dir || ++i == argc) goto usage;
            dir = argv[i];
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output-dir")) {
            if (output || ++i == argc) goto usage;
            output = argv[i];
        } else if (argv[i][0] == '-' || file) goto usage;
        else file = argv[i];
    }
    if ((dir && file) || (!dir && !file)) goto usage;
    char *base = NULL, *defaultdir = NULL;
    if (!output) {
        base = stem(dir ? dir : file);
        if (base) defaultdir = combine(base, "_" STUDENT_NAME "_" STUDENT_ID, "");
        if (!defaultdir) { fprintf(stderr, "Out of memory\n"); free(base); return 1; }
        output = defaultdir;
    }
    // Check the input before creating the output directory.
    int valid = 0;
    if (dir) { DIR *d = opendir(dir); if (d) { valid = 1; closedir(d); } }
    else { FILE *f = fopen(file, "rb"); if (f) { valid = 1; fclose(f); } }
    if (!valid) {
        fprintf(stderr, "Input not found or unreadable: %s\n", dir ? dir : file);
        free(base); free(defaultdir); return 1;
    }
    int ok = ensure_dir(output);
    if (ok) ok = dir ? process_directory(dir, output) : process_file(file, output);
    free(base); free(defaultdir);
    return ok ? 0 : 1;
usage:
    fprintf(stderr, "Usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 1;
}
