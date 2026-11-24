// This is copy from nob.h (https://github.com/tsoding/nob.h)
#include "sv.h"
#include <string.h>
#define isspace(x) (x == ' ')

StringView sv_from_cstr(const char *cstr) {
    return sv_from_parts(cstr, strlen(cstr));
}

StringView sv_chop_while(StringView *sv, int (*p)(int)) {
    size_t i = 0;
    while (i < sv->count && p((unsigned char)sv->data[i])) i++;
    StringView result = sv_from_parts(sv->data, i);
    sv->data  += i;
    sv->count -= i;
    return result;
}

StringView sv_chop_by_delim(StringView *sv, char delim) {
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) i++;
    StringView result = sv_from_parts(sv->data, i);
    if (i < sv->count) {
        sv->data  += i + 1;
        sv->count -= i + 1;
    } else {
        sv->data  += i;
        sv->count -= i;
    }
    return result;
}

StringView sv_chop_left(StringView *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    StringView result = sv_from_parts(sv->data, n);
    sv->data  += n;
    sv->count -= n;
    return result;
}

StringView sv_chop_right(StringView *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    StringView result = sv_from_parts(sv->data + sv->count - n, n);
    sv->count -= n;
    return result;
}

bool sv_chop_prefix(StringView *sv, StringView prefix) {
    if (sv_starts_with(*sv, prefix)) {
        sv_chop_left(sv, prefix.count);
        return true;
    }
    return false;
}

bool sv_chop_suffix(StringView *sv, StringView suffix) {
    if (sv_ends_with(*sv, suffix)) {
        sv_chop_right(sv, suffix.count);
        return true;
    }
    return false;
}

StringView sv_trim_left(StringView sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[i])) i++;
    return sv_from_parts(sv.data + i, sv.count - i);
}

StringView sv_trim_right(StringView sv) {
    size_t i = 0;
    while (i < sv.count && isspace((unsigned char)sv.data[sv.count - 1 - i])) i++;
    return sv_from_parts(sv.data, sv.count - i);
}

StringView sv_trim(StringView sv) {
    return sv_trim_right(sv_trim_left(sv));
}

bool sv_eq(StringView a, StringView b) {
    if (a.count != b.count) return false;
    if (a.count == 0) return true;
    return memcmp(a.data, b.data, a.count) == 0;
}

bool sv_eq_cstr(StringView a, const char *cstr) {
    return sv_eq(a, sv_from_cstr(cstr));
}

bool sv_starts_with(StringView sv, StringView prefix) {
    if (prefix.count > sv.count) return false;
    return sv_eq(sv_from_parts(sv.data, prefix.count), prefix);
}

bool sv_ends_with(StringView sv, StringView suffix) {
    if (suffix.count > sv.count) return false;
    return sv_eq(sv_from_parts(sv.data + sv.count - suffix.count, suffix.count), suffix);
}

bool sv_ends_with_cstr(StringView sv, const char *cstr) {
    return sv_ends_with(sv, sv_from_cstr(cstr));
}

bool sv_to_cstr_buf(StringView sv, char *buf, size_t buf_size) {
    if (buf_size < sv.count + 1) return false;
    if (sv.count > 0) memcpy(buf, sv.data, sv.count);
    buf[sv.count] = '\0';
    return true;
}


StringView sv_from_parts(const char *data, size_t count) {
    StringView sv;
    sv.data  = data;
    sv.count = count;
    return sv;
}

