// Nashir Nizamudeen 251ADB122
// https://github.com/nashirnizam59-ops/calc-lab-home
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: supplied arithmetic-parser lab instructions; OpenAI ChatGPT (this conversation).
// AI assistance: ChatGPT drafted the initial implementation and provided explanations.
// Prompt (paraphrased): Implement the arithmetic parser described in the supplied lab specification in a single C source file.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define make_directory(path) _mkdir(path)
#else
#define make_directory(path) mkdir(path, 0777)
#endif

#define FIRST_NAME "nashir"
#define LAST_NAME "nizamudeen"
#define STUDENT_ID "251ADB122"

typedef struct {
    double real;
    long long integer;
    int is_float;
} Value;

typedef struct {
    const char *text;
    size_t offset;
    size_t line_end;
    size_t error_pos;
    int failed;
} Parser;

static Value parse_expression(Parser *p);

static Value make_int(long long n) {
    Value v;
    v.integer = n;
    v.real = (double)n;
    v.is_float = 0;
    return v;
}

static Value make_float(double n) {
    Value v;
    v.real = n;
    v.integer = 0;
    v.is_float = 1;
    return v;
}

static void fail_at(Parser *p, size_t pos) {
    if (!p->failed) {
        p->failed = 1;
        p->error_pos = pos;
    }
}

/* The parser keeps an offset into the original file, so positions include
   spaces, comments, and newlines without copying or renumbering characters. */
static void skip_space(Parser *p) {
    while (p->offset < p->line_end &&
           (p->text[p->offset] == ' ' || p->text[p->offset] == '\t' ||
            p->text[p->offset] == '\r' || p->text[p->offset] == '\v' ||
            p->text[p->offset] == '\f')) {
        p->offset++;
    }
}

static double as_double(Value v) {
    return v.is_float ? v.real : (double)v.integer;
}

static Value parse_primary(Parser *p) {
    Value bad = make_int(0);
    skip_space(p);

    if (p->offset >= p->line_end) {
        fail_at(p, p->line_end + 1);
        return bad;
    }

    size_t start = p->offset;
    char c = p->text[p->offset];

    if (c == '(') {
        p->offset++;
        Value inside = parse_expression(p);
        if (p->failed) return bad;

        skip_space(p);
        if (p->offset >= p->line_end) {
            fail_at(p, p->line_end + 1);
            return bad;
        }

        if (p->text[p->offset] != ')') {
            fail_at(p, p->offset + 1);
            return bad;
        }

        p->offset++;
        return inside;
    }

    if (isdigit((unsigned char)c)) {
        size_t digits_start = p->offset;

        while (p->offset < p->line_end &&
               isdigit((unsigned char)p->text[p->offset])) {
            p->offset++;
        }

        int floating = 0;

        if (p->offset < p->line_end && p->text[p->offset] == '.') {
            size_t dot_pos = p->offset;
            floating = 1;
            p->offset++;

            if (p->offset >= p->line_end ||
                !isdigit((unsigned char)p->text[p->offset])) {
                fail_at(p, dot_pos + 1);
                return bad;
            }

            while (p->offset < p->line_end &&
                   isdigit((unsigned char)p->text[p->offset])) {
                p->offset++;
            }
        }

        if (p->offset < p->line_end &&
            (p->text[p->offset] == 'e' || p->text[p->offset] == 'E')) {
            fail_at(p, p->offset + 1);
            return bad;
        }

        size_t count = p->offset - digits_start;
        char *token = malloc(count + 1);

        if (token == NULL) {
            fail_at(p, start + 1);
            return bad;
        }

        memcpy(token, p->text + digits_start, count);
        token[count] = '\0';
        errno = 0;

        if (floating) {
            double d = strtod(token, NULL);
            free(token);

            if (errno == ERANGE || !isfinite(d)) {
                fail_at(p, start + 1);
                return bad;
            }

            return make_float(d);
        }

        long long n = strtoll(token, NULL, 10);
        free(token);
        return make_int(n);
    }

    fail_at(p, start + 1);
    return bad;
}

static long long integer_power(long long base, long long exponent) {
    long long result = 1;

    while (exponent > 0) {
        if (exponent & 1LL) {
            result *= base;
        }

        exponent >>= 1;

        if (exponent > 0) {
            base *= base;
        }
    }

    return result;
}

/* Power parses its right side recursively, so 2**3**2 is 2**(3**2).
   The term and expression loops below make * / + - associate left. */
static Value parse_power(Parser *p) {
    Value left = parse_primary(p);
    if (p->failed) return make_int(0);

    skip_space(p);

    if (p->offset + 1 >= p->line_end ||
        p->text[p->offset] != '*' ||
        p->text[p->offset + 1] != '*') {
        return left;
    }

    size_t operator_pos = p->offset + 1;
    p->offset += 2;
    skip_space(p);
    size_t exponent_pos = p->offset + 1;

    Value right = parse_power(p);
    if (p->failed) return make_int(0);

    if (!left.is_float && !right.is_float) {
        if (right.integer < 0) {
            fail_at(p, exponent_pos);
            return make_int(0);
        }

        return make_int(integer_power(left.integer, right.integer));
    }

    double result = pow(as_double(left), as_double(right));

    if (!isfinite(result)) {
        fail_at(p, operator_pos);
    }

    return make_float(result);
}

static Value apply_binary(Parser *p, Value left, Value right, char op,
                          size_t op_pos, size_t rhs_pos) {
    if (op == '/' &&
        ((right.is_float && right.real == 0.0) ||
         (!right.is_float && right.integer == 0))) {
        fail_at(p, rhs_pos);
        return make_int(0);
    }

    if (!left.is_float && !right.is_float) {
        switch (op) {
            case '+': return make_int(left.integer + right.integer);
            case '-': return make_int(left.integer - right.integer);
            case '*': return make_int(left.integer * right.integer);
            case '/': return make_int(left.integer / right.integer);
        }
    }

    double a = as_double(left);
    double b = as_double(right);
    double result = 0.0;

    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': result = a / b; break;
    }

    if (!isfinite(result)) {
        fail_at(p, op_pos);
    }

    return make_float(result);
}

static Value parse_term(Parser *p) {
    Value value = parse_power(p);

    while (!p->failed) {
        skip_space(p);

        if (p->offset >= p->line_end ||
            (p->text[p->offset] != '*' && p->text[p->offset] != '/')) {
            break;
        }

        if (p->text[p->offset] == '*' &&
            p->offset + 1 < p->line_end &&
            p->text[p->offset + 1] == '*') {
            break;
        }

        size_t op_pos = p->offset + 1;
        char op = p->text[p->offset++];
        skip_space(p);
        size_t rhs_pos = p->offset + 1;

        Value right = parse_power(p);
        if (p->failed) break;

        value = apply_binary(p, value, right, op, op_pos, rhs_pos);
    }

    return value;
}

static Value parse_expression(Parser *p) {
    Value value = parse_term(p);

    while (!p->failed) {
        skip_space(p);

        if (p->offset >= p->line_end ||
            (p->text[p->offset] != '+' && p->text[p->offset] != '-')) {
            break;
        }

        size_t op_pos = p->offset + 1;
        char op = p->text[p->offset++];
        skip_space(p);
        size_t rhs_pos = p->offset + 1;

        Value right = parse_term(p);
        if (p->failed) break;

        value = apply_binary(p, value, right, op, op_pos, rhs_pos);
    }

    return value;
}

/* The buffer grows geometrically. This avoids repeatedly copying the whole
   file while reading large inputs. */
static char *read_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    size_t capacity = 8192;
    size_t used = 0;
    char *buffer = malloc(capacity + 1);

    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }

    for (;;) {
        if (used == capacity) {
            if (capacity > (size_t)-1 / 2) {
                free(buffer);
                fclose(f);
                errno = ENOMEM;
                return NULL;
            }

            size_t next = capacity * 2;
            char *grown = realloc(buffer, next + 1);

            if (grown == NULL) {
                free(buffer);
                fclose(f);
                return NULL;
            }

            buffer = grown;
            capacity = next;
        }

        size_t got = fread(buffer + used, 1, capacity - used, f);
        used += got;

        if (got == 0) {
            if (ferror(f)) {
                free(buffer);
                fclose(f);
                return NULL;
            }
            break;
        }
    }

    fclose(f);
    buffer[used] = '\0';
    *length = used;
    return buffer;
}

static int make_output_path(char *dst, size_t size, const char *outdir,
                            const char *base) {
    int n = snprintf(dst, size, "%s/%s_%s_%s_%s.txt",
                     outdir, base, FIRST_NAME, LAST_NAME, STUDENT_ID);

    return n >= 0 && (size_t)n < size;
}

static int evaluate_file(const char *input, const char *outdir,
                         const char *base) {
    size_t length = 0;
    char *text = read_file(input, &length);

    if (text == NULL) {
        fprintf(stderr, "calc: cannot read input '%s': %s\n",
                input, strerror(errno));
        return 0;
    }

    char path[4096];

    if (!make_output_path(path, sizeof(path), outdir, base)) {
        fprintf(stderr, "calc: output path is too long\n");
        free(text);
        return 0;
    }

    FILE *out = fopen(path, "w");

    if (out == NULL) {
        fprintf(stderr, "calc: cannot write '%s': %s\n",
                path, strerror(errno));
        free(text);
        return 0;
    }

    int output_ok = 1;
    size_t begin = 0;

    while (begin < length || begin == 0) {
        size_t end = begin;

        while (end < length && text[end] != '\n') {
            end++;
        }

        Parser p = {text, begin, end, 0, 0};
        skip_space(&p);

        if (p.offset == p.line_end || text[p.offset] == '#') {
            if (end == length) break;
            begin = end + 1;
            continue;
        }

        Value result = parse_expression(&p);

        if (!p.failed) {
            skip_space(&p);

            if (p.offset < p.line_end) {
                fail_at(&p, p.offset + 1);
            }
        }

        if (p.failed) {
            /* A file with an error must contain only that error, so restart
               its output file instead of keeping earlier results. */
            if (fclose(out) != 0) {
                output_ok = 0;
            }

            out = fopen(path, "w");

            if (out == NULL ||
                fprintf(out, "ERROR:%zu\n", p.error_pos) < 0) {
                output_ok = 0;
            }

            break;
        }

        if (result.is_float) {
            if (fprintf(out, "%.15g\n", result.real) < 0) {
                output_ok = 0;
            }
        } else if (fprintf(out, "%lld\n", result.integer) < 0) {
            output_ok = 0;
        }

        if (!output_ok || end == length) break;
        begin = end + 1;
    }

    free(text);

    if (out != NULL && fclose(out) != 0) {
        output_ok = 0;
    }

    if (!output_ok) {
        fprintf(stderr, "calc: failed writing output '%s'\n", path);
    }

    return output_ok;
}

static int is_txt_file(const char *name) {
    size_t n = strlen(name);
    return n >= 4 && strcmp(name + n - 4, ".txt") == 0;
}

static const char *base_name(const char *path) {
    const char *last = path;

    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }

    return last;
}

static void strip_txt(char *name) {
    size_t n = strlen(name);

    if (n >= 4 && strcmp(name + n - 4, ".txt") == 0) {
        name[n - 4] = '\0';
    }
}

static int ensure_directory(const char *path) {
    struct stat st;

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    if (make_directory(path) == 0) {
        return 1;
    }

    return errno == EEXIST &&
           stat(path, &st) == 0 &&
           S_ISDIR(st.st_mode);
}

static void usage(void) {
    fprintf(stderr,
            "Usage: calc [-d DIR | --dir DIR] "
            "[-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *dir = NULL;
    const char *outdir_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 ||
            strcmp(argv[i], "--dir") == 0) {
            if (++i >= argc || dir != NULL) {
                usage();
                return 2;
            }
            dir = argv[i];
        } else if (strcmp(argv[i], "-o") == 0 ||
                   strcmp(argv[i], "--output-dir") == 0) {
            if (++i >= argc || outdir_arg != NULL) {
                usage();
                return 2;
            }
            outdir_arg = argv[i];
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            usage();
            return 2;
        }
    }

    if ((dir == NULL) == (input == NULL)) {
        usage();
        return 2;
    }

    char default_out[4096];
    const char *outdir = outdir_arg;
    const char *default_base;

    if (dir == NULL) {
        const char *file = base_name(input);
        size_t n = strlen(file);

        if (n < 4 || strcmp(file + n - 4, ".txt") != 0) {
            fprintf(stderr, "calc: input file must end in .txt\n");
            return 2;
        }

        char temp[2048];

        if (strlen(file) >= sizeof(temp)) {
            fprintf(stderr, "calc: input name is too long\n");
            return 2;
        }

        strcpy(temp, file);
        strip_txt(temp);
        default_base = temp;

        if (outdir == NULL) {
            int nout = snprintf(default_out, sizeof(default_out),
                                "%s_%s_%s", temp, FIRST_NAME, STUDENT_ID);

            if (nout < 0 || (size_t)nout >= sizeof(default_out)) {
                fprintf(stderr, "calc: output directory path is too long\n");
                return 2;
            }

            outdir = default_out;
        }

        if (!ensure_directory(outdir)) {
            fprintf(stderr,
                    "calc: cannot create or access output directory '%s'\n",
                    outdir);
            return 2;
        }

        return evaluate_file(input, outdir, default_base) ? 0 : 1;
    }

    default_base = base_name(dir);

    if (outdir == NULL) {
        int nout = snprintf(default_out, sizeof(default_out),
                            "%s_%s_%s", default_base, FIRST_NAME, STUDENT_ID);

        if (nout < 0 || (size_t)nout >= sizeof(default_out)) {
            fprintf(stderr, "calc: output directory path is too long\n");
            return 2;
        }

        outdir = default_out;
    }

    if (!ensure_directory(outdir)) {
        fprintf(stderr,
                "calc: cannot create or access output directory '%s'\n",
                outdir);
        return 2;
    }

    DIR *d = opendir(dir);

    if (d == NULL) {
        fprintf(stderr, "calc: cannot open directory '%s': %s\n",
                dir, strerror(errno));
        return 2;
    }

    struct dirent *entry;
    int ok = 1;

    while ((entry = readdir(d)) != NULL) {
        if (!is_txt_file(entry->d_name)) {
            continue;
        }

        char input_path[4096];
        int n = snprintf(input_path, sizeof(input_path),
                         "%s/%s", dir, entry->d_name);

        if (n < 0 || (size_t)n >= sizeof(input_path)) {
            fprintf(stderr, "calc: input path is too long\n");
            ok = 0;
            break;
        }

        struct stat st;

        if (stat(input_path, &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }

        char filebase[2048];

        if (strlen(entry->d_name) >= sizeof(filebase)) {
            fprintf(stderr, "calc: input name is too long\n");
            ok = 0;
            break;
        }

        strcpy(filebase, entry->d_name);
        strip_txt(filebase);

        if (!evaluate_file(input_path, outdir, filebase)) {
            ok = 0;
        }
    }

    closedir(d);
    return ok ? 0 : 1;
}