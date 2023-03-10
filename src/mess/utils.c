#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "utils.h"

char *strncpyz(char *dest, const char *source, size_t len)
{
	char *s;
	if (len) {
		s = strncpy(dest, source, len - 1);
		dest[len-1] = '\0';
	}
	else {
		s = dest;
	}
	return s;
}

char *strncatz(char *dest, const char *source, size_t len)
{
	size_t l;
	l = strlen(dest);
	dest += l;
	if (len > l)
		len -= l;
	else
		len = 0;
	return strncpyz(dest, source, len);
}

void rtrim(char *buf)
{
	size_t buflen;
	char *s;

	buflen = strlen(buf);
	if (buflen)
	{
		for (s = &buf[buflen-1]; s >= buf && (*s >= '\0') && isspace(*s); s--)
			*s = '\0';
	}
}

#ifndef strncmpi
int strncmpi(const char *dst, const char *src, size_t n)
{
	int result = 0;

	while( !result && *src && *dst && n)
	{
		result = tolower(*dst) - tolower(*src);
		src++;
		dst++;
		n--;
	}
	return result;
}
#endif /* strncmpi */

#ifndef memset16
void *memset16 (void *dest, int value, size_t size)
{
	register int i;

	for (i = 0; i < size; i++)
		((short *) dest)[i] = value;
	return dest;
}
#endif

char *stripspace(const char *src)
{
	static char buff[512];
	if( src )
	{
		char *dst;
		while( *src && isspace(*src) )
			src++;
		strcpy(buff, src);
		dst = buff + strlen(buff);
		while( dst >= buff && isspace(*--dst) )
			*dst = '\0';
		return buff;
	}
	return NULL;
}

//============================================================
//	strip_extension
//============================================================

char *strip_extension(const char *filename)
{
	char *newname;
	char *c;

	// NULL begets NULL
	if (!filename)
		return NULL;

	// allocate space for it
	newname = (char *) malloc(strlen(filename) + 1);
	if (!newname)
		return NULL;

	// copy in the name
	strcpy(newname, filename);

	// search backward for a period, failing if we hit a slash or a colon
	for (c = newname + strlen(newname) - 1; c >= newname; c--)
	{
		// if we hit a period, NULL terminate and break
		if (*c == '.')
		{
			*c = 0;
			break;
		}

		// if we hit a slash or colon just stop
		if (*c == '\\' || *c == '/' || *c == ':')
			break;
	}

	return newname;
}


int compute_log2(int val)
{
	int count = 0;

	while(val > 1)
	{
		if (val & 1)
			return -1;
		val /= 2;
		count++;
	}
	return (val == 0) ? -1 : count;
}



/*
   Compute CCITT CRC-16 using the correct bit order for floppy disks.
   CRC code courtesy of Tim Mann.
*/

/* Accelerator table to compute the CRC eight bits at a time */
static const unsigned short ccitt_crc16_table[256] =
{
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
	0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
	0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
	0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
	0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
	0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
	0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
	0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
	0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
	0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
	0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
	0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
	0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
	0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
	0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
	0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
	0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
	0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
	0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
	0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
	0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
	0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
	0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

unsigned short ccitt_crc16(unsigned short crc, const unsigned char *buffer, size_t buffer_len)
{
	size_t i;
	for (i = 0; i < buffer_len; i++)
		crc = (crc << 8) ^ ccitt_crc16_table[(crc >> 8) ^ buffer[i]];
	return crc;
}

unsigned short ccitt_crc16_one( unsigned short crc, const unsigned char data )
{
    return (crc << 8) ^ ccitt_crc16_table[(crc >> 8) ^ data];
}



int hexdigit(char c)
{
	int result = 0;
	if (isdigit(c))
		result = c - '0';
	else if (isxdigit(c))
		result = toupper(c) - 'A' + 10;
	return result;
}



/*-------------------------------------------------
    is_delim - returns whether c is a comma or \0
-------------------------------------------------*/

static int is_delim(char c)
{
	return (c == '\0') || (c == ',');
}



/*-------------------------------------------------
    internal_find_extension - find an extension in
	an extension list
-------------------------------------------------*/

static int internal_find_extension(const char *extension_list, const char *target_extension)
{
	/* this version allows target_extension to be delimited with a comma */
	int pos = 0;
	int i;

	while(extension_list[pos] != '\0')
	{
		/* compare a file extension */
		i = 0;
		while(!is_delim(extension_list[pos + i])
			&& !is_delim(target_extension[i])
			&& (tolower(extension_list[pos + i]) == tolower(target_extension[i])))
		{
			i++;
		}

		/* check to see if it was found */
		if (is_delim(extension_list[pos + i]) && is_delim(target_extension[i]))
			return TRUE;

		/* move to next position in the buffer */
		pos += i;
		while(!is_delim(extension_list[pos]))
			pos++;
		while(extension_list[pos] == ',')
			pos++;
	}

	/* not found */
	return FALSE;
}



/*-------------------------------------------------
    find_extension - find an extension in an extension
	list
-------------------------------------------------*/

int find_extension(const char *extension_list, const char *target_extension)
{
	/* the internal function allows something that we do not */
	if (strchr(target_extension, ','))
		return FALSE;

	/* special case to allow ext to be in the form '.EXT' */
	if (*target_extension == '.')
		target_extension++;

	/* do the actual work */
	return internal_find_extension(extension_list, target_extension);
}



/*-------------------------------------------------
    specify_extension - merge a comma-delimited
	list of file extensions onto an existing list
-------------------------------------------------*/

void specify_extension(char *buffer, size_t buffer_len, const char *extension)
{
	int extension_pos = 0;
	int len;
	int found;

	/* determine the length of the buffer */
	len = strlen(buffer);

	/* be aware that extension can be NULL */
	if (extension != NULL)
	{
		while(extension[extension_pos] != '\0')
		{
			/* try to find the file extension */
			found = internal_find_extension(buffer, &extension[extension_pos]);

			/* append a delimiter if we have to */
			if (!found && (len > 0))
				len += snprintf(&buffer[len], buffer_len - len, ",");

			/* move to the next extension, appending the extension if not found */
			while(!is_delim(extension[extension_pos]))
			{
				if (!found)
					len += snprintf(&buffer[len], buffer_len - len, "%c", extension[extension_pos]);
				extension_pos++;
			}
			while(extension[extension_pos] == ',')
				extension_pos++;
		}
	}
}



/*-------------------------------------------------
    utils_validitychecks - unit tests
-------------------------------------------------*/

int utils_validitychecks(void)
{
	char buffer[256];
	int error = FALSE;

	/* test 1 */
	buffer[0] = '\0';
	specify_extension(buffer, ARRAY_LENGTH(buffer), "abc");
	if (strcmp(buffer, "abc"))
	{
		printf("Invalid validity check result\n");
		error = TRUE;
	}

	/* test 2 */
	buffer[0] = '\0';
	specify_extension(buffer, ARRAY_LENGTH(buffer), "abc,def,ghi");
	specify_extension(buffer, ARRAY_LENGTH(buffer), "jkl,mno,ghi");
	if (strcmp(buffer, "abc,def,ghi,jkl,mno"))
	{
		printf("Invalid validity check result\n");
		error = TRUE;
	}

	/* test 3 */
	buffer[0] = '\0';
	specify_extension(buffer, ARRAY_LENGTH(buffer), "abc,def,ghi");
	specify_extension(buffer, ARRAY_LENGTH(buffer), "abc,jkl,mno");
	if (strcmp(buffer, "abc,def,ghi,jkl,mno"))
	{
		printf("Invalid validity check result\n");
		error = TRUE;
	}

	return error;
}
