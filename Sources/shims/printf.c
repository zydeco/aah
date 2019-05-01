#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int EncodeStringFormatArgs(char *fmt, char *type_encoding, int is_nsstring, int replace_long_double) {
    int nargs = 0;
    for(const char *arg = strchr(fmt, '%'); arg; arg = arg[1] ? strchr(arg+2, '%') : 0) {
        int pos = 0;
        if (arg[1] == 0) {
            break;
        } else if (arg[1] == '%') {
            continue;
        } else if (isdigit(arg[1]) && arg[2] == '$') {
            // positional argument
            pos = arg[1] - '1';
            if (nargs < pos) nargs = pos;
            arg += 2;
        } else if (arg[1] != '%') {
            // normal argument
            pos = nargs++;
        }
        
        // flags
        arg++;
        while (*arg && (*arg == 0x27 || *arg == '-' || *arg == '+' || *arg == ' ' || *arg == '#' || *arg == '0')) {
            arg++;
        }
        if (*arg == 0) return -1;
        
        // field width
        if (*arg == '*') {
            // int argument
            if (type_encoding) type_encoding[pos++] = 'i';
            nargs++;
            arg++;
        } else {
            // skip digits
            while (isdigit(*arg)) arg++;
        }
        if (*arg == 0) return -1;
        
        // precision
        if (*arg == '.') {
            arg++;
            if (*arg == '*') {
                // int argument
                if (type_encoding) type_encoding[pos++] = 'i';
                nargs++;
                arg++;
            } else {
                // skip digits
                while (isdigit(*arg)) arg++;
            }
        }
        
        // length modifier
        // anything smaller than int gets promoted to int
        enum {
            hh = -2,
            h = -1,
            none = 0,
            l = 1,
            ll,
            j,
            z,
            t,
            L
        } length_mod = none;
        switch(*arg) {
        case 'h': // short
        case 'l': // long or wchar
            if (arg[1] == arg[0]) {
                // hh or ll
                arg += 2;
                length_mod = ll;
            } else {
                arg++;
                length_mod = l;
            }
            break;
        case 'q': // same as ll
            length_mod = ll;
            break;
            break;
        case 'j': // (u)intmax_t
            length_mod = j;
            arg++;
            break;
        case 'z': // size_t
            length_mod = z;
            arg++;
            break;
        case 't': // ptrdiff_t
            length_mod = t;
            arg++;
            break;
        case 'L': // long double is 16 bytes on x64, but 8 on aarch64
            if (replace_long_double) {
                memmove((void*)arg, arg+1, strlen(arg));
                break;
            }
            length_mod = L;
            arg++;
            break;
        default:
            break;
        }
        
        // conversion specifier
        char arg_enc = 0;
        switch(*arg) {
        // signed ints
        case 'd':
        case 'D':
        case 'i': switch (length_mod) {
            case hh:
                arg_enc = 'c'; break;
            case h:
                arg_enc = 's'; break;
            case l:
            case ll:
            case j:
            case z:
            case t:
                arg_enc = 'q'; break;
            default:
                arg_enc = 'i'; break;
        } break;
        // unsigned ints
        case 'o':
        case 'O':
        case 'u':
        case 'U':
        case 'x':
        case 'X': switch (length_mod) {
            case hh:
                arg_enc = 'C'; break;
            case h:
                arg_enc = 'S'; break;
            case l:
            case ll:
            case j:
            case z:
            case t:
                arg_enc = 'Q'; break;
            default:
                arg_enc = 'I'; break;
        } break;
        // doubles
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            arg_enc = (length_mod == L) ? 'D' : 'd';
            break;
        // chars
        case 'C':
            length_mod = l;
        case 'c':
            arg_enc = (length_mod == l) ? 'i' : 'C';
            break;
        // pointers
        case 'S':
        case 's':
        case 'p':
        case 'n':
            arg_enc = '*';
            break;
        case '@':
            if (is_nsstring) {
                arg_enc = '*';
                break;
            }
        default:
            printf("unknown conversion specifier '%1$c' (%1$hhd) in \"%2$s\" at %3$d", *arg, fmt, (int)(arg-fmt));
            abort();
        }
        
        if (type_encoding && arg_enc) type_encoding[pos] = arg_enc;
    }
    
    if (type_encoding) type_encoding[nargs] = 0;
    return nargs;
}

int CountStringFormatArgs(const char *format) {
    return EncodeStringFormatArgs((char*)format, NULL, 1, 0);
}
