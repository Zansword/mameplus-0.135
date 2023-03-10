/***************************************************************************

    sound.c

    Core sound functions and definitions.

    Copyright Nicola Salmoria and the MAME Team.
    Visit http://mamedev.org for licensing and usage restrictions.

***************************************************************************/

#include "driver.h"
#include "osdepend.h"
#include "streams.h"
#include "config.h"
#include "profiler.h"
#include "sound/wavwrite.h"



/***************************************************************************
    DEBUGGING
***************************************************************************/

#define VERBOSE			(0)

#define VPRINTF(x)		do { if (VERBOSE) mame_printf_debug x; } while (0)



/***************************************************************************
    CONSTANTS
***************************************************************************/

#define MAX_MIXER_CHANNELS		100

#ifdef USE_VOLUME_AUTO_ADJUST
#define VOLUME_MULTIPLIER_FRAC_ONE	256
#define DEFAULT_VOLUME_MULTIPLIER	VOLUME_MULTIPLIER_FRAC_ONE
#define DEFAULT_VOLUME_MULTIPLIER_MAX	(10 * VOLUME_MULTIPLIER_FRAC_ONE)
#endif /* USE_VOLUME_AUTO_ADJUST */


/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

typedef struct _sound_output sound_output;
struct _sound_output
{
	sound_stream *	stream;					/* associated stream */
	int				output;					/* output number */
};


typedef struct _sound_class_data sound_class_data;
struct _sound_class_data
{
	int				outputs;				/* number of outputs from this instance */
	sound_output	output[MAX_OUTPUTS];	/* array of output information */
};


typedef struct _speaker_input speaker_input;
struct _speaker_input
{
	float			gain;					/* current gain */
	float			default_gain;			/* default gain */
	char *			name;					/* name of this input */
};


typedef struct _speaker_info speaker_info;
struct _speaker_info
{
	const speaker_config *speaker;			/* pointer to the speaker info */
	const char *	tag;					/* speaker tag */
	sound_stream *	mixer_stream;			/* mixing stream */
	int				inputs;					/* number of input streams */
	speaker_input *	input;					/* array of input information */
#ifdef MAME_DEBUG
	INT32			max_sample;				/* largest sample value we've seen */
	INT32			clipped_samples;		/* total number of clipped samples */
	INT32			total_samples;			/* total number of samples */
#endif
};

struct _sound_private
{
	emu_timer *update_timer;

	int totalsnd;

	UINT32 finalmix_leftover;
	INT16 *finalmix;
	INT32 *leftmix;
	INT32 *rightmix;

	int muted;
	int attenuation;
	int enabled;
	int nosound_mode;

	wav_file *wavfile;
};

#ifdef USE_VOLUME_AUTO_ADJUST
static INT32 volume_multiplier_final = DEFAULT_VOLUME_MULTIPLIER;
static INT32 volume_multiplier_final_max = DEFAULT_VOLUME_MULTIPLIER_MAX;
static INT32 volume_multiplier_mixer = DEFAULT_VOLUME_MULTIPLIER;
static INT32 volume_multiplier_mixer_max = DEFAULT_VOLUME_MULTIPLIER_MAX;
static int have_sample = 0;
#endif /* USE_VOLUME_AUTO_ADJUST */


/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static void sound_reset(running_machine *machine);
static void sound_exit(running_machine *machine);
static void sound_pause(running_machine *machine, int pause);
static void sound_load(running_machine *machine, int config_type, xml_data_node *parentnode);
static void sound_save(running_machine *machine, int config_type, xml_data_node *parentnode);
static TIMER_CALLBACK( sound_update );
static void route_sound(running_machine *machine);
static STREAM_UPDATE( mixer_update );
static STATE_POSTLOAD( mixer_postload );

#ifdef USE_VOLUME_AUTO_ADJUST
INLINE INT16 calc_volume_final(INT32 sample);
INLINE INT16 calc_volume_mixer(INT32 sample);
#endif /* USE_VOLUME_AUTO_ADJUST */


/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    get_class_data - return a pointer to the
    class data
-------------------------------------------------*/

INLINE sound_class_data *get_class_data(const device_config *device)
{
	assert(device != NULL);
	assert(device->type == SOUND);
	assert(device->devclass == DEVICE_CLASS_SOUND_CHIP);
	assert(device->token != NULL);
	return (sound_class_data *)((UINT8 *)device->token + device->tokenbytes) - 1;
}


/*-------------------------------------------------
    get_safe_token - makes sure that the passed
    in device is, in fact, a timer
-------------------------------------------------*/

INLINE speaker_info *get_safe_token(const device_config *device)
{
	assert(device != NULL);
	assert(device->token != NULL);
	assert(device->type == SPEAKER_OUTPUT);

	return (speaker_info *)device->token;
}


/*-------------------------------------------------
    index_to_input - map an absolute index to
    a particular input
-------------------------------------------------*/

INLINE speaker_info *index_to_input(running_machine *machine, int index, int *input)
{
	const device_config *curspeak;
	int count = 0;

	/* scan through the speakers until we find the indexed input */
	for (curspeak = speaker_output_first(machine->config); curspeak != NULL; curspeak = speaker_output_next(curspeak))
	{
		speaker_info *info = (speaker_info *)curspeak->token;
		if (index < count + info->inputs)
		{
			*input = index - count;
			return info;
		}
		count += info->inputs;
	}

	/* index out of range */
	return NULL;
}



/***************************************************************************
    INITIALIZATION
***************************************************************************/

/*-------------------------------------------------
    sound_init - start up the sound system
-------------------------------------------------*/

void sound_init(running_machine *machine)
{
	sound_private *global;
	const char *filename;

	machine->sound_data = global = auto_alloc_clear(machine, sound_private);

	/* handle -nosound */
	global->nosound_mode = !options_get_bool(mame_options(), OPTION_SOUND);
	if (global->nosound_mode)
		machine->sample_rate = 11025;

	/* count the speakers */
	VPRINTF(("total speakers = %d\n", speaker_output_count(machine->config)));

	/* allocate memory for mix buffers */
	global->leftmix = auto_alloc_array(machine, INT32, machine->sample_rate);
	global->rightmix = auto_alloc_array(machine, INT32, machine->sample_rate);
	global->finalmix = auto_alloc_array(machine, INT16, machine->sample_rate);

	/* allocate a global timer for sound timing */
	global->update_timer = timer_alloc(machine, sound_update, NULL);
	timer_adjust_periodic(global->update_timer, STREAMS_UPDATE_ATTOTIME, 0, STREAMS_UPDATE_ATTOTIME);

	/* finally, do all the routing */
	VPRINTF(("route_sound\n"));
	route_sound(machine);

	/* open the output WAV file if specified */
	filename = options_get_string(mame_options(), OPTION_WAVWRITE);
	if (filename[0] != 0)
		global->wavfile = wav_open(filename, machine->sample_rate, 2);

	/* enable sound by default */
	global->enabled = TRUE;
	global->muted = FALSE;
	sound_set_attenuation(machine, options_get_int(mame_options(), OPTION_VOLUME));

	/* register callbacks */
	config_register(machine, "mixer", sound_load, sound_save);
	add_pause_callback(machine, sound_pause);
	add_reset_callback(machine, sound_reset);
	add_exit_callback(machine, sound_exit);
}


/*-------------------------------------------------
    sound_exit - clean up after ourselves
-------------------------------------------------*/

static void sound_exit(running_machine *machine)
{
	sound_private *global = machine->sound_data;

	/* close any open WAV file */
	if (global->wavfile != NULL)
		wav_close(global->wavfile);
	global->wavfile = NULL;

	/* reset variables */
	global->totalsnd = 0;
}



/***************************************************************************
    SOUND DEVICE INTERFACE
***************************************************************************/

/*-------------------------------------------------
    device_start_sound - device start callback
-------------------------------------------------*/

static DEVICE_START( sound )
{
	sound_class_data *classdata;
	const sound_config *config;
	deviceinfo devinfo;
	int num_regs, outputnum;

	/* validate some basic stuff */
	assert(device != NULL);
	assert(device->inline_config != NULL);
	assert(device->machine != NULL);
	assert(device->machine->config != NULL);

	/* get pointers to our data */
	config = (const sound_config *)device->inline_config;
	classdata = get_class_data(device);

	/* get the chip's start function */
	devinfo.start = NULL;
	(*config->type)(device, DEVINFO_FCT_START, &devinfo);
	assert(devinfo.start != NULL);

	/* initialize this sound chip */
	num_regs = state_save_get_reg_count(device->machine);
	(*devinfo.start)(device);
	num_regs = state_save_get_reg_count(device->machine) - num_regs;

	/* now count the outputs */
	VPRINTF(("Counting outputs\n"));
	for (outputnum = 0; outputnum < MAX_OUTPUTS; outputnum++)
	{
		sound_stream *stream = stream_find_by_device(device, outputnum);
		int curoutput, numoutputs;

		/* stop when we run out of streams */
		if (stream == NULL)
			break;

		/* accumulate the number of outputs from this stream */
		numoutputs = stream_get_outputs(stream);
		assert(classdata->outputs < MAX_OUTPUTS);

		/* fill in the array */
		for (curoutput = 0; curoutput < numoutputs; curoutput++)
		{
			sound_output *output = &classdata->output[classdata->outputs++];
			output->stream = stream;
			output->output = curoutput;
		}
	}

	/* if no state registered for saving, we can't save */
	if (num_regs == 0)
	{
		logerror("Sound chip '%s' did not register any state to save!\n", device->tag);
		if (device->machine->gamedrv->flags & GAME_SUPPORTS_SAVE)
			fatalerror("Sound chip '%s' did not register any state to save!", device->tag);
	}
}


/*-------------------------------------------------
    device_custom_config_sound - custom inline
    config callback for populating sound routes
-------------------------------------------------*/

static DEVICE_CUSTOM_CONFIG( sound )
{
	sound_config *config = (sound_config *)device->inline_config;

	switch (entrytype)
	{
		/* custom config 1 is a new route */
		case MCONFIG_TOKEN_DEVICE_CONFIG_CUSTOM_1:
		{
			sound_route **routeptr;
			int output, input;
			UINT32 gain;

			/* put back the token that was originally fetched so we can grab a packed 64-bit token */
			TOKEN_UNGET_UINT32(tokens);
			TOKEN_GET_UINT64_UNPACK4(tokens, entrytype, 8, output, 12, input, 12, gain, 32);

			/* allocate a new route */
			for (routeptr = &config->routelist; *routeptr != NULL; routeptr = &(*routeptr)->next) ;
			*routeptr = alloc_or_die(sound_route);
			(*routeptr)->next = NULL;
			(*routeptr)->output = output;
			(*routeptr)->input = input;
			(*routeptr)->gain = (float)gain * (1.0f / (float)(1 << 24));
			(*routeptr)->target = TOKEN_GET_STRING(tokens);
			break;
		}

		/* custom config free is also used as a reset of sound routes */
		case MCONFIG_TOKEN_DEVICE_CONFIG_CUSTOM_FREE:
			while (config->routelist != NULL)
			{
				sound_route *temp = config->routelist;
				config->routelist = temp->next;
				free(temp);
			}
			break;
	}

	return tokens;
}


/*-------------------------------------------------
    device_get_info_sound - device get info
    callback
-------------------------------------------------*/

DEVICE_GET_INFO( sound )
{
	const sound_config *config = (device != NULL) ? (const sound_config *)device->inline_config : NULL;

	switch (state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case DEVINFO_INT_TOKEN_BYTES:
			(*config->type)(device, DEVINFO_INT_TOKEN_BYTES, info);
			info->i += sizeof(sound_class_data);
			break;

		case DEVINFO_INT_INLINE_CONFIG_BYTES:	info->i = sizeof(sound_config);				break;
		case DEVINFO_INT_CLASS:					info->i = DEVICE_CLASS_SOUND_CHIP;			break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case DEVINFO_FCT_START:					info->start = DEVICE_START_NAME(sound); 	break;
		case DEVINFO_FCT_CUSTOM_CONFIG:			info->custom_config = DEVICE_CUSTOM_CONFIG_NAME(sound);	break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case DEVINFO_STR_NAME:
			if (config != NULL)
				(*config->type)(device, state, info);
			else
				strcpy(info->s, "sound");
			break;

		default:
			(*config->type)(device, state, info);
			break;
	}
}



/***************************************************************************
    INITIALIZATION HELPERS
***************************************************************************/

/*-------------------------------------------------
    route_sound - route sound outputs to target
    inputs
-------------------------------------------------*/

static void route_sound(running_machine *machine)
{
	astring *tempstring = astring_alloc();
	const device_config *curspeak;
	const device_config *sound;
	int outputnum;

	/* first count up the inputs for each speaker */
	for (sound = sound_first(machine->config); sound != NULL; sound = sound_next(sound))
	{
		const sound_config *config = (const sound_config *)sound->inline_config;
		int numoutputs = stream_get_device_outputs(sound);
		const sound_route *route;

		/* iterate over all routes */
		for (route = config->routelist; route != NULL; route = route->next)
		{
			const device_config *target_device = devtag_get_device(machine, route->target);

			/* if neither found, it's fatal */
			if (target_device == NULL)
				fatalerror("Sound route \"%s\" not found!\n", route->target);

			/* if we got a speaker, bump its input count */
			if (target_device->type == SPEAKER_OUTPUT)
				get_safe_token(target_device)->inputs += (route->output == ALL_OUTPUTS) ? numoutputs : 1;
		}
	}

	/* now allocate the mixers and input data */
	for (curspeak = speaker_output_first(machine->config); curspeak != NULL; curspeak = speaker_output_next(curspeak))
	{
		speaker_info *info = get_safe_token(curspeak);
		if (info->inputs != 0)
		{
			info->mixer_stream = stream_create(curspeak, info->inputs, 1, machine->sample_rate, info, mixer_update);
			state_save_register_postload(machine, mixer_postload, info->mixer_stream);
			info->input = auto_alloc_array(machine, speaker_input, info->inputs);
			info->inputs = 0;
		}
		else
			logerror("Warning: speaker \"%s\" has no inputs\n", info->tag);
	}

	/* iterate again over all the sound chips */
	for (sound = sound_first(machine->config); sound != NULL; sound = sound_next(sound))
	{
		const sound_config *config = (const sound_config *)sound->inline_config;
		int numoutputs = stream_get_device_outputs(sound);
		const sound_route *route;

		/* iterate over all routes */
		for (route = config->routelist; route != NULL; route = route->next)
		{
			const device_config *target_device = devtag_get_device(machine, route->target);
			int inputnum = route->input;
			sound_stream *stream;
			int streamoutput;

			/* iterate over all outputs, matching any that apply */
			for (outputnum = 0; outputnum < numoutputs; outputnum++)
				if (route->output == outputnum || route->output == ALL_OUTPUTS)
				{
					/* if it's a speaker, set the input */
					if (target_device->type == SPEAKER_OUTPUT)
					{
						speaker_info *speakerinfo = get_safe_token(target_device);

						/* generate text for the UI */
						astring_printf(tempstring, _("Speaker '%s': %s '%s'"), target_device->tag, device_get_name(sound), sound->tag);
						if (numoutputs > 1)
							astring_catprintf(tempstring, _(" Ch.%d"), outputnum);

						/* fill in the input data on this speaker */
						speakerinfo->input[speakerinfo->inputs].gain = route->gain;
						speakerinfo->input[speakerinfo->inputs].default_gain = route->gain;
						speakerinfo->input[speakerinfo->inputs].name = auto_strdup(machine, astring_c(tempstring));

						/* connect the output to the input */
						if (stream_device_output_to_stream_output(sound, outputnum, &stream, &streamoutput))
							stream_set_input(speakerinfo->mixer_stream, speakerinfo->inputs++, stream, streamoutput, route->gain);
					}

					/* otherwise, it's a sound chip */
					else
					{
						sound_stream *inputstream;
						int streaminput;

						if (stream_device_input_to_stream_input(target_device, inputnum++, &inputstream, &streaminput))
							if (stream_device_output_to_stream_output(sound, outputnum, &stream, &streamoutput))
								stream_set_input(inputstream, streaminput, stream, streamoutput, route->gain);
					}
				}
		}
	}

	/* free up our temporary string */
	astring_free(tempstring);
}



/***************************************************************************
    GLOBAL STATE MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    sound_reset - reset all sound chips
-------------------------------------------------*/

static void sound_reset(running_machine *machine)
{
	const device_config *sound;

	/* reset all the sound chips */
	for (sound = sound_first(machine->config); sound != NULL; sound = sound_next(sound))
		device_reset(sound);
}


/*-------------------------------------------------
    sound_pause - pause sound output
-------------------------------------------------*/

static void sound_pause(running_machine *machine, int pause)
{
	sound_private *global = machine->sound_data;

	if (pause)
		global->muted |= 0x02;
	else
		global->muted &= ~0x02;
	osd_set_mastervolume(global->muted ? -32 : global->attenuation);
}


/*-------------------------------------------------
    sound_mute - mute sound output
-------------------------------------------------*/

void sound_mute(running_machine *machine, int mute)
{
	sound_private *global = machine->sound_data;

	if (mute)
		global->muted |= 0x01;
	else
		global->muted &= ~0x01;
	osd_set_mastervolume(global->muted ? -32 : global->attenuation);
}


/*-------------------------------------------------
    sound_set_attenuation - set the global volume
-------------------------------------------------*/

void sound_set_attenuation(running_machine *machine, int attenuation)
{
	sound_private *global = machine->sound_data;
	global->attenuation = attenuation;
	osd_set_mastervolume(global->muted ? -32 : global->attenuation);
}


/*-------------------------------------------------
    sound_get_attenuation - return the global
    volume
-------------------------------------------------*/

int sound_get_attenuation(running_machine *machine)
{
	sound_private *global = machine->sound_data;
	return global->attenuation;
}


/*-------------------------------------------------
    sound_global_enable - enable/disable sound
    globally
-------------------------------------------------*/

void sound_global_enable(running_machine *machine, int enable)
{
	sound_private *global = machine->sound_data;
	global->enabled = enable;
}



/***************************************************************************
    SOUND SAVE/LOAD
***************************************************************************/

/*-------------------------------------------------
    sound_load - read and apply data from the
    configuration file
-------------------------------------------------*/

static void sound_load(running_machine *machine, int config_type, xml_data_node *parentnode)
{
	xml_data_node *channelnode;
	int mixernum;

	/* we only care about game files */
	if (config_type != CONFIG_TYPE_GAME)
		return;

	/* might not have any data */
	if (parentnode == NULL)
		return;

	/* iterate over channel nodes */
	for (channelnode = xml_get_sibling(parentnode->child, "channel"); channelnode; channelnode = xml_get_sibling(channelnode->next, "channel"))
	{
		mixernum = xml_get_attribute_int(channelnode, "index", -1);
		if (mixernum >= 0 && mixernum < MAX_MIXER_CHANNELS)
		{
			float defvol = xml_get_attribute_float(channelnode, "defvol", -1000.0);
			float newvol = xml_get_attribute_float(channelnode, "newvol", -1000.0);
			if (fabs(defvol - sound_get_default_gain(machine, mixernum)) < 1e-6 && newvol != -1000.0)
				sound_set_user_gain(machine, mixernum, newvol);
		}
	}

#ifdef USE_VOLUME_AUTO_ADJUST
	channelnode = xml_get_sibling(parentnode->child, "volume_multiplier");
	if (channelnode)
	{
		volume_multiplier_final = xml_get_attribute_int(channelnode, "final", DEFAULT_VOLUME_MULTIPLIER);
		volume_multiplier_final_max = xml_get_attribute_int(channelnode, "final_max", DEFAULT_VOLUME_MULTIPLIER_MAX);
		volume_multiplier_mixer = xml_get_attribute_int(channelnode, "mixer", DEFAULT_VOLUME_MULTIPLIER);
		volume_multiplier_mixer_max = xml_get_attribute_int(channelnode, "mixer_max", DEFAULT_VOLUME_MULTIPLIER_MAX);
	}
#endif /* USE_VOLUME_AUTO_ADJUST */
}


/*-------------------------------------------------
    sound_save - save data to the configuration
    file
-------------------------------------------------*/

static void sound_save(running_machine *machine, int config_type, xml_data_node *parentnode)
{
	int mixernum;

	/* we only care about game files */
	if (config_type != CONFIG_TYPE_GAME)
		return;

	/* iterate over mixer channels */
	if (parentnode != NULL)
		for (mixernum = 0; mixernum < MAX_MIXER_CHANNELS; mixernum++)
		{
			float defvol = sound_get_default_gain(machine, mixernum);
			float newvol = sound_get_user_gain(machine, mixernum);

			if (defvol != newvol)
			{
				xml_data_node *channelnode = xml_add_child(parentnode, "channel", NULL);
				if (channelnode != NULL)
				{
					xml_set_attribute_int(channelnode, "index", mixernum);
					xml_set_attribute_float(channelnode, "defvol", defvol);
					xml_set_attribute_float(channelnode, "newvol", newvol);
				}
			}
		}

#ifdef USE_VOLUME_AUTO_ADJUST
	if (parentnode)
	{
		xml_data_node *channelnode = xml_add_child(parentnode, "volume_multiplier", NULL);
		if (channelnode)
		{
			xml_set_attribute_int(channelnode, "final", volume_multiplier_final);
			xml_set_attribute_int(channelnode, "final_max", volume_multiplier_final_max);
			xml_set_attribute_int(channelnode, "mixer", volume_multiplier_mixer);
			xml_set_attribute_int(channelnode, "mixer_max", volume_multiplier_mixer_max);
		}
	}
#endif /* USE_VOLUME_AUTO_ADJUST */
}



/***************************************************************************
    MIXING STAGE
***************************************************************************/

/*-------------------------------------------------
    sound_update - mix everything down to
    its final form and send it to the OSD layer
-------------------------------------------------*/

static TIMER_CALLBACK( sound_update )
{
	UINT32 finalmix_step, finalmix_offset;
	const device_config *curspeak;
	int samples_this_update = 0;
	int sample;
	sound_private *global = machine->sound_data;
	INT16 *finalmix;
	INT32 *leftmix, *rightmix;

	VPRINTF(("sound_update\n"));

	profiler_mark_start(PROFILER_SOUND);

	leftmix = global->leftmix;
	rightmix = global->rightmix;
	finalmix = global->finalmix;

	/* force all the speaker streams to generate the proper number of samples */
	for (curspeak = speaker_output_first(machine->config); curspeak != NULL; curspeak = speaker_output_next(curspeak))
	{
		speaker_info *spk = (speaker_info *)curspeak->token;
		const stream_sample_t *stream_buf;

		/* get the output buffer */
		if (spk->mixer_stream != NULL)
		{
			int numsamples;

			/* update the stream, getting the start/end pointers around the operation */
			stream_buf = stream_get_output_since_last_update(spk->mixer_stream, 0, &numsamples);

			/* set or assert that all streams have the same count */
			if (samples_this_update == 0)
			{
				samples_this_update = numsamples;

				/* reset the mixing streams */
				memset(leftmix, 0, samples_this_update * sizeof(*leftmix));
				memset(rightmix, 0, samples_this_update * sizeof(*rightmix));
			}
			assert(samples_this_update == numsamples);

#ifdef MAME_DEBUG
			/* debug version: keep track of the maximum sample */
			for (sample = 0; sample < samples_this_update; sample++)
			{
				if (stream_buf[sample] > spk->max_sample)
					spk->max_sample = stream_buf[sample];
				else if (-stream_buf[sample] > spk->max_sample)
					spk->max_sample = -stream_buf[sample];
				if (stream_buf[sample] > 32767 || stream_buf[sample] < -32768)
					spk->clipped_samples++;
				spk->total_samples++;
			}
#endif

			/* mix if sound is enabled */
			if (global->enabled && !global->nosound_mode)
			{
				/* if the speaker is centered, send to both left and right */
				if (spk->speaker->x == 0)
					for (sample = 0; sample < samples_this_update; sample++)
					{
						leftmix[sample] += stream_buf[sample];
						rightmix[sample] += stream_buf[sample];
					}

				/* if the speaker is to the left, send only to the left */
				else if (spk->speaker->x < 0)
					for (sample = 0; sample < samples_this_update; sample++)
						leftmix[sample] += stream_buf[sample];

				/* if the speaker is to the right, send only to the right */
				else
					for (sample = 0; sample < samples_this_update; sample++)
						rightmix[sample] += stream_buf[sample];
			}
		}
	}

	/* now downmix the final result */
	finalmix_step = video_get_speed_factor();
	finalmix_offset = 0;

#ifdef USE_VOLUME_AUTO_ADJUST
	if (options_get_bool(mame_options(), OPTION_VOLUME_ADJUST))
	{
		have_sample = 0;

		for (sample = global->finalmix_leftover; sample < samples_this_update * 100; sample += finalmix_step)
		{
			int sampindex = sample / 100;

			/* clamp the left side */
			finalmix[finalmix_offset++] = calc_volume_final(leftmix[sampindex]);
	
			/* clamp the right side */
			finalmix[finalmix_offset++] = calc_volume_final(rightmix[sampindex]);
		}

		if (have_sample)
		{
			if (volume_multiplier_final_max > volume_multiplier_final)
				volume_multiplier_final++;
		}
	}
	else
#endif /* USE_VOLUME_AUTO_ADJUST */
	for (sample = global->finalmix_leftover; sample < samples_this_update * 100; sample += finalmix_step)
	{
		int sampindex = sample / 100;
		INT32 samp;

		/* clamp the left side */
		samp = leftmix[sampindex];
		if (samp < -32768)
			samp = -32768;
		else if (samp > 32767)
			samp = 32767;
		finalmix[finalmix_offset++] = samp;

		/* clamp the right side */
		samp = rightmix[sampindex];
		if (samp < -32768)
			samp = -32768;
		else if (samp > 32767)
			samp = 32767;
		finalmix[finalmix_offset++] = samp;
	}
	global->finalmix_leftover = sample - samples_this_update * 100;

	/* play the result */
	if (finalmix_offset > 0)
	{
		osd_update_audio_stream(machine, finalmix, finalmix_offset / 2);
		video_avi_add_sound(machine, finalmix, finalmix_offset / 2);
		if (global->wavfile != NULL && !mame_is_paused(machine))
			wav_add_data_16(global->wavfile, finalmix, finalmix_offset);
	}

	/* update the streamer */
	streams_update(machine);

	profiler_mark_end();
}

#ifdef USE_VOLUME_AUTO_ADJUST
/*************************************
 *
 *	Adjust sample by volume multiplier
 *
 *************************************/

INLINE INT16 calc_volume_final(INT32 sample)
{
	if (sample)
	{
		INT32 temp;

		have_sample = 1;

		if (sample > 0)
			temp = (32767.0 * VOLUME_MULTIPLIER_FRAC_ONE) / (double)sample;
		else
			temp = (-32768.0 * VOLUME_MULTIPLIER_FRAC_ONE) / (double)sample;

		if (volume_multiplier_final_max > temp)
		{
			volume_multiplier_final_max = temp;

			if (volume_multiplier_final_max < volume_multiplier_final)
				volume_multiplier_final = volume_multiplier_final_max;
		}
	}

	return sample * volume_multiplier_final / VOLUME_MULTIPLIER_FRAC_ONE;
}

INLINE INT16 calc_volume_mixer(INT32 sample)
{
	if (sample)
	{
		INT32 temp;

		have_sample = 1;

		if (sample > 0)
			temp = (32767.0 * VOLUME_MULTIPLIER_FRAC_ONE) / (double)sample;
		else
			temp = (-32768.0 * VOLUME_MULTIPLIER_FRAC_ONE) / (double)sample;

		if (volume_multiplier_mixer_max > temp)
		{
			volume_multiplier_mixer_max = temp;

			if (volume_multiplier_mixer_max < volume_multiplier_mixer)
				volume_multiplier_mixer = volume_multiplier_mixer_max;
		}
	}

	return sample * volume_multiplier_mixer / VOLUME_MULTIPLIER_FRAC_ONE;
}
#endif /* USE_VOLUME_AUTO_ADJUST */


/*-------------------------------------------------
    mixer_update - mix all inputs to one output
-------------------------------------------------*/

static STREAM_UPDATE( mixer_update )
{
	speaker_info *speaker = (speaker_info *)param;
	int numinputs = speaker->inputs;
	int pos;

	VPRINTF(("Mixer_update(%d)\n", samples));

	/* loop over samples */
#ifdef USE_VOLUME_AUTO_ADJUST
	if (options_get_bool(mame_options(), OPTION_VOLUME_ADJUST))
	{
		have_sample = 0;

		for (pos = 0; pos < samples; pos++)
		{
			INT32 sample = inputs[0][pos];
			int inp;

			/* add up all the inputs */
			for (inp = 1; inp < numinputs; inp++)
				sample += inputs[inp][pos];

			/* clamp and store */
			outputs[0][pos] = calc_volume_mixer(sample);
		}

		if (have_sample)
		{
			if (volume_multiplier_mixer_max > volume_multiplier_mixer)
				volume_multiplier_mixer++;
		}
	}
	else
#endif /* USE_VOLUME_AUTO_ADJUST */
	for (pos = 0; pos < samples; pos++)
	{
		INT32 sample = inputs[0][pos];
		int inp;

		/* add up all the inputs */
		for (inp = 1; inp < numinputs; inp++)
			sample += inputs[inp][pos];
		outputs[0][pos] = sample;
	}
}


/*-------------------------------------------------
    mixer_postload - postload function to reset
    the mixer stream to the proper sample rate
-------------------------------------------------*/

static STATE_POSTLOAD( mixer_postload )
{
	sound_stream *stream = (sound_stream *)param;
	stream_set_sample_rate(stream, machine->sample_rate);
}



/***************************************************************************
    SPEAKER OUTPUT DEVICE INTERFACE
***************************************************************************/

/*-------------------------------------------------
    speaker_output_start - device start callback
    for a speaker
-------------------------------------------------*/

static DEVICE_START( speaker_output )
{
	speaker_info *info = (speaker_info *)device->token;

	/* copy in all the relevant info */
	info->speaker = (const speaker_config *)device->inline_config;
	info->tag = device->tag;
}


/*-------------------------------------------------
    speaker_output_stop - device stop callback
    for a speaker
-------------------------------------------------*/

static DEVICE_STOP( speaker_output )
{
#ifdef MAME_DEBUG
	speaker_info *info = (speaker_info *)device->token;

	/* log the maximum sample values for all speakers */
	if (info->max_sample > 0)
		mame_printf_debug("Speaker \"%s\" - max = %d (gain *= %f) - %d%% samples clipped\n", info->tag, info->max_sample, 32767.0 / (info->max_sample ? info->max_sample : 1), (int)((double)info->clipped_samples * 100.0 / info->total_samples));
#endif /* MAME_DEBUG */
}


/*-------------------------------------------------
    speaker_output_get_info - device get info
    callback
-------------------------------------------------*/

DEVICE_GET_INFO( speaker_output )
{
	switch (state)
	{
		/* --- the following bits of info are returned as 64-bit signed integers --- */
		case DEVINFO_INT_TOKEN_BYTES:					info->i = sizeof(speaker_info);			break;
		case DEVINFO_INT_INLINE_CONFIG_BYTES:			info->i = sizeof(speaker_config);		break;
		case DEVINFO_INT_CLASS:							info->i = DEVICE_CLASS_AUDIO;			break;

		/* --- the following bits of info are returned as pointers to data or functions --- */
		case DEVINFO_FCT_START:							info->start = DEVICE_START_NAME(speaker_output); break;
		case DEVINFO_FCT_STOP:							info->stop = DEVICE_STOP_NAME(speaker_output); break;
		case DEVINFO_FCT_RESET:							/* Nothing */							break;

		/* --- the following bits of info are returned as NULL-terminated strings --- */
		case DEVINFO_STR_NAME:							strcpy(info->s, "Speaker");				break;
		case DEVINFO_STR_FAMILY:						strcpy(info->s, "Sound");				break;
		case DEVINFO_STR_VERSION:						strcpy(info->s, "1.0");					break;
		case DEVINFO_STR_SOURCE_FILE:					strcpy(info->s, __FILE__);				break;
		case DEVINFO_STR_CREDITS:						strcpy(info->s, "Copyright Nicola Salmoria and the MAME Team"); break;
	}
}



/***************************************************************************
    MISCELLANEOUS HELPERS
***************************************************************************/

/*-------------------------------------------------
    sound_set_output_gain - set the gain of a
    particular output
-------------------------------------------------*/

void sound_set_output_gain(const device_config *device, int output, float gain)
{
	sound_stream *stream;
	int outputnum;

	if (stream_device_output_to_stream_output(device, output, &stream, &outputnum))
		stream_set_output_gain(stream, outputnum, gain);
}



/***************************************************************************
    USER GAIN CONTROLS
***************************************************************************/

/*-------------------------------------------------
    sound_get_user_gain_count - return the number
    of user-controllable gain parameters
-------------------------------------------------*/

int sound_get_user_gain_count(running_machine *machine)
{
	const device_config *curspeak;
	int count = 0;

	/* count up the number of speaker inputs */
	for (curspeak = speaker_output_first(machine->config); curspeak != NULL; curspeak = speaker_output_next(curspeak))
	{
		speaker_info *info = (speaker_info *)curspeak->token;
		count += info->inputs;
	}
	return count;
}


/*-------------------------------------------------
    sound_set_user_gain - set the nth user gain
    value
-------------------------------------------------*/

void sound_set_user_gain(running_machine *machine, int index, float gain)
{
	int inputnum;
	speaker_info *spk = index_to_input(machine, index, &inputnum);

	if (spk != NULL)
	{
		spk->input[inputnum].gain = gain;
		stream_set_input_gain(spk->mixer_stream, inputnum, gain);
	}
}


/*-------------------------------------------------
    sound_get_user_gain - get the nth user gain
    value
-------------------------------------------------*/

float sound_get_user_gain(running_machine *machine, int index)
{
	int inputnum;
	speaker_info *spk = index_to_input(machine, index, &inputnum);
	return (spk != NULL) ? spk->input[inputnum].gain : 0;
}


/*-------------------------------------------------
    sound_get_default_gain - return the default
    gain of the nth user value
-------------------------------------------------*/

float sound_get_default_gain(running_machine *machine, int index)
{
	int inputnum;
	speaker_info *spk = index_to_input(machine, index, &inputnum);
	return (spk != NULL) ? spk->input[inputnum].default_gain : 0;
}


/*-------------------------------------------------
    sound_get_user_gain_name - return the name
    of the nth user value
-------------------------------------------------*/

const char *sound_get_user_gain_name(running_machine *machine, int index)
{
	int inputnum;
	speaker_info *spk = index_to_input(machine, index, &inputnum);
	return (spk != NULL) ? spk->input[inputnum].name : NULL;
}
