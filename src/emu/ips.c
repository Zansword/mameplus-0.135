/***************************************************************************

    ips.c

    International Patching System.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

***************************************************************************/

#include "driver.h"
#include "ips.h"
#include "hash.h"


#define IPS_SIGNATURE	"PATCH"
#define IPS_TAG_EOF	"EOF"
#define INDEX_EXT	".dat"
#define IPS_EXT		".ips"
#define CRC_STAG	"CRC("
#define CRC_ETAG	")"

#define BYTE3_TO_UINT(bp) \
	(((bp[0] << 16) & 0x00ff0000) | \
	 ((bp[1] << 8)  & 0x0000ff00) | \
	 ((bp[2] << 0)  & 0x000000ff))

#define BYTE2_TO_UINT(bp) \
	(((bp[0] << 8) & 0xff00) | \
	 ((bp[1] << 0) & 0x00ff))


typedef struct _ips_chunk
{
	struct _ips_chunk *next;
	int offset;
	int size;
	char *data;
} ips_chunk;

typedef struct _ips_entry
{
	struct _ips_entry *next;
	char *rom_name;
	char *ips_name;
	ips_chunk *chunk;
	ips_chunk current;
} ips_entry;


static ips_entry *ips_list;


static const rom_entry *find_rom_entry(const rom_entry *romp, const char *name)
{
	const rom_entry *region, *rom;

	for (region = romp; region; region = rom_next_region(region))
	{
		if (!ROMREGION_ISROMDATA(region))
			continue;

		for (rom = rom_first_file(region); rom; rom = rom_next_file(rom))
		{
			if (!ROMENTRY_ISFILE(rom))
				continue;

			if (mame_stricmp(ROM_GETNAME(rom), name) == 0)
				return rom;
		}
	}

	return NULL;
}

static int load_ips_file(ips_chunk **p, const char *ips_dir, const char *ips_name, rom_load_data *romdata /* , UINT32 length */ )
{
	mame_file *file;
	file_error filerr;
	astring *fname;
	UINT32 pos = 0;
	UINT8 buffer[8];
	int len;

	mame_printf_verbose("IPS: loading ips \"%s/%s%s\"\n", ips_dir, ips_name, IPS_EXT);

	fname = astring_assemble_4(astring_alloc(), ips_dir, PATH_SEPARATOR, ips_name, IPS_EXT);
	filerr = mame_fopen(SEARCHPATH_IPS, astring_c(fname), OPEN_FLAG_READ, &file);
	astring_free(fname);

	if (filerr != FILERR_NONE)
	{
		astring_catprintf(romdata->errorstring,
			_("ERROR: %s/%s: open fail\n"), ips_dir, ips_name);
		romdata->warnings++;

		return 0;
	}

	len = strlen(IPS_SIGNATURE);
	if (mame_fread(file, buffer, len) != len || strncmp(buffer, IPS_SIGNATURE, len) != 0)
	{
		astring_catprintf(romdata->errorstring,
			_("ERROR: %s/%s: incorrent IPS header\n"), ips_dir, ips_name);
		goto load_ips_file_fail;
	}

	while (!mame_feof(file))
	{
		UINT32 offset;
		UINT16 size;
		int bRLE = 0;

		if (mame_fread(file, buffer, 3) != 3)
			goto load_ips_file_unexpected_eof;

		if (strncmp(buffer, IPS_TAG_EOF, 3) == 0)
			break;

		offset = BYTE3_TO_UINT(buffer);

		if (mame_fread(file, buffer, 2) != 2)
			goto load_ips_file_unexpected_eof;

		size = BYTE2_TO_UINT(buffer);
		if (size == 0)
		{
			if (mame_fread(file, buffer, 3) != 3)
				goto load_ips_file_unexpected_eof;

			size = BYTE2_TO_UINT(buffer);
			bRLE = 1;
		}

		*p = malloc(sizeof (ips_chunk));
		if (*p)
			(*p)->data = malloc(size);

		if (!*p || !(*p)->data)
		{
			astring_catc(romdata->errorstring, _("ERROR: IPS: not enough memory\n"));
			goto load_ips_file_fail;
		}

		if (bRLE)
			memset((*p)->data, buffer[2], size);
		else
		{
			if (mame_fread(file, (*p)->data, size) != size)
				goto load_ips_file_unexpected_eof;
		}

		//logerror("IPS: offset = %d, size = %d\n", offset, size);
		offset -= pos;
		(*p)->offset = offset;
		(*p)->size = size;
		(*p)->next = NULL;

		p = &(*p)->next;
		pos += offset + size;
	}

	mame_fclose(file);

	return 1;

load_ips_file_unexpected_eof:
	astring_catprintf(romdata->errorstring,
		_("ERROR: %s/%s: unexpected EOF\n"), ips_dir, ips_name);

load_ips_file_fail:
	mame_fclose(file);

	romdata->warnings++;

	return 0;
}

static int check_crc(char *crc, const char *rom_hash)
{
	char ips_hash[HASH_BUF_SIZE];
	char tmp[10];
	int slen = strlen(CRC_STAG);
	int elen = strlen(CRC_ETAG);

	if (crc == NULL)
		return 0;

	if (strlen(crc) != 8 + slen + elen)
		return 0;

	if (strncmp(crc, CRC_STAG, slen) != 0)
		return 0;

	if (strcmp(crc + 8 + slen, CRC_ETAG) != 0)
		return 0;

	strcpy(tmp, crc + slen);
	tmp[8] = '\0';

	hash_data_clear(ips_hash);
	if (hash_data_insert_printable_checksum(ips_hash, HASH_CRC, tmp) != 1)
		return 0;

	if (hash_data_is_equal(rom_hash, ips_hash, HASH_CRC) != 1)
		return 0;

	return 1;
}

static int parse_ips_patch(running_machine *machine, ips_entry **ips_p, const char *patch_name, rom_load_data *romdata, const rom_entry *romp)
{
	UINT8 buffer[1024];
	mame_file *fpDat;
	file_error filerr;
	astring *fname;
	int result = 0;

	mame_printf_verbose("IPS: parsing ips \"%s/%s%s\"\n", machine->gamedrv->name, patch_name, INDEX_EXT);

	fname = astring_assemble_4(astring_alloc(), machine->gamedrv->name, PATH_SEPARATOR, patch_name, INDEX_EXT);
	filerr = mame_fopen(SEARCHPATH_IPS, astring_c(fname), OPEN_FLAG_READ, &fpDat);
	astring_free(fname);

	if (filerr != FILERR_NONE)
	{
		astring_catprintf(romdata->errorstring,
			_("ERROR: %s: IPS file is not found\n"), patch_name);
		romdata->warnings++;

		return 0;
	}

	while (!mame_feof(fpDat))
	{
		if (mame_fgets(buffer, sizeof (buffer), fpDat) != NULL)
		{
			ips_entry *entry;
			const rom_entry *current;
			UINT8 *p = buffer;
			char *rom_name;
			const char *ips_dir;
			char *ips_name;
			char *crc;

			if (p[0] == '[')	// '['
				break;

			rom_name = strtok(p, " \t\r\n");
			if (!rom_name)
				continue;
			if (rom_name[0] == '#')
				continue;

			logerror("IPS: target rom name: \"%s\"\n", rom_name);

			current = find_rom_entry(romp, rom_name);
			if (!current)
			{
				astring_catprintf(romdata->errorstring,
					_("ERROR: ROM entry \"%s\" is not found for IPS file \"%s\"\n"), rom_name, patch_name);
				goto parse_ips_patch_fail;
			}

			ips_name = strtok(NULL, " \t\r\n");
			if (!ips_name)
			{
				astring_catprintf(romdata->errorstring,
					_("ERROR: IPS file is not defined for ROM entry \"%s\"\n"), rom_name);
				goto parse_ips_patch_fail;
			}

			crc = strtok(NULL, "\r\n");
			strtok(NULL, "\r\n");

			if (crc && !check_crc(crc, ROM_GETHASHDATA(current)))
			{
				astring_catprintf(romdata->errorstring,
					_("ERROR: wrong CRC for ROM entry \"%s\"\n"), rom_name);
				goto parse_ips_patch_fail;
			}

			result = 1;

			if (strchr(ips_name, '\\'))
			{
				ips_dir = strtok(ips_name, "\\");
				ips_name = strtok(NULL, "\\");
			}
			else
			{
				ips_dir = machine->gamedrv->name;
			}

			entry = malloc(sizeof (*entry));
			memset(entry, 0, sizeof (*entry));
			*ips_p = entry;
			ips_p = &entry->next;

			entry->rom_name = mame_strdup(rom_name);
			entry->ips_name = mame_strdup(ips_name);
			if (!entry->rom_name || !entry->ips_name)
			{
				astring_catc(romdata->errorstring, _("ERROR: IPS: not enough memory\n"));
				goto parse_ips_patch_fail;
			}

			if (!load_ips_file(&entry->chunk, ips_dir, entry->ips_name, romdata))
				goto parse_ips_patch_fail;

			if (entry->chunk == NULL)
			{
				astring_catprintf(romdata->errorstring,
					_("ERROR: %s/%s: IPS data is empty\n"), ips_dir, entry->ips_name);
				goto parse_ips_patch_fail;
			}
		}
	}
	mame_fclose(fpDat);

	return result;

parse_ips_patch_fail:
	mame_fclose(fpDat);

	romdata->warnings++;

	return 0;
}


int open_ips_entry(running_machine *machine, const char *patch_name, rom_load_data *romdata, const rom_entry *romp)
{
	int result = 0;
	char *s = mame_strdup(patch_name);
	char *p, *q;
	ips_entry **list;

	ips_list = NULL;
	list = &ips_list;

	for (p = s; *p; p = q)
	{

		for (q = p; *q; q++)
			if (*q == ',')
			{
				*q++ = '\0';
				break;
			}

		result = parse_ips_patch(machine, list, p, romdata, romp);
		if (!result)
			return result;

		while (*list)
			list = &(*list)->next;
	}

	free(s);

	if (!result)
	{
		astring_catprintf(romdata->errorstring,
			_("ERROR: %s: IPS file is not found\n"), patch_name);
		romdata->warnings++;
	}

	return result;
}

int close_ips_entry(rom_load_data *romdata)
{
	ips_entry *p;
	ips_entry *next;
	int result = 1;

	for (p = ips_list; p; p = next)
	{
		ips_chunk *chunk;
		ips_chunk *next_chunk;

		if (p->current.data)
		{
			astring_catprintf(romdata->errorstring,
				_("ERROR: %s: ips is not applied correctly to ROM entry \"%s\"\n"), p->ips_name, p->rom_name);
			romdata->warnings++;

			result = 0;
		}

		for (chunk = p->chunk; chunk; chunk = next_chunk)
		{
			next_chunk = chunk->next;
			free(chunk);
		}

		if (p->ips_name)
			free(p->ips_name);

		if (p->rom_name)
			free(p->rom_name);

		next = p->next;
		free(p);
	}

	ips_list = NULL;

	return result;
}

void *assign_ips_patch(const rom_entry *romp)
{
	const char *name = ROM_GETNAME(romp);
	ips_entry *p;
	int found = 0;

	for (p = ips_list; p; p = p->next)
	{
		memset(&p->current, 0, sizeof (p->current));

		if (mame_stricmp(p->rom_name, name) == 0)
		{
			logerror("IPS: assign IPS file \"%s\" to ROM entry \"%s\"\n", p->ips_name, p->rom_name);
			p->current = *p->chunk;

			found = 1;
		}
	}

	if (found)
		return ips_list;

	return NULL;
}

static void apply_ips_patch_single(ips_chunk *p, UINT8 *buffer, int length)
{
	if (!p->data)
		return;

	while (1)
	{
		if (p->offset >= length)
		{
			p->offset -= length;
			return;
		}

		length -= p->offset;
		buffer += p->offset;
		p->offset = 0;

		if (p->size > length)
		{
			//logerror("IPS: apply %d bytes\n", length);
			memcpy(buffer, p->data, length);
			p->size -= length;
			p->data += length;
			return;
		}

		//logerror("IPS: apply %d bytes\n", p->size);
		memcpy(buffer, p->data, p->size);
		length -= p->size;
		buffer += p->size;

		p->size = 0;
		p->data = NULL;

		if (!p->next)
		{
			logerror("IPS: apply IPS done\n");
			return;
		}

		*p = *p->next;
	}
}

void apply_ips_patch(void *patch, UINT8 *buffer, int length)
{
	ips_entry *p;

	for (p = (ips_entry *)patch; p; p = p->next)
		if (p->current.data)
			apply_ips_patch_single(&p->current, buffer, length);
}
