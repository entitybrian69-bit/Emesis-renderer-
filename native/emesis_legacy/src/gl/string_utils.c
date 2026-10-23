#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "string_utils.h"

const char* AllSeparators = " \t\n\r.,;()[]{}-<>+*/%&\\\"'^$=!:?";

char* gl4es_resize_if_needed(char* pBuffer, int *size, int addsize);

void AppendToEnd(char **str, const char *suffix) {
    size_t len = strlen(*str);
    size_t suffixLen = strlen(suffix);
    char *newStr = (char *)malloc(len + suffixLen + 1);
    if (newStr == NULL) {
        return;
    }
    strcpy(newStr, *str);
    strcpy(newStr + len, suffix);
    free(*str);
    *str = newStr;
}
void InsertAtBeginning(char **str, const char *prefix) {
    size_t len = strlen(*str);
    size_t prefixLen = strlen(prefix);
    char *newStr = (char *)malloc(len + prefixLen + 1);
    strcpy(newStr, prefix);
    strcpy(newStr + prefixLen, *str);
    free(*str);
    *str = newStr;
}


char * InplaceReplaceByIndex(char* pBuffer, int* size, const int startIndex, const int endIndex, const char* replacement)
{
    //printf("BY INDEX: %s", replacement);
    //printf("BY INDEX: %i", strlen(replacement));
    int length_difference;
    if(endIndex < startIndex)
        length_difference = strlen(replacement) + (endIndex - startIndex);
    else if(endIndex == startIndex)
        length_difference = strlen(replacement) - 1; // The initial char gets replaced
    else
        length_difference = strlen(replacement) - (endIndex - startIndex); // Can be negative if repl is smaller
    pBuffer = gl4es_resize_if_needed(pBuffer, size, length_difference);
    //printf("BEFORE MOVING: \n%s", pBuffer);
    // Move the end of the string
    memmove(pBuffer + startIndex + strlen(replacement) , pBuffer + endIndex + 1, strlen(pBuffer) - endIndex + 1);
    //printf("AFTER MOVING 1: \n%s", pBuffer);
    // Insert the replacement
    memcpy(pBuffer + startIndex, replacement, strlen(replacement));
    //strncpy(pBuffer + startIndex, replacement, strlen(replacement));
    //printf("AFTER MOVING 2: \n%s", pBuffer);
    return pBuffer;
}
/**
 * Insert the string at the index, pushing "every chars to the right"
 * @param source The shader as a string
 * @param sourceLength The ALLOCATED length of the shader
 * @param insertPoint The index at which the string is inserted.
 * @param insertedString The string to insert
 * @return The shader as a string, maybe in a different memory location
 */
char * InplaceInsertByIndex(char * source, int *sourceLength, const int insertPoint, const char *insertedString){
    int insertLength = strlen(insertedString);
    source = gl4es_resize_if_needed(source, sourceLength, insertLength);
    memmove(source + insertPoint + insertLength,  source + insertPoint, strlen(source) - insertPoint + 1);
    memcpy(source + insertPoint, insertedString, insertLength);
    return source;
}

static inline int is_all_separator_char(unsigned char c) {
    switch (c) {
        case '\0': case ' ': case '\t': case '\n': case '\r':
        case '.': case ',': case ';': case '(': case ')':
        case '[': case ']': case '{': case '}': case '-':
        case '<': case '>': case '+': case '*': case '/':
        case '%': case '&': case '\\': case '"': case '\'':
        case '^': case '$': case '=': case '!': case ':':
        case '?':
            return 1;
        default:
            return 0;
    }
}

char* gl4es_inplace_replace(char* pBuffer, int* size, const char* S, const char* D)
{
    if (!pBuffer || !S || !S[0] || !D) return pBuffer;
    char* first = strstr(pBuffer, S);
    if (!first) return pBuffer;

    size_t lS = strlen(S), lD = strlen(D);
    int matches = 0;
    const char* scan = first;
    while (scan) {
        if (is_all_separator_char((unsigned char)scan[lS]) &&
            (scan == pBuffer || is_all_separator_char((unsigned char)scan[-1]))) {
            matches++;
        }
        scan = strstr(scan + lS, S);
    }
    if (matches == 0) return pBuffer;

    size_t srcLen = strlen(pBuffer);
    long diff = ((long)lD - (long)lS) * (long)matches;
    size_t newLen = (size_t)((long)srcLen + diff);

    if (lD == lS) {
        char* p = first;
        while (p) {
            if (is_all_separator_char((unsigned char)p[lS]) &&
                (p == pBuffer || is_all_separator_char((unsigned char)p[-1]))) {
                memcpy(p, D, lD);
                p = strstr(p + lD, S);
            } else {
                p = strstr(p + lS, S);
            }
        }
        return pBuffer;
    }

    int allocCap = *size;
    if ((int)(newLen + 1) > allocCap) {
        allocCap = (int)(newLen + 1) * 2;
    }
    char* out = (char*)malloc((size_t)allocCap);
    if (!out) return pBuffer;

    const char* cur = pBuffer;
    const char* hit = first;
    char* dst = out;
    while (hit) {
        if (is_all_separator_char((unsigned char)hit[lS]) &&
            (hit == pBuffer || is_all_separator_char((unsigned char)hit[-1]))) {
            size_t chunk = (size_t)(hit - cur);
            if (chunk) {
                memcpy(dst, cur, chunk);
                dst += chunk;
            }
            if (lD) {
                memcpy(dst, D, lD);
                dst += lD;
            }
            cur = hit + lS;
            hit = strstr(cur, S);
        } else {
            hit = strstr(hit + lS, S);
        }
    }
    size_t tail = (pBuffer + srcLen) - cur;
    if (tail) {
        memcpy(dst, cur, tail);
        dst += tail;
    }
    *dst = '\0';
    free(pBuffer);
    *size = allocCap;
    return out;
}

char* gl4es_inplace_insert(char* pBuffer, const char* S, char* master, int* size)
{
    char* m = gl4es_resize_if_needed(master, size, strlen(S));
    if(m!=master) {
        pBuffer += (m-master);
        master = m;
    }
    char* p = pBuffer;
    int lS = strlen(S), ll = strlen(pBuffer);
    memmove(p+lS, p, ll+1);
    memcpy(p, S, lS);

    return master;
}

char* gl4es_getline(char* pBuffer, int num)
{
    char *p = pBuffer;
    while(num-- && (p=strstr(p, "\n"))) p+=strlen("\n");
    return (p)?p:pBuffer;
}

int gl4es_countline(const char* pBuffer)
{
    int n=0;
    const char* p = pBuffer;
    while((p=strstr(p, "\n"))) {
        p+=strlen("\n");
        n++;
    }
    return n;
}

int gl4es_getline_for(const char* pBuffer, const char* S)
{
    int n=0;
    const char* p = pBuffer;
    const char* end = gl4es_find_string(pBuffer, S);
    if(!end)
        return 0;
    while((p=strstr(p, "\n"))) {
        p+=strlen("\n");
        n++;
        if(p>=end)
            return n;
    }
    return n;
}

int gl4es_count_string(const char* pBuffer, const char* S)
{
    const char* p = pBuffer;
    int lS = strlen(S);
    int n = 0;
    while((p = strstr(p, S)))
    {
        // found an occurrence of S
        // check if good to count, strchr also found '\0' :)
        if(strchr(AllSeparators, p[lS])!=NULL && (p==pBuffer || strchr(AllSeparators, p[-1])!=NULL))
            n++;
        p+=lS;
    }
    return n;
}

const char* gl4es_find_string(const char* pBuffer, const char* S)
{
    const char* p = pBuffer;
    int lS = strlen(S);
    while((p = strstr(p, S)))
    {
        // found an occurrence of S
        // check if good to count, strchr also found '\0' :)
        if(strchr(AllSeparators, p[lS])!=NULL && (p==pBuffer || strchr(AllSeparators, p[-1])!=NULL))
            return p;
        p+=lS;
    }
    return NULL;
}

char* gl4es_find_string_nc(char* pBuffer, const char* S)
{
    char* p = pBuffer;
    int lS = strlen(S);
    while((p = strstr(p, S)))
    {
        // found an occurrence of S
        // check if good to count, strchr also found '\0' :)
        if(strchr(AllSeparators, p[lS])!=NULL && (p==pBuffer || strchr(AllSeparators, p[-1])!=NULL))
            return p;
        p+=lS;
    }
    return NULL;
}

char* gl4es_resize_if_needed(char* pBuffer, int *size, int addsize) {
    char* p = pBuffer;
    int newsize = (int)strlen(pBuffer) + addsize + 1;
    if (newsize > *size) {
        int grown = (*size > 0) ? (*size * 2) : 256;
        if (grown < newsize) grown = newsize + (newsize >> 1) + 64;
        p = (char*)realloc(pBuffer, (size_t)grown);
        *size = grown;
    }
    return p;
}

int isDigit(char value){
    return (value >= '0' && value <= '9');
}

int isValidFunctionName(char value){
    return ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value == '_'));
}

char* gl4es_append(char* pBuffer, int* size, const char* S) {
    char* p =pBuffer;
    p = gl4es_resize_if_needed(pBuffer, size, strlen(S));
    strcat(p, S);
    return p;
}

static int gl4es_is_blank(char c)  {
    switch(c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case ':':
        case ',':
        case ';':
        case '/':
            return 1;
        default:
            return 0;
    }
}
char* gl4es_str_next(char *pBuffer, const char* S) {
    if(!pBuffer) return NULL;
    char *p = strstr(pBuffer, S);
    return (p)?p:(p+strlen(S));
}

char* gl4es_next_str(char* pBuffer) {
    if(!pBuffer) return NULL;
    while(gl4es_is_blank(*pBuffer))
        ++pBuffer;
    return pBuffer;
}

char* gl4es_prev_str(char* Str, char* pBuffer) {
    if(!pBuffer) return NULL;
    if(pBuffer == Str)
        return Str;
    // go to previous non blank
    do {
        --pBuffer;
    } while(gl4es_is_blank(*pBuffer) && (pBuffer!=Str));
    // go to blank
    while((pBuffer!=Str) && !gl4es_is_blank(*(pBuffer-1)))
        --pBuffer;
    return pBuffer;
}

char* gl4es_next_blank(char* pBuffer) {
    if(!pBuffer) return NULL;
    while(!gl4es_is_blank(*pBuffer))
        ++pBuffer;
    return pBuffer;
}

char* gl4es_next_line(char* pBuffer) {
    if(!pBuffer) return NULL;
    while(*pBuffer && *pBuffer!='\n')
        ++pBuffer;
    return pBuffer;
}

const char* gl4es_get_next_str(char* pBuffer) {
    static char buff[100] = {0};
    buff[0] = '\0';
    if(!pBuffer) return NULL;
    char* p1 = gl4es_next_str(pBuffer);
    if(!p1) return buff;
    char* p2 = gl4es_next_blank(p1);
    if(!p2) return buff;
    int i=0;
    while(p1!=p2 && i<99)
        buff[i++] = *(p1++);
    buff[i] = '\0';
    return buff;
}

int gl4es_countstring_simple(char* pBuffer, const char* S)
{
    char* p = pBuffer;
    int lS = strlen(S);
    int n = 0;
    while((p = strstr(p, S)))
    {
        // found an occurrence of S
        n++;
        p+=lS;
    }
    return n;
}

char* gl4es_inplace_replace_simple(char* pBuffer, int* size, const char* S, const char* D)
{
    if (!pBuffer || !S || !S[0] || !D) return pBuffer;
    char* first = strstr(pBuffer, S);
    if (!first) return pBuffer;

    size_t lS = strlen(S), lD = strlen(D);
    if (lD == lS) {
        char* p = first;
        while (p) {
            memcpy(p, D, lD);
            p = strstr(p + lD, S);
        }
        return pBuffer;
    }

    int matches = 0;
    const char* scan = first;
    while (scan) {
        matches++;
        scan = strstr(scan + lS, S);
    }
    size_t srcLen = strlen(pBuffer);
    size_t newLen = (size_t)((long)srcLen + ((long)lD - (long)lS) * (long)matches);
    int allocCap = *size;
    if ((int)(newLen + 1) > allocCap) {
        allocCap = (int)(newLen + 1) * 2;
    }
    char* out = (char*)malloc((size_t)allocCap);
    if (!out) return pBuffer;

    const char* cur = pBuffer;
    const char* hit = first;
    char* dst = out;
    while (hit) {
        size_t chunk = (size_t)(hit - cur);
        if (chunk) {
            memcpy(dst, cur, chunk);
            dst += chunk;
        }
        if (lD) {
            memcpy(dst, D, lD);
            dst += lD;
        }
        cur = hit + lS;
        hit = strstr(cur, S);
    }
    size_t tail = (pBuffer + srcLen) - cur;
    if (tail) {
        memcpy(dst, cur, tail);
        dst += tail;
    }
    *dst = '\0';
    free(pBuffer);
    *size = allocCap;
    return out;
}


// for vgpu/shaderconv.c
const char* FindString(const char* pBuffer, const char* S) __attribute__((alias("gl4es_find_string")));
char* FindStringNC(char* pBuffer, const char* S) __attribute__((alias("gl4es_find_string_nc")));
int CountString(const char* pBuffer, const char* S) __attribute__((alias("gl4es_count_string")));
char* ResizeIfNeeded(char* pBuffer, int *size, int addsize) __attribute__((alias("gl4es_resize_if_needed")));
char* InplaceReplace(char* pBuffer, int* size, const char* S, const char* D) __attribute__((alias("gl4es_inplace_replace")));
char* Append(char* pBuffer, int* size, const char* S) __attribute__((alias("gl4es_append")));
char* InplaceInsert(char* pBuffer, const char* S, char* master, int* size) __attribute__((alias("gl4es_inplace_insert")));
char* GetLine(char* pBuffer, int num) __attribute__((alias("gl4es_getline")));
int CountLine(const char* pBuffer) __attribute__((alias("gl4es_countline")));
int GetLineFor(const char* pBuffer, const char* S) __attribute__((alias("gl4es_getline_for")));
char* StrNext(char *pBuffer, const char* S) __attribute__((alias("gl4es_str_next")));
char* NextStr(char* pBuffer) __attribute__((alias("gl4es_next_str")));
char* PrevStr(char* Str, char* pBuffer) __attribute__((alias("gl4es_prev_str")));
char* NextBlank(char* pBuffer) __attribute__((alias("gl4es_next_blank")));
char* NextLine(char* pBuffer) __attribute__((alias("gl4es_next_line")));
const char* GetNextStr(char* pBuffer) __attribute__((alias("gl4es_get_next_str")));
int CountStringSimple(char* pBuffer, const char* S) __attribute__((alias("gl4es_countstring_simple")));
char* InplaceReplaceSimple(char* pBuffer, int* size, const char* S, const char* D) __attribute__((alias("gl4es_inplace_replace_simple")));
