#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <jack/jack.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSIZE 2048

#include <soundpipe.h>
#include <sporth.h>

#include "stream.h"
#include "audio.h"
#include "data.h"

static int sporth_jack_in(sporth_stack *stack, void *ud);

static int sp_jack_cb(jack_nframes_t nframes, void *arg)
{
    int i, chan;
    sp_jack *jd = arg;
    jack_default_audio_sample_t  *out[jd->sp->nchan];
    jack_default_audio_sample_t  *in = NULL;

    in = jack_port_get_buffer(jd->input_port, nframes);

    if(in == NULL) return 0;

    for(chan = 0; chan < jd->sp->nchan; chan++)
        out[chan] = jack_port_get_buffer (jd->output_port[chan], nframes);

    for(i = 0; i < nframes; i++){
        jd->in = in[i];
        jd->callback(jd->sp, jd->ud);
        for(chan = 0; chan < jd->sp->nchan; chan++)
        out[chan][i] = jd->sp->out[chan];
    }
    return 0;
}

static void sp_jack_shutdown (void *arg)
{
    exit (1);
}


int start_audio(plumber_data *pd, 
        void *ud, void (*callback)(sp_data *, void *), int port)
{
    const char *server_name = NULL;
    user_data *data = ud;
    int chan;
    jack_options_t options = JackNullOption;
    jack_status_t status;
    sp_jack *jd = &data->jd;
    jd->sp = pd->sp;
    jd->pd = pd;
    pd->ud = &data->jd;

    char client_name[256];

    sp_data *sp = pd->sp;
    jd->callback = callback;
    jd->ud = ud;

    jd->run = 0;
    strncpy(client_name, "soundpipe", 256);

    pd->sporth.flist[SPORTH_IN - SPORTH_FOFFSET].func = sporth_jack_in;

    jd->output_port = malloc(sizeof(jack_port_t *) * sp->nchan);
    jd->client = malloc(sizeof(jack_client_t *));
   
    jd->client[0] = jack_client_open (client_name, options, &status, server_name);
    if (jd->client[0] == NULL) {
        fprintf (stderr, "jack_client_open() failed, "
             "status = 0x%2.0x\n", status);
        if (status & JackServerFailed) {
            fprintf (stderr, "Unable to connect to JACK server\n");
        }
        exit (1);
    }
    if (status & JackServerStarted) {
        fprintf (stderr, "JACK server started\n");
    }
    if (status & JackNameNotUnique) {
        /* client_name = jack_get_client_name(jd->client[0]); */
        fprintf (stderr, "unique name `%s' assigned\n", client_name);
    }
    jack_set_process_callback (jd->client[0], sp_jack_cb, jd);
    jack_on_shutdown (jd->client[0], sp_jack_shutdown, 0);

    char chan_name[50];

    jd->input_port = jack_port_register(jd->client[0], "sp_input",
            JACK_DEFAULT_AUDIO_TYPE,
            JackPortIsInput, 0);

    for(chan = 0; chan < sp->nchan; chan++) {
        sprintf(chan_name, "output_%d", chan);
        jd->output_port[chan] = jack_port_register (jd->client[0], chan_name,
                          JACK_DEFAULT_AUDIO_TYPE,
                          JackPortIsOutput, chan);

        if (jd->output_port[chan] == NULL) {
            fprintf(stderr, "no more JACK ports available\n");
            exit (1);
        }

        if (jack_activate (jd->client[0])) {
            fprintf (stderr, "cannot activate client");
            exit (1);
        }
    }
    jd->ports = jack_get_ports (jd->client[0], NULL, NULL,
                JackPortIsPhysical|JackPortIsInput);
    if (jd->ports == NULL) {
        fprintf(stderr, "no physical playback ports\n");
        exit (1);
    }
    for(chan = 0; chan < sp->nchan; chan++) {
        if (jack_connect (jd->client[0], jack_port_name (jd->output_port[chan]), 
                    jd->ports[chan])) {
            fprintf (stderr, "cannot connect output ports\n"); 
        }
    }

    jd->run = 1;


    return SP_OK;
}

int stop_audio(sp_jack *jd)
{
    jd->run = 0;
    jack_client_close(jd->client[0]);
    free (jd->ports); 
    free(jd->output_port);
    free(jd->client);
    return SP_OK;
}

static int sporth_jack_in(sporth_stack *stack, void *ud)
{
    plumber_data *pd = (plumber_data *) ud;

    sp_jack * data = (sp_jack *) pd->ud;
    switch(pd->mode) {
        case PLUMBER_CREATE:
#ifdef DEBUG_MODE
            fprintf(stderr, "JACK IN: creating\n");
#endif
            plumber_add_ugen(pd, SPORTH_IN, NULL);

            sporth_stack_push_float(stack, 0);
            break;
        case PLUMBER_INIT:
#ifdef DEBUG_MODE
            fprintf(stderr, "JACK IN: initialising.\n");
#endif

            sporth_stack_push_float(stack, 0);

            break;

        case PLUMBER_COMPUTE:

            sporth_stack_push_float(stack, data->in);

            break;

        case PLUMBER_DESTROY:
#ifdef DEBUG_MODE
            fprintf(stderr, "JACK IN: destroying.\n");
#endif

            break;

        default:
            fprintf(stderr, "Unknown mode!\n");
            break;
    }
    return PLUMBER_OK;
}
