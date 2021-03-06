#define _GNU_SOURCE
#include <string.h>

#include "banner.h"

static const char *typemap_low[1024] = {
	[21] = "ftp",
	[22] = "ssh",
	[23] = "telnet",
	[53] = "domain",
	[80] = "http",
};

const char *banner_service_type(int port)
{
	if(port < 1024)
		return typemap_low[port];
	switch(port) {
		case 8080:
			return typemap_low[80];
		default:
			return NULL;
	}
}

const char *banner_get_query(int port, unsigned int *len)
{
	static const char ftp[] =
		"HELP\r\n"
		"FEAT\r\n"
	;
	static const char dns[] =
		"\x00\x1e" // Length field (TCP only)
		"\x12\x34" // Transaction ID
		"\x01\x00" // QUERY opcode, RD=1
		"\x00\x01\x00\x00\x00\x00\x00\x00" // 1 query
		"\x07" "version" "\x04" "bind" "\x00\x00\x10\x00\x03" // version.bind.  CH  TXT
	;
	static const char http[] =
		"GET / HTTP/1.0\r\n"
		"Accept: */*\r\n"
		"User-Agent: fi6s/0.1 (+https://github.com/sfan5/fi6s)\r\n"
		"\r\n"
	;

	switch(port) {
		case 21:
			*len = strlen(ftp);
			return ftp;
		case 22:
		case 23:
			*len = 0;
			return "";
		case 53:
			*len = sizeof(dns) - 1; // mind the null byte!
			return dns;
		case 80:
		case 8080:
			*len = strlen(http);
			return http;
		default:
			return NULL;
	}
}

void banner_postprocess(int port, char *banner, unsigned int *len)
{
	switch(port) {
		case 22: {
			// cut off after identification string or first NUL
			char *end;
			end = (char*) memmem(banner, *len, "\r\n", 2);
			if(!end)
				end = (char*) memchr(banner, 0, *len);
			if(end)
				*len = end - banner;
			break;
		}

#define BREAK_ERR_IF(expr) \
	if(expr) { *len = 0; break; }
#define SKIP_LABELS() \
	while(off < *len) { \
		if((banner[off] & 0xc0) == 0xc0) /* message compression */ \
			{ off += 2; break; } \
		else if(banner[off] > 0) /* ordinary label */ \
			{ off += 1 + banner[off]; } \
		else /* terminating zero-length label */ \
			{ off += 1; break; } \
	}
		case 53: {
			int off = 2; // skip length field
			BREAK_ERR_IF(off + 12 > *len)
			uint16_t flags = (banner[off+2] << 8) | banner[off+3];
			if((flags & 0x8000) != 0x8000 || (flags & 0xf) != 0x0) {
				strncpy(banner, "<SERVFAIL>", 10);
				*len = 10;
				break;
			}

			uint16_t qdcount = (banner[off+4] << 8) | banner[off+5];
			uint16_t ancount = (banner[off+6] << 8) | banner[off+7];
			BREAK_ERR_IF(qdcount != 1 || ancount < 1)
			off += 12;
			// skip query
			SKIP_LABELS()
			off += 4;
			BREAK_ERR_IF(off > *len)

			// parse answer record
			SKIP_LABELS()
			BREAK_ERR_IF(off + 10 > *len)
			uint16_t rr_type = (banner[off] << 8) | banner[off+1];
			uint16_t rr_rdlength = (banner[off+8] << 8) | banner[off+9];
			BREAK_ERR_IF(rr_type != 0x0010 /* TXT */)
			BREAK_ERR_IF(rr_rdlength < 2)
			off += 10;
			BREAK_ERR_IF(off + rr_rdlength > *len)

			// return just the TXT record contents
			memmove(banner, &banner[off+1], rr_rdlength - 1);
			*len = rr_rdlength - 1;
			break;
		}
#undef BREAK_ERR_IF
#undef SKIP_LABELS

		case 80:
		case 8080: {
			// cut off after headers
			char *end = (char*) memmem(banner, *len, "\r\n\r\n", 4);
			if(!end)
				end = (char*) memmem(banner, *len, "\n\n", 2);
			if(end)
				*len = end - banner;
			break;
		}

		default:
			break; // do nothing
	}
}
