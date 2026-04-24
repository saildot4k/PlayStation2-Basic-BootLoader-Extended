// Generic string/path helpers and LOGO_DISPLAY mapping utilities.
#include "main.h"

char *trim_ws_inplace(char *s)
{
    char *end;
    if (!s)
        return s;

    // Left trim.
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;

    if (*s == '\0')
        return s;

    // Right trim.
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = '\0';

    return s;
}

const char *strip_crlf_copy(const char *s, char *buf, size_t buf_len)
{
    size_t i = 0;
    if (buf_len == 0)
        return "";
    if (s == NULL) {
        buf[0] = '\0';
        return buf;
    }
    while (s[i] != '\0' && s[i] != '\r' && s[i] != '\n' && i + 1 < buf_len) {
        buf[i] = s[i];
        i++;
    }
    buf[i] = '\0';
    return buf;
}

int ci_eq(const char *a, const char *b)
{
    unsigned char ca, cb;
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'a' && ca <= 'z')
            ca -= ('a' - 'A');
        if (cb >= 'a' && cb <= 'z')
            cb -= ('a' - 'A');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

int ci_starts_with(const char *s, const char *prefix)
{
    unsigned char cs, cp;
    if (s == NULL || prefix == NULL)
        return 0;
    while (*prefix != '\0') {
        cs = (unsigned char)*s;
        cp = (unsigned char)*prefix;
        if (cs == '\0')
            return 0;
        if (cs >= 'a' && cs <= 'z')
            cs -= ('a' - 'A');
        if (cp >= 'a' && cp <= 'z')
            cp -= ('a' - 'A');
        if (cs != cp)
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

int ci_starts_with_n(const char *s, size_t s_len, const char *prefix)
{
    size_t i;

    if (s == NULL || prefix == NULL)
        return 0;

    for (i = 0; prefix[i] != '\0'; i++) {
        unsigned char cs;
        unsigned char cp;

        if (i >= s_len)
            return 0;

        cs = (unsigned char)s[i];
        cp = (unsigned char)prefix[i];
        if (cs >= 'a' && cs <= 'z')
            cs -= ('a' - 'A');
        if (cp >= 'a' && cp <= 'z')
            cp -= ('a' - 'A');
        if (cs != cp)
            return 0;
    }

    return 1;
}

int normalize_logo_display(int value)
{
    if (value < 0)
        return 2;
    if (value > 3)
        return 3;
    return value;
}

int logo_to_hotkey_display(int logo_disp)
{
    switch (logo_disp) {
        case 3:
            return 1;
        case 4:
            return 2;
        case 5:
            return 3;
        default:
            return 0;
    }
}
