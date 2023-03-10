/****************************************************************************

    image.c

    Code for handling devices/software images

    The UI can call image_load and image_unload to associate and disassociate
    with disk images on disk.  In fact, devices that unmount on their own (like
    Mac floppy drives) may call this from within a driver.

****************************************************************************/

#include <ctype.h>

#include "mame.h"
#include "image.h"
#include "hash.h"
#include "unzip.h"
#include "devices/flopdrv.h"
#include "utils.h"
#include "hashfile.h"
#include "mamecore.h"
#include "messopts.h"
#include "ui.h"
#include "zippath.h"



/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _image_slot_data image_slot_data;
struct _image_slot_data
{
    /* variables that persist across image mounts */
    object_pool *mempool;
    const device_config *dev;
    image_device_info info;

    /* creation info */
    const option_guide *create_option_guide;
    image_device_format *formatlist;

    /* callbacks */
    device_image_load_func load;
    device_image_create_func create;
    device_image_unload_func unload;
    device_image_verify_func verify;

    /* error related info */
    image_error_t err;
    char *err_message;

    /* variables that are only non-zero when an image is mounted */
    core_file *file;
    astring *name;
    char *dir;
    char *hash;
    char *basename_noext;

    /* flags */
    unsigned int writeable : 1;
    unsigned int created : 1;
    unsigned int is_loading : 1;

    /* info read from the hash file */
    char *longname;
    char *manufacturer;
    char *year;
    char *playable;
    char *pcb;
    char *extrainfo;

    /* working directory; persists across mounts */
    astring *working_directory;

    /* special - used when creating */
    int create_format;
    option_resolution *create_args;

    /* pointer */
    void *ptr;
};



struct _images_private
{
    int slot_count;
    image_slot_data slots[1];
};



/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static void image_clear(image_slot_data *image);
static void image_clear_error(image_slot_data *image);
static void image_unload_internal(image_slot_data *slot);
static image_slot_data *find_image_slot(const device_config *image);



/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    device_get_info_int_offline - return an integer
    state value from an allocated device; can be
    called offline
-------------------------------------------------*/

INLINE INT64 device_get_info_int_offline(const device_config *device, UINT32 state)
{
    deviceinfo info;

    assert(device != NULL);
    assert(device->type != NULL);
    assert(state >= DEVINFO_INT_FIRST && state <= DEVINFO_INT_LAST);

    /* retrieve the value */
    info.i = 0;
    (*device->type)(device, state, &info);
    return info.i;
}



/*-------------------------------------------------
    device_get_info_fct_offline - return an integer
    state value from an allocated device; can be
    called offline
-------------------------------------------------*/

INLINE genf *device_get_info_fct_offline(const device_config *device, UINT32 state)
{
    deviceinfo info;

    assert(device != NULL);
    assert(device->type != NULL);
    assert(state >= DEVINFO_FCT_FIRST && state <= DEVINFO_FCT_LAST);

    /* retrieve the value */
    info.f = 0;
    (*device->type)(device, state, &info);
    return info.f;
}



/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

/*-------------------------------------------------
    memory_error - report a memory error
-------------------------------------------------*/

static void memory_error(const char *message)
{
    fatalerror("%s", message);
}



/*-------------------------------------------------
    hash_data_extract_crc32 - extract crc32 value
    from hash string
-------------------------------------------------*/

static UINT32 hash_data_extract_crc32(const char *d)
{
	UINT32 crc = 0;
	UINT8 crc_bytes[4];

	if (hash_data_extract_binary_checksum(d, HASH_CRC, crc_bytes) == 1)
	{
		crc = (((UINT32) crc_bytes[0]) << 24)
			| (((UINT32) crc_bytes[1]) << 16)
			| (((UINT32) crc_bytes[2]) << 8)
			| (((UINT32) crc_bytes[3]) << 0);
	}
	return crc;
}



/*-------------------------------------------------
    image_init - initialize the core image system
-------------------------------------------------*/

void image_init(running_machine *machine)
{
    int count, indx, format_count, i,cnt;
    const device_config *dev;
    size_t private_size;
    image_slot_data *slot;
    image_device_format **formatptr;
    image_device_format *format;

    /* sanity checks */
    assert(DEVINFO_FCT_IMAGE_FIRST > DEVINFO_FCT_FIRST);
    assert(DEVINFO_FCT_IMAGE_LAST < DEVINFO_FCT_DEVICE_SPECIFIC);

    /* first count all images, and identify multiply defined devices */
    count = image_device_count(machine->config);

    /* allocate the private structure */
    private_size = sizeof(*machine->images_data) + ((count - 1)
        * sizeof(machine->images_data->slots[0]));
    machine->images_data = (images_private *) auto_alloc_array_clear(machine, char, private_size);
    memset(machine->images_data, '\0', private_size);

    /* some setup */
    machine->images_data->slot_count = count;

    /* initialize the devices */
    indx = 0;
    for (dev = image_device_first(machine->config); dev != NULL; dev = image_device_next(dev))
    {
        slot = &machine->images_data->slots[indx];

        /* create a memory pool, and allocated strings */
        slot->mempool = pool_alloc(memory_error);
        slot->name = astring_alloc();
        slot->working_directory = astring_alloc();

        /* clear error message */
        slot->err_message = NULL;

        /* setup the device */
        slot->dev = dev;
        slot->info = image_device_getinfo(machine->config, dev);

        /* callbacks */
        slot->load = (device_image_load_func) device_get_info_fct(slot->dev, DEVINFO_FCT_IMAGE_LOAD);
        slot->create = (device_image_create_func) device_get_info_fct(slot->dev, DEVINFO_FCT_IMAGE_CREATE);
        slot->unload = (device_image_unload_func) device_get_info_fct(slot->dev, DEVINFO_FCT_IMAGE_UNLOAD);
        slot->verify = (device_image_verify_func) device_get_info_fct(slot->dev, DEVINFO_FCT_IMAGE_VERIFY);

        /* creation option guide */
        slot->create_option_guide = (const option_guide *) device_get_info_ptr(slot->dev, DEVINFO_PTR_IMAGE_CREATE_OPTGUIDE);

        /* creation formats */
        format_count = device_get_info_int(slot->dev, DEVINFO_INT_IMAGE_CREATE_OPTCOUNT);
        formatptr = &slot->formatlist;
        cnt = 0;
        for (i = 0; i < format_count; i++)
        {
        	// only add if creatable
        	if (device_get_info_ptr(slot->dev, DEVINFO_PTR_IMAGE_CREATE_OPTSPEC + i)) {
	            /* allocate a new format */
	            format = auto_alloc_clear(machine, image_device_format);

	            /* populate it */
	            format->index       = cnt;
	            format->name        = auto_strdup(machine, device_get_info_string(slot->dev, DEVINFO_STR_IMAGE_CREATE_OPTNAME + i));
	            format->description = auto_strdup(machine, device_get_info_string(slot->dev, DEVINFO_STR_IMAGE_CREATE_OPTDESC + i));
	            format->extensions  = auto_strdup(machine, device_get_info_string(slot->dev, DEVINFO_STR_IMAGE_CREATE_OPTEXTS + i));
	            format->optspec     = device_get_info_ptr(slot->dev, DEVINFO_PTR_IMAGE_CREATE_OPTSPEC + i);

	            /* and append it to the list */
	            *formatptr = format;
	            formatptr = &format->next;
	            cnt++;
	        }
        }

        indx++;
    }
}



/*-------------------------------------------------
    image_unload_all - unload all images and
    extract options
-------------------------------------------------*/

void image_unload_all(running_machine *machine)
{
    int i;
    image_slot_data *slot;

    if (machine->images_data != NULL)
    {
        /* extract the options */
        mess_options_extract(machine);

        for (i = 0; i < machine->images_data->slot_count; i++)
        {
            /* identify the image slot */
            slot = &machine->images_data->slots[i];

            /* unload this image */
            image_unload_internal(slot);

            /* free the memory pool */
            pool_free(slot->mempool);
            slot->mempool = NULL;

            /* free the allocated strings */
            astring_free(slot->name);
            astring_free(slot->working_directory);
            slot->name = NULL;
            slot->working_directory = NULL;
        }

        machine->images_data = NULL;
    }
}



/****************************************************************************
    IMAGE DEVICE ENUMERATION
****************************************************************************/

/*-------------------------------------------------
    is_image_device - determines if a particular
    device supports images or not
-------------------------------------------------*/

static int is_image_device(const device_config *device)
{
    return (device->type == MESS_DEVICE)
        || (device_get_info_int_offline(device, DEVINFO_INT_IMAGE_READABLE) != 0)
        || (device_get_info_int_offline(device, DEVINFO_INT_IMAGE_WRITEABLE) != 0);
}



/*-------------------------------------------------
    image_device_first - return the first device in
    the list that supports images
-------------------------------------------------*/

const device_config *image_device_first(const machine_config *config)
{
    const device_config *device = device_list_first(config->devicelist, DEVICE_TYPE_WILDCARD);
    while((device != NULL) && !is_image_device(device))
    {
        device = device_list_next(device, DEVICE_TYPE_WILDCARD);
    }
    return device;
}



/*-------------------------------------------------
    image_device_next - return the next device in
    the list that supports images
-------------------------------------------------*/

const device_config *image_device_next(const device_config *prevdevice)
{
    const device_config *device = device_list_next(prevdevice, DEVICE_TYPE_WILDCARD);
    while((device != NULL) && !is_image_device(device))
    {
        device = device_list_next(device, DEVICE_TYPE_WILDCARD);
    }
    return device;
}



/*-------------------------------------------------
    image_device_count - counts the number of
    devices that support images
-------------------------------------------------*/

int image_device_count(const machine_config *config)
{
    int count = 0;
    const device_config *device;
    for (device = image_device_first(config); device != NULL; device = image_device_next(device))
    {
        count++;
    }
    return count;
}



/****************************************************************************
    ANALYSIS
****************************************************************************/

/*-------------------------------------------------
    get_device_name - retrieves the name of a
    device
-------------------------------------------------*/

static void get_device_name(const device_config *device, char *buffer, size_t buffer_len)
{
    const char *name = NULL;
    device_get_name_func get_name;

    if (name == NULL)
    {
        /* first try a get_name function */
        get_name = (device_get_name_func) device_get_info_fct_offline(device, DEVINFO_FCT_GET_NAME);
        if (get_name != NULL)
            name = get_name(device, buffer, buffer_len);
    }

    if (name == NULL)
    {
        /* next try DEVINFO_STR_NAME */
        name = device_get_info_string(device, DEVINFO_STR_NAME);
    }

    /* make sure that the name is put into the buffer */
    if (name != buffer)
        snprintf(buffer, buffer_len, "%s", (name != NULL) ? name : "");
}



/*-------------------------------------------------
    get_device_file_extensions - retrieves the file
    extensions used by a device
-------------------------------------------------*/

static void get_device_file_extensions(const device_config *device,
    char *buffer, size_t buffer_len)
{
    const char *file_extensions;
    char *s;

    /* be pedantic - we need room for a string list */
    assert(buffer_len > 0);
    buffer_len--;

    /* copy the string */
    file_extensions = device_get_info_string(device, DEVINFO_STR_IMAGE_FILE_EXTENSIONS);
    snprintf(buffer, buffer_len, "%s", (file_extensions != NULL) ? file_extensions : "");

    /* convert the comma delimited list to a NUL delimited list */
    s = buffer;
    while((s = strchr(s, ',')) != NULL)
        *(s++) = '\0';
}



/*-------------------------------------------------
    get_device_instance_name - retrieves the device
    instance name or brief instance name
-------------------------------------------------*/

static void get_device_instance_name(const machine_config *config, const device_config *device,
    char *buffer, size_t buffer_len, iodevice_t type, UINT32 state, const char *(*get_dev_typename)(iodevice_t))
{
    const char *result;
    const device_config *that_device;
    int count, index;

    /* retrieve info about the device instance */
    result = device_get_info_string(device, state);
    if ((result != NULL) && (result[0] != '\0'))
    {
        /* we got info directly */
        snprintf(buffer, buffer_len, "%s", result);
    }
    else
    {
        /* not specified? default to device names based on the device type */
        result = get_dev_typename(type);

        /* are there multiple devices of the same type */
        count = 0;
        index = -1;
        for (that_device = image_device_first(config); that_device != NULL; that_device = image_device_next(that_device))
        {
            if (device == that_device)
                index = count;
            if (device_get_info_int_offline(that_device, DEVINFO_INT_IMAGE_TYPE) == type)
                count++;
        }

        /* need to number if there is more than one device */
        if (count > 1)
            snprintf(buffer, buffer_len, "%s%d", result, index + 1);
        else
            snprintf(buffer, buffer_len, "%s", result);
    }
}



/*-------------------------------------------------
    image_device_getinfo - returns info on a device;
    can be called by front end code
-------------------------------------------------*/

image_device_info image_device_getinfo(const machine_config *config, const device_config *device)
{
    const device_config *this_device;
    image_device_info info;
    int found;

    /* sanity checks */
    assert((device->machine == NULL) || (device->machine->config == config));
    if (device->machine == NULL)
    {
        found = FALSE;
        for (this_device = image_device_first(config); this_device != NULL; this_device = image_device_next(this_device))
        {
            if (this_device == device)
                found = TRUE;
        }
        assert(found);
    }

    /* clear return value */
    memset(&info, 0, sizeof(info));

    /* retrieve type */
    info.type = (iodevice_t) (int) device_get_info_int_offline(device, DEVINFO_INT_IMAGE_TYPE);

    /* retrieve flags */
    info.readable = device_get_info_int_offline(device, DEVINFO_INT_IMAGE_READABLE) ? 1 : 0;
    info.writeable = device_get_info_int_offline(device, DEVINFO_INT_IMAGE_WRITEABLE) ? 1 : 0;
    info.creatable = device_get_info_int_offline(device, DEVINFO_INT_IMAGE_CREATABLE) ? 1 : 0;
    info.must_be_loaded = device_get_info_int_offline(device, DEVINFO_INT_IMAGE_MUST_BE_LOADED) ? 1 : 0;
    info.reset_on_load = device_get_info_int_offline(device, DEVINFO_INT_IMAGE_RESET_ON_LOAD) ? 1 : 0;
    info.has_partial_hash = device_get_info_fct_offline(device, DEVINFO_FCT_IMAGE_PARTIAL_HASH) ? 1 : 0;

    /* retrieve name */
    get_device_name(device, info.name, ARRAY_LENGTH(info.name));

    /* retrieve file extensions */
    get_device_file_extensions(device, info.file_extensions, ARRAY_LENGTH(info.file_extensions));

    /* retrieve instance name */
    get_device_instance_name(config, device, info.instance_name, ARRAY_LENGTH(info.instance_name),
        info.type, DEVINFO_STR_IMAGE_INSTANCE_NAME, device_typename);

    /* retrieve brief instance name */
    get_device_instance_name(config, device, info.brief_instance_name, ARRAY_LENGTH(info.brief_instance_name),
        info.type, DEVINFO_STR_IMAGE_BRIEF_INSTANCE_NAME, device_brieftypename);

    return info;
}



/*-------------------------------------------------
    image_device_uses_file_extension - checks to
    see if a particular devices uses a certain
    file extension
-------------------------------------------------*/

int image_device_uses_file_extension(const device_config *device, const char *file_extension)
{
    int result = FALSE;
    const char *s;
    char file_extension_list[256];

    /* skip initial period, if present */
    if (file_extension[0] == '.')
        file_extension++;

    /* retrieve file extension list */
    get_device_file_extensions(device, file_extension_list, ARRAY_LENGTH(file_extension_list));

    /* find the extensions */
    s = file_extension_list;
    while(!result && (*s != '\0'))
    {
        if (!mame_stricmp(s, file_extension))
        {
            result = TRUE;
            break;
        }
        s += strlen(s) + 1;
    }
    return result;
}



/*-------------------------------------------------
    image_device_compute_hash - compute a hash,
    using this device's partial hash if appropriate
-------------------------------------------------*/

void image_device_compute_hash(char *dest, const device_config *device,
    const void *data, size_t length, unsigned int functions)
{
    device_image_partialhash_func partialhash;

    /* retrieve the partial hash func */
    partialhash = (device_image_partialhash_func) device_get_info_fct_offline(device, DEVINFO_FCT_IMAGE_PARTIAL_HASH);

    /* compute the hash */
    if (partialhash != NULL)
        partialhash(dest, data, length, functions);
    else
        hash_compute(dest, data, length, functions);
}



/****************************************************************************
    CREATION FORMATS
****************************************************************************/

/*-------------------------------------------------
    image_device_get_creation_option_guide - accesses
    the creation option guide
-------------------------------------------------*/

const option_guide *image_device_get_creation_option_guide(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    return slot->create_option_guide;
}



/*-------------------------------------------------
    image_device_get_creatable_formats - accesses
    the image formats available for image creation
-------------------------------------------------*/

const image_device_format *image_device_get_creatable_formats(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    return slot->formatlist;
}



/*-------------------------------------------------
    image_device_get_indexed_creatable_format -
    accesses a specific image format available for
    image creation by index
-------------------------------------------------*/

const image_device_format *image_device_get_indexed_creatable_format(const device_config *device, int index)
{
    const image_device_format *format = image_device_get_creatable_formats(device);
    while(index-- && (format != NULL))
        format = format->next;
    return format;
}



/*-------------------------------------------------
    image_device_get_named_creatable_format -
    accesses a specific image format available for
    image creation by name
-------------------------------------------------*/

const image_device_format *image_device_get_named_creatable_format(const device_config *device, const char *format_name)
{
    const image_device_format *format = image_device_get_creatable_formats(device);
    while((format != NULL) && strcmp(format->name, format_name))
        format = format->next;
    return format;
}



/****************************************************************************
    IMAGE LOADING
****************************************************************************/

/*-------------------------------------------------
    set_image_filename - specifies the filename of
    an image
-------------------------------------------------*/

static image_error_t set_image_filename(image_slot_data *image, const char *filename)
{
    astring_cpyc(image->name, filename);
    zippath_parent(image->working_directory, filename);
    return IMAGE_ERROR_SUCCESS;
}



/*-------------------------------------------------
    is_loaded - quick check to determine whether an
    image is loaded
-------------------------------------------------*/

static int is_loaded(image_slot_data *image)
{
    return (image->file != NULL) || (image->ptr != NULL);
}



/*-------------------------------------------------
    load_image_by_path - loads an image with a
    specific path
-------------------------------------------------*/

static image_error_t load_image_by_path(image_slot_data *image, UINT32 open_flags, const char *path)
{
    file_error filerr = FILERR_NOT_FOUND;
    image_error_t err = IMAGE_ERROR_FILENOTFOUND;
    astring *revised_path = astring_alloc();

    /* attempt to read the file */
    filerr = zippath_fopen(path, open_flags, &image->file, revised_path);

    /* did the open succeed? */
    switch(filerr)
    {
        case FILERR_NONE:
            /* success! */
            image->writeable = (open_flags & OPEN_FLAG_WRITE) ? 1 : 0;
            image->created = (open_flags & OPEN_FLAG_CREATE) ? 1 : 0;
            err = IMAGE_ERROR_SUCCESS;
            break;

        case FILERR_NOT_FOUND:
        case FILERR_ACCESS_DENIED:
            /* file not found (or otherwise cannot open); continue */
            err = IMAGE_ERROR_FILENOTFOUND;
            break;

        case FILERR_OUT_OF_MEMORY:
            /* out of memory */
            err = IMAGE_ERROR_OUTOFMEMORY;
            break;

        case FILERR_ALREADY_OPEN:
            /* this shouldn't happen */
            err = IMAGE_ERROR_ALREADYOPEN;
            break;

        case FILERR_FAILURE:
        case FILERR_TOO_MANY_FILES:
        case FILERR_INVALID_DATA:
        default:
            /* other errors */
            err = IMAGE_ERROR_INTERNAL;
            break;
    }

    /* if successful, set the file name */
    if (filerr == FILERR_NONE)
        set_image_filename(image, astring_c(revised_path));

    astring_free(revised_path);
    return err;
}



/*-------------------------------------------------
    determine_open_plan - determines which open
    flags to use, and in what order
-------------------------------------------------*/

static void determine_open_plan(image_slot_data *image, int is_create, UINT32 *open_plan)
{
    int i = 0;

    /* emit flags */
    if (!is_create && image->info.readable && image->info.writeable)
        open_plan[i++] = OPEN_FLAG_READ | OPEN_FLAG_WRITE;
    if (!is_create && !image->info.readable && image->info.writeable)
        open_plan[i++] = OPEN_FLAG_WRITE;
    if (!is_create && image->info.readable)
        open_plan[i++] = OPEN_FLAG_READ;
    if (image->info.writeable && image->info.creatable)
        open_plan[i++] = OPEN_FLAG_READ | OPEN_FLAG_WRITE | OPEN_FLAG_CREATE;
    open_plan[i] = 0;
}



/*-------------------------------------------------
    find_image_slot - locates the slot for an
    image
-------------------------------------------------*/

static image_slot_data *find_image_slot(const device_config *image)
{
    int i;
    images_private *images_data = image->machine->images_data;

    for (i = 0; i < images_data->slot_count; i++)
    {
        if (images_data->slots[i].dev == image)
        {
            return &images_data->slots[i];
        }
    }
    return NULL;
}



/*-------------------------------------------------
    image_load_internal - core image loading
-------------------------------------------------*/

static int image_load_internal(const device_config *image, const char *path,
    int is_create, int create_format, option_resolution *create_args)
{
    running_machine *machine = image->machine;
    image_error_t err;
    const void *buffer;
    UINT32 open_plan[4];
    int i;
    image_slot_data *slot = find_image_slot(image);

    /* sanity checks */
    assert_always(image != NULL, "image_load(): image is NULL");
    assert_always(path != NULL, "image_load(): path is NULL");

    /* first unload the image */
    image_unload(image);

    /* clear any possible error messages */
    image_clear_error(slot);

    /* we are now loading */
    slot->is_loading = 1;

    /* record the filename */
    slot->err = set_image_filename(slot, path);
    if (slot->err)
        goto done;

    /* do we need to reset the CPU? */
    if ((attotime_compare(timer_get_time(machine), attotime_zero) > 0) && slot->info.reset_on_load)
        mame_schedule_hard_reset(machine);

    /* determine open plan */
    determine_open_plan(slot, is_create, open_plan);

    /* attempt to open the file in various ways */
    for (i = 0; !slot->file && open_plan[i]; i++)
    {
        /* open the file */
        slot->err = load_image_by_path(slot, open_plan[i], path);
        if (slot->err && (slot->err != IMAGE_ERROR_FILENOTFOUND))
            goto done;
    }

    /* did we fail to find the file? */
    if (!is_loaded(slot))
    {
        slot->err = IMAGE_ERROR_FILENOTFOUND;
        goto done;
    }

    /* if applicable, call device verify */
    if ((slot->verify != NULL) && !image_has_been_created(image))
    {
        /* access the memory */
        buffer = image_ptr(image);
        if (!buffer)
        {
            slot->err = IMAGE_ERROR_OUTOFMEMORY;
            goto done;
        }

        /* verify the file */
        err = (*slot->verify)(buffer, core_fsize(slot->file));
        if (err)
        {
            slot->err = IMAGE_ERROR_INVALIDIMAGE;
            goto done;
        }
    }

    /* call device load or create */
    if (image->token != NULL)
    {
        slot->create_format = create_format;
        slot->create_args = create_args;

        err = image_finish_load(image);
        if (err)
            goto done;
    }

    /* success! */

done:
    if (slot->err)
        image_clear(slot);
    return slot->err ? INIT_FAIL : INIT_PASS;
}



/*-------------------------------------------------
    image_load - load an image into MESS
-------------------------------------------------*/

int image_load(const device_config *image, const char *path)
{
    return image_load_internal(image, path, FALSE, 0, NULL);
}



/*-------------------------------------------------
    image_finish_load - special call - only use
    from core
-------------------------------------------------*/

int image_finish_load(const device_config *device)
{
    int err = INIT_PASS;

    image_slot_data *slot = find_image_slot(device);

    if (slot->is_loading)
    {
        if (image_has_been_created(device) && (slot->create != NULL))
        {
            err = (*slot->create)(device, slot->create_format, slot->create_args);
            if (err)
            {
                if (!slot->err)
                    slot->err = IMAGE_ERROR_UNSPECIFIED;
            }
        }
        else if (slot->load != NULL)
        {
            /* using device load */
            err = (*slot->load)(device);
            if (err)
            {
                if (!slot->err)
                    slot->err = IMAGE_ERROR_UNSPECIFIED;
            }
        }
    }

    slot->is_loading = 0;
    slot->create_format = 0;
    slot->create_args = NULL;
    return err;
}



/*-------------------------------------------------
    image_create - create a MESS image
-------------------------------------------------*/

int image_create(const device_config *image, const char *path, const image_device_format *create_format, option_resolution *create_args)
{
    int format_index = (create_format != NULL) ? create_format->index : 0;
    return image_load_internal(image, path, TRUE, format_index, create_args);
}



/*-------------------------------------------------
    image_clear - clear all internal data pertaining
    to an image
-------------------------------------------------*/

static void image_clear(image_slot_data *image)
{
    if (image->file)
    {
        core_fclose(image->file);
        image->file = NULL;
    }

    astring_cpyc(image->name, "");

    image->writeable = 0;
    image->created = 0;
    image->is_loading = 0;
    image->dir = NULL;
    image->hash = NULL;
    image->longname = NULL;
    image->manufacturer = NULL;
    image->year = NULL;
    image->playable = NULL;
    image->extrainfo = NULL;
    image->basename_noext = NULL;
    image->ptr = NULL;
}



/*-------------------------------------------------
    image_unload_internal - internal call to unload
    images
-------------------------------------------------*/

static void image_unload_internal(image_slot_data *slot)
{
    /* is there an actual image loaded? */
    if (is_loaded(slot))
    {
        /* call the unload function */
        if (slot->unload != NULL)
            slot->unload(slot->dev);
    }

    image_clear(slot);
    image_clear_error(slot);
}



/*-------------------------------------------------
    image_unload - main call to unload an image
-------------------------------------------------*/

void image_unload(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    image_unload_internal(slot);
}



/****************************************************************************
    ERROR HANDLING
****************************************************************************/

/*-------------------------------------------------
    image_clear_error - clear out any specified
    error
-------------------------------------------------*/

static void image_clear_error(image_slot_data *image)
{
    image->err = IMAGE_ERROR_SUCCESS;
    if (image->err_message != NULL)
    {
        image_freeptr(image->dev, image->err_message);
        image->err_message = NULL;
    }
}



/*-------------------------------------------------
    image_error - returns the error text for an image
    error
-------------------------------------------------*/

const char *image_error(const device_config *image)
{
    static const char *const messages[] =
    {
        NULL,
        "Internal error",
        "Unsupported operation",
        "Out of memory",
        "File not found",
        "Invalid image",
        "File already open",
        "Unspecified error"
    };

    image_slot_data *slot = find_image_slot(image);
    return slot->err_message ? slot->err_message : messages[slot->err];
}



/*-------------------------------------------------
    image_seterror - specifies an error on an image
-------------------------------------------------*/

void image_seterror(const device_config *image, image_error_t err, const char *message)
{
    image_slot_data *slot = find_image_slot(image);

    image_clear_error(slot);
    slot->err = err;
    if (message != NULL)
    {
        slot->err_message = image_strdup(image, message);
    }
}



/*-------------------------------------------------
    image_message - used to display a message while
    loading
-------------------------------------------------*/

void image_message(const device_config *device, const char *format, ...)
{
    image_slot_data *slot;
    va_list args;
    char buffer[256];

    /* sanity checks */
    slot = find_image_slot(device);
    assert(is_loaded(slot) || slot->is_loading);

    /* format the message */
    va_start(args, format);
    vsnprintf(buffer, ARRAY_LENGTH(buffer), format, args);
    va_end(args);

    /* display the popup for a standard amount of time */
    ui_popup_time(5, "%s: %s",
        image_basename(device),
        buffer);
}



/****************************************************************************
  Hash info loading

  If the hash is not checked and the relevant info not loaded, force that info
  to be loaded
****************************************************************************/

static int read_hash_config(const char *sysname, image_slot_data *image)
{
    hash_file *hashfile = NULL;
    const hash_info *info = NULL;

    /* open the hash file */
    hashfile = hashfile_open(sysname, FALSE, NULL);
    if (!hashfile)
        goto done;

    /* look up this entry in the hash file */
    info = hashfile_lookup(hashfile, image->hash);
    if (!info)
        goto done;

    /* copy the relevant entries */
    image->longname     = info->longname        ? image_strdup(image->dev, info->longname)      : NULL;
    image->manufacturer = info->manufacturer    ? image_strdup(image->dev, info->manufacturer)  : NULL;
    image->year         = info->year            ? image_strdup(image->dev, info->year)          : NULL;
    image->playable     = info->playable        ? image_strdup(image->dev, info->playable)      : NULL;
    image->pcb          = info->pcb             ? image_strdup(image->dev, info->pcb)           : NULL;
    image->extrainfo    = info->extrainfo       ? image_strdup(image->dev, info->extrainfo)     : NULL;

done:
    if (hashfile != NULL)
        hashfile_close(hashfile);
    return !hashfile || !info;
}



static void run_hash(const device_config *image,
    void (*partialhash)(char *, const unsigned char *, unsigned long, unsigned int),
    char *dest, unsigned int hash_functions)
{
    UINT32 size;
    UINT8 *buf = NULL;

    *dest = '\0';
    size = (UINT32) image_length(image);

    buf = alloc_array_or_die(UINT8, size);

    /* read the file */
    image_fseek(image, 0, SEEK_SET);
    image_fread(image, buf, size);

    if (partialhash)
        partialhash(dest, buf, size, hash_functions);
    else
        hash_compute(dest, buf, size, hash_functions);

    /* cleanup */
    free(buf);
    image_fseek(image, 0, SEEK_SET);
}



static int image_checkhash(image_slot_data *image)
{
    const game_driver *drv;
    char hash_string[HASH_BUF_SIZE];
    device_image_partialhash_func partialhash;
    int rc;

    /* this call should not be made when the image is not loaded */
    assert(is_loaded(image));

    /* only calculate CRC if it hasn't been calculated, and the open_mode is read only */
    if (!image->hash && !image->writeable && !image->created)
    {
        /* do not cause a linear read of 600 megs please */
        /* TODO: use SHA/MD5 in the CHD header as the hash */
        if (image->info.type == IO_CDROM)
            return FALSE;

        /* retrieve the partial hash func */
        partialhash = (device_image_partialhash_func) device_get_info_fct_offline(image->dev, DEVINFO_FCT_IMAGE_PARTIAL_HASH);

        run_hash(image->dev, partialhash, hash_string, HASH_CRC | HASH_MD5 | HASH_SHA1);

        image->hash = image_strdup(image->dev, hash_string);

        /* now read the hash file */
        drv = image->dev->machine->gamedrv;
        do
        {
            rc = read_hash_config(drv->name, image);
            drv = mess_next_compatible_driver(drv);
        }
        while(rc && (drv != NULL));
    }
    return TRUE;
}



/****************************************************************************
  Accessor functions

  These provide information about the device; and about the mounted image
****************************************************************************/

/*-------------------------------------------------
    image_exists
-------------------------------------------------*/

int image_exists(const device_config *image)
{
    return image_filename(image) != NULL;
}



/*-------------------------------------------------
    image_slotexists
-------------------------------------------------*/

int image_slotexists(const device_config *image)
{
    return TRUE;
}



/*-------------------------------------------------
    image_filename
-------------------------------------------------*/

const char *image_filename(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    const char *name = astring_c(slot->name);
    return (name[0] != '\0') ? name : NULL;
}



/*-------------------------------------------------
    image_basename
-------------------------------------------------*/

const char *image_basename(const device_config *image)
{
    return osd_basename((char *) image_filename(image));
}



/*-------------------------------------------------
    image_basename_noext
-------------------------------------------------*/

const char *image_basename_noext(const device_config *image)
{
    const char *s;
    char *ext;
    image_slot_data *slot = find_image_slot(image);

    if (!slot->basename_noext)
    {
        s = image_basename(image);
        if (s)
        {
            slot->basename_noext = image_strdup(image, s);
            ext = strrchr(slot->basename_noext, '.');
            if (ext)
                *ext = '\0';
        }
    }
    return slot->basename_noext;
}



/*-------------------------------------------------
    image_filetype
-------------------------------------------------*/

const char *image_filetype(const device_config *image)
{
    const char *s;
    s = image_filename(image);
    if (s != NULL)
        s = strrchr(s, '.');
    return s ? s+1 : NULL;
}



/*-------------------------------------------------
    image_filedir
-------------------------------------------------*/

const char *image_filedir(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    return slot->dir;
}



/*-------------------------------------------------
    image_core_file
-------------------------------------------------*/

core_file *image_core_file(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    return slot->file;
}



/*-------------------------------------------------
    image_typename_id
-------------------------------------------------*/

const char *image_typename_id(const device_config *image)
{
    static char buffer[64];
    get_device_name(image, buffer, ARRAY_LENGTH(buffer));
    return buffer;
}



/*-------------------------------------------------
    check_for_file
-------------------------------------------------*/

static void check_for_file(image_slot_data *image)
{
    assert_always(image->file != NULL, "Illegal operation on unmounted image");
}



/*-------------------------------------------------
    image_length
-------------------------------------------------*/

UINT64 image_length(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_fsize(slot->file);
}



/*-------------------------------------------------
    image_hash
-------------------------------------------------*/

const char *image_hash(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    image_checkhash(slot);
    return slot->hash;
}



/*-------------------------------------------------
    image_crc
-------------------------------------------------*/

UINT32 image_crc(const device_config *image)
{
    const char *hash_string;
    UINT32 crc = 0;

    hash_string = image_hash(image);
    if (hash_string != NULL)
        crc = hash_data_extract_crc32(hash_string);

    return crc;
}



/*-------------------------------------------------
    image_is_writable
-------------------------------------------------*/

int image_is_writable(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    return slot->writeable;
}



/*-------------------------------------------------
    image_has_been_created
-------------------------------------------------*/

int image_has_been_created(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    return slot->created;
}



/*-------------------------------------------------
    image_make_readonly
-------------------------------------------------*/

void image_make_readonly(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    slot->writeable = 0;
}



/*-------------------------------------------------
    image_fread
-------------------------------------------------*/

UINT32 image_fread(const device_config *image, void *buffer, UINT32 length)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_fread(slot->file, buffer, length);
}



/*-------------------------------------------------
    image_fwrite
-------------------------------------------------*/

UINT32 image_fwrite(const device_config *image, const void *buffer, UINT32 length)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_fwrite(slot->file, buffer, length);
}



/*-------------------------------------------------
    image_fseek
-------------------------------------------------*/

int image_fseek(const device_config *image, INT64 offset, int whence)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_fseek(slot->file, offset, whence);
}



/*-------------------------------------------------
    image_ftell
-------------------------------------------------*/

UINT64 image_ftell(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_ftell(slot->file);
}



/*-------------------------------------------------
    image_fgetc
-------------------------------------------------*/

int image_fgetc(const device_config *image)
{
    char ch;
    if (image_fread(image, &ch, 1) != 1)
        ch = '\0';
    return ch;
}



/*-------------------------------------------------
    image_fgets
-------------------------------------------------*/

char *image_fgets(const device_config *image, char *buffer, UINT32 length)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_fgets(buffer, length, slot->file);
}



/*-------------------------------------------------
    image_feof
-------------------------------------------------*/

int image_feof(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return core_feof(slot->file);
}



/*-------------------------------------------------
    image_ptr
-------------------------------------------------*/

void *image_ptr(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    check_for_file(slot);
    return (void *) core_fbuffer(slot->file);
}



/***************************************************************************
    WORKING DIRECTORIES
***************************************************************************/

/*-------------------------------------------------
    try_change_working_directory - tries to change
    the working directory, but only if the directory
    actually exists
-------------------------------------------------*/

static int try_change_working_directory(image_slot_data *image, const char *subdir)
{
    osd_directory *directory;
    const osd_directory_entry *entry;
    int success = FALSE;
    int done = FALSE;

    directory = osd_opendir(astring_c(image->working_directory));
    if (directory != NULL)
    {
        while(!done && (entry = osd_readdir(directory)) != NULL)
        {
            if (!mame_stricmp(subdir, entry->name))
            {
                done = TRUE;
                success = entry->type == ENTTYPE_DIR;
            }
        }

        osd_closedir(directory);
    }

    /* did we successfully identify the directory? */
    if (success)
        zippath_combine(image->working_directory, astring_c(image->working_directory), subdir);

    return success;
}



/*-------------------------------------------------
    setup_working_directory - sets up the working
    directory according to a few defaults
-------------------------------------------------*/

static void setup_working_directory(image_slot_data *image)
{
    char mess_directory[1024];
    const game_driver *gamedrv;

    /* first set up the working directory to be the MESS directory */
    osd_get_emulator_directory(mess_directory, ARRAY_LENGTH(mess_directory));
    astring_cpyc(image->working_directory, mess_directory);

    /* now try browsing down to "software" */
    if (try_change_working_directory(image, "software"))
    {
        /* now down to a directory for this computer */
        gamedrv = image->dev->machine->gamedrv;
        while(gamedrv && !try_change_working_directory(image, gamedrv->name))
        {
            gamedrv = mess_next_compatible_driver(gamedrv);
        }
    }
}



/*-------------------------------------------------
    image_working_directory - returns the working
    directory to use for this image; this is
    valid even if not mounted
-------------------------------------------------*/

const char *image_working_directory(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);

    /* check to see if we've never initialized the working directory */
    if (astring_len(slot->working_directory) == 0)
        setup_working_directory(slot);

    return astring_c(slot->working_directory);
}



/*-------------------------------------------------
    image_set_working_directory - sets the working
    directory to use for this image
-------------------------------------------------*/

void image_set_working_directory(const device_config *image, const char *working_directory)
{
    image_slot_data *slot = find_image_slot(image);
    astring_cpyc(slot->working_directory, (working_directory != NULL) ? working_directory : "");
}



/****************************************************************************
  Memory allocators

  These allow memory to be allocated for the lifetime of a mounted image.
  If these (and the above accessors) are used well enough, they should be
  able to eliminate the need for a unload function.
****************************************************************************/

void *image_malloc(const device_config *image, size_t size)
{
    return image_realloc(image, NULL, size);
}



void *image_realloc(const device_config *image, void *ptr, size_t size)
{
    image_slot_data *slot = find_image_slot(image);

    /* sanity checks */
    assert(is_loaded(slot) || slot->is_loading);

    return pool_realloc(slot->mempool, ptr, size);
}



char *image_strdup(const device_config *image, const char *src)
{
    image_slot_data *slot = find_image_slot(image);

    /* sanity checks */
    assert(is_loaded(slot) || slot->is_loading);

    return pool_strdup(slot->mempool, src);
}



void image_freeptr(const device_config *image, void *ptr)
{
    /* should really do something better here */
    image_realloc(image, ptr, 0);
}



/****************************************************************************
  CRC Accessor functions

  When an image is mounted; these functions provide access to the information
  pertaining to that image in the CRC database
****************************************************************************/

/*-------------------------------------------------
    image_longname
-------------------------------------------------*/

const char *image_longname(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->longname;
}



/*-------------------------------------------------
    image_manufacturer
-------------------------------------------------*/

const char *image_manufacturer(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->manufacturer;
}



/*-------------------------------------------------
    image_year
-------------------------------------------------*/

const char *image_year(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->year;
}



/*-------------------------------------------------
    image_playable
-------------------------------------------------*/

const char *image_playable(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->playable;
}



/*-------------------------------------------------
    image_pcb
-------------------------------------------------*/

const char *image_pcb(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->pcb;
}



/*-------------------------------------------------
    image_extrainfo
-------------------------------------------------*/

const char *image_extrainfo(const device_config *device)
{
    image_slot_data *slot = find_image_slot(device);
    image_checkhash(slot);
    return slot->extrainfo;
}



/****************************************************************************
  Battery functions

  These functions provide transparent access to battery-backed RAM on an
  image; typically for cartridges.
****************************************************************************/

/*-------------------------------------------------
    open_battery_file_by_name - opens the battery backed
    NVRAM file for an image
-------------------------------------------------*/

static file_error open_battery_file_by_name(const char *filename, UINT32 openflags, mame_file **file)
{
    file_error filerr;
    filerr = mame_fopen(SEARCHPATH_NVRAM, filename, openflags, file);
    return filerr;
}

/*-------------------------------------------------
    image_battery_load_by_name - retrieves the battery
    backed RAM for an image. A filename may be supplied
    to the function.
-------------------------------------------------*/

void image_battery_load_by_name(const char *filename, void *buffer, int length)
{
    file_error filerr;
    mame_file *file;
    int bytes_read = 0;

    assert_always(buffer && (length > 0), "Must specify sensical buffer/length");

    /* try to open the battery file and read it in, if possible */
    filerr = open_battery_file_by_name(filename, OPEN_FLAG_READ, &file);
    if (filerr == FILERR_NONE)
    {
        bytes_read = mame_fread(file, buffer, length);
        mame_fclose(file);
    }

    /* fill remaining bytes (if necessary) */
    memset(((char *) buffer) + bytes_read, '\0', length - bytes_read);
}


/*-------------------------------------------------
    image_battery_load - retrieves the battery
    backed RAM for an image. The file name is
    created from the machine driver name and the
    image name.
-------------------------------------------------*/

void image_battery_load(const device_config *image, void *buffer, int length)
{
    char *basename_noext;
    astring *fname;

    basename_noext = strip_extension(image_basename(image));
    if (!basename_noext)
        fatalerror("FILERR_OUT_OF_MEMORY");

    fname = astring_assemble_4(astring_alloc(), image->machine->gamedrv->name, PATH_SEPARATOR, basename_noext, ".nv");

    image_battery_load_by_name(astring_c(fname), buffer, length);
    astring_free(fname);
    free(basename_noext);
}


/*-------------------------------------------------
    image_battery_save_by_name - stores the battery
    backed RAM for an image. A filename may be supplied
    to the function.
-------------------------------------------------*/
void image_battery_save_by_name(const char *filename, const void *buffer, int length)
{
    file_error filerr;
    mame_file *file;

    assert_always(buffer && (length > 0), "Must specify sensical buffer/length");

    /* try to open the battery file and write it out, if possible */
    filerr = open_battery_file_by_name(filename, OPEN_FLAG_WRITE | OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_PATHS, &file);
    if (filerr == FILERR_NONE)
    {
        mame_fwrite(file, buffer, length);
        mame_fclose(file);
    }
}

/*-------------------------------------------------
    image_battery_save - stores the battery
    backed RAM for an image. The file name is
    created from the machine driver name and the
    image name.
-------------------------------------------------*/

void image_battery_save(const device_config *image, const void *buffer, int length)
{
    astring *fname;
    char *basename_noext;

    basename_noext = strip_extension(image_basename(image));
    if (!basename_noext)
        fatalerror("FILERR_OUT_OF_MEMORY");

    fname = astring_assemble_4(astring_alloc(), image->machine->gamedrv->name, PATH_SEPARATOR, basename_noext, ".nv");

    image_battery_save_by_name(astring_c(fname), buffer, length);
    astring_free(fname);
    free(basename_noext);
}



/****************************************************************************
  Indexing functions

  These provide various ways of indexing images
****************************************************************************/

int image_absolute_index(const device_config *image)
{
    image_slot_data *slot = find_image_slot(image);
    return slot - image->machine->images_data->slots;
}



const device_config *image_from_absolute_index(running_machine *machine, int absolute_index)
{
    return machine->images_data->slots[absolute_index].dev;
}
