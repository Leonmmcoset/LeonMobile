#include "audio.h"
#include <errno.h>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct device {
    uint32_t id;
    int card_id, profile_device;
    unsigned channels;
    char name[160], description[160];
    struct pw_node *node;
    struct spa_hook listener;
};
struct card {
    struct audio_backend *backend;
    uint32_t id;
    struct pw_device *proxy;
    struct spa_hook listener;
    struct {
        int valid, device, index;
        unsigned channels;
    } routes[32];
};
struct audio_backend {
    struct audio *owner;
    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct pw_metadata *metadata;
    struct spa_hook registry_listener, core_listener, metadata_listener;
    struct device devices[32];
    struct card cards[32];
    char default_sink[160];
    uint32_t metadata_id;
    int ready, failed, route_changed;
};
static void core_info(void *data, const struct pw_core_info *info) {
    (void)info;
    ((struct audio_backend *)data)->ready = 1;
}
static void core_error(void *data, uint32_t id, int seq, int res, const char *message) {
    (void)seq;
    (void)message;
    struct audio_backend *b = data;
    if (id == PW_ID_CORE && res < 0) {
        b->failed = 1;
        b->ready = 0;
    }
}
static const struct pw_core_events core_events = {PW_VERSION_CORE_EVENTS, .info = core_info,
                                                  .error = core_error};
static int metadata_property(void *data, uint32_t subject, const char *key, const char *type,
                             const char *value) {
    (void)subject;
    (void)type;
    struct audio_backend *b = data;
    if (key && !strcmp(key, "default.audio.sink")) {
        lm_json *v = value ? lm_parse(value, strlen(value)) : NULL;
        snprintf(b->default_sink, sizeof(b->default_sink), "%s", lm_string(lm_get(v, "name"), ""));
        b->route_changed = 1;
        lm_free(v);
    }
    return 0;
}
static const struct pw_metadata_events metadata_events = {PW_VERSION_METADATA_EVENTS,
                                                          .property = metadata_property};
static int property_integer(const struct spa_dict *props, const char *key) {
    const char *value = spa_dict_lookup(props, key);
    if (!value || !*value)
        return -1;
    char *end;
    errno = 0;
    long number = strtol(value, &end, 10);
    return errno || *end || number < 0 || number > INT_MAX ? -1 : (int)number;
}
static void node_info(void *data, const struct pw_node_info *info) {
    struct device *device = data;
    if (!(info->change_mask & PW_NODE_CHANGE_MASK_PROPS) || !info->props)
        return;
    // Registry globals omit properties such as card.profile.device. Resolve
    // the hardware route from the bound node's complete, changing properties.
    device->card_id = property_integer(info->props, PW_KEY_DEVICE_ID);
    device->profile_device = property_integer(info->props, "card.profile.device");
    int channels = property_integer(info->props, "audio.channels");
    if (channels > 0 && channels <= 64)
        device->channels = (unsigned)channels;
}
static const struct pw_node_events node_events = {PW_VERSION_NODE_EVENTS, .info = node_info};
static void card_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next,
                       const struct spa_pod *param) {
    (void)seq;
    (void)index;
    (void)next;
    struct card *card = data;
    int32_t route = -1, device = -1;
    const struct spa_pod *props = NULL, *volumes = NULL;
    if (id != SPA_PARAM_Route || !param)
        return;
    if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamRoute, NULL,
                             SPA_PARAM_ROUTE_index, SPA_POD_Int(&route),
                             SPA_PARAM_ROUTE_device, SPA_POD_Int(&device),
                             SPA_PARAM_ROUTE_props, SPA_POD_Pod(&props)) < 0 ||
        route < 0 || device < 0)
        return;
    if (spa_pod_parse_object(props, SPA_TYPE_OBJECT_Props, NULL,
                             SPA_PROP_channelVolumes, SPA_POD_Pod(&volumes)) < 0)
        return;
    float values[64];
    unsigned channels = spa_pod_copy_array(volumes, SPA_TYPE_Float, values, 64);
    if (!channels)
        return;
    size_t slot = 32;
    for (size_t i = 0; i < 32; ++i) {
        if (card->routes[i].valid && card->routes[i].device == device) {
            slot = i;
            break;
        }
        if (!card->routes[i].valid)
            slot = i;
    }
    if (slot == 32)
        return;
    card->routes[slot].valid = 1;
    card->routes[slot].device = device;
    card->routes[slot].index = route;
    card->routes[slot].channels = channels;
    card->backend->route_changed = 1;
}
static void card_info(void *data, const struct pw_device_info *info) {
    struct card *card = data;
    if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS))
        return;
    for (uint32_t i = 0; i < info->n_params; ++i)
        if (info->params[i].id == SPA_PARAM_Route) {
            memset(card->routes, 0, sizeof(card->routes));
            if (info->params[i].flags & SPA_PARAM_INFO_READ)
                pw_device_enum_params(card->proxy, 0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
        }
}
static const struct pw_device_events card_events = {PW_VERSION_DEVICE_EVENTS, .info = card_info,
                                                    .param = card_param};
static void global(void *data, uint32_t id, uint32_t permissions, const char *type,
                   uint32_t version, const struct spa_dict *props) {
    (void)permissions;
    (void)version;
    struct audio_backend *b = data;
    if (!props)
        return;
    const char *kind = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!strcmp(type, PW_TYPE_INTERFACE_Node) && kind && !strcmp(kind, "Audio/Sink")) {
        for (size_t i = 0; i < 32; ++i)
            if (!b->devices[i].node) {
                struct device *d = &b->devices[i];
                d->id = id;
                d->card_id = property_integer(props, PW_KEY_DEVICE_ID);
                d->profile_device = property_integer(props, "card.profile.device");
                const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME),
                           *description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
                snprintf(d->name, sizeof(d->name), "%s", name ? name : "");
                snprintf(d->description, sizeof(d->description), "%s",
                         description ? description : d->name);
                const char *channels = spa_dict_lookup(props, "audio.channels");
                d->channels = channels ? (unsigned)atoi(channels) : 2;
                if (d->channels < 1 || d->channels > 64)
                    d->channels = 2;
                d->node = pw_registry_bind(b->registry, id, type, PW_VERSION_NODE, 0);
                if (d->node)
                    pw_node_add_listener(d->node, &d->listener, &node_events, d);
                b->route_changed = 1;
                break;
            }
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Device) && kind && !strcmp(kind, "Audio/Device")) {
        for (size_t i = 0; i < 32; ++i)
            if (!b->cards[i].proxy) {
                struct card *card = &b->cards[i];
                card->backend = b;
                card->id = id;
                card->proxy = pw_registry_bind(b->registry, id, type, PW_VERSION_DEVICE, 0);
                if (!card->proxy)
                    break;
                pw_device_add_listener(card->proxy, &card->listener, &card_events, card);
                uint32_t params[] = {SPA_PARAM_Route};
                pw_device_subscribe_params(card->proxy, params, 1);
                pw_device_enum_params(card->proxy, 0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
                break;
            }
    } else if (!strcmp(type, PW_TYPE_INTERFACE_Metadata) && !b->metadata) {
        const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (name && !strcmp(name, "default")) {
            b->metadata = pw_registry_bind(b->registry, id, type, PW_VERSION_METADATA, 0);
            b->metadata_id = id;
            pw_metadata_add_listener(b->metadata, &b->metadata_listener, &metadata_events, b);
        }
    }
}
static void global_remove(void *data, uint32_t id) {
    struct audio_backend *b = data;
    if (b->metadata && b->metadata_id == id) {
        pw_proxy_destroy((struct pw_proxy *)b->metadata);
        b->metadata = NULL;
        b->default_sink[0] = 0;
    }
    for (size_t i = 0; i < 32; ++i)
        if (b->devices[i].node && b->devices[i].id == id) {
            pw_proxy_destroy((struct pw_proxy *)b->devices[i].node);
            memset(&b->devices[i], 0, sizeof(b->devices[i]));
            b->route_changed = 1;
        }
    for (size_t i = 0; i < 32; ++i)
        if (b->cards[i].proxy && b->cards[i].id == id) {
            pw_proxy_destroy((struct pw_proxy *)b->cards[i].proxy);
            memset(&b->cards[i], 0, sizeof(b->cards[i]));
            b->route_changed = 1;
        }
}
static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS, .global = global, .global_remove = global_remove};
struct audio_backend *audio_backend_new(struct audio *s) {
    pw_init(NULL, NULL);
    struct audio_backend *b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->owner = s;
    b->loop = pw_thread_loop_new("leon-audio", NULL);
    if (!b->loop)
        goto fail;
    b->context = pw_context_new(pw_thread_loop_get_loop(b->loop), NULL, 0);
    if (!b->context)
        goto fail;
    b->core =
        pw_context_connect(b->context,
                           pw_properties_new(PW_KEY_REMOTE_NAME, "/run/user/1000/pipewire-0",
                                             PW_KEY_APP_NAME, "LeonMobile audio service", NULL),
                           0);
    if (!b->core)
        goto fail;
    pw_core_add_listener(b->core, &b->core_listener, &core_events, b);
    b->registry = pw_core_get_registry(b->core, PW_VERSION_REGISTRY, 0);
    if (!b->registry)
        goto fail;
    pw_registry_add_listener(b->registry, &b->registry_listener, &registry_events, b);
    if (pw_thread_loop_start(b->loop) < 0)
        goto fail;
    return b;
fail:
    audio_backend_free(b);
    return NULL;
}
void audio_backend_free(struct audio_backend *b) {
    if (!b)
        return;
    if (b->loop)
        pw_thread_loop_stop(b->loop);
    for (size_t i = 0; i < 32; ++i)
        if (b->devices[i].node)
            pw_proxy_destroy((struct pw_proxy *)b->devices[i].node);
    for (size_t i = 0; i < 32; ++i)
        if (b->cards[i].proxy)
            pw_proxy_destroy((struct pw_proxy *)b->cards[i].proxy);
    if (b->metadata)
        pw_proxy_destroy((struct pw_proxy *)b->metadata);
    if (b->registry)
        pw_proxy_destroy((struct pw_proxy *)b->registry);
    if (b->core)
        pw_core_disconnect(b->core);
    if (b->context)
        pw_context_destroy(b->context);
    if (b->loop)
        pw_thread_loop_destroy(b->loop);
    free(b);
}
int audio_backend_ready(struct audio_backend *b) {
    if (!b)
        return 0;
    pw_thread_loop_lock(b->loop);
    int ready = b->ready && !b->failed;
    pw_thread_loop_unlock(b->loop);
    return ready;
}
static void process(void *data) {
    struct audio_stream *p = data;
    struct pw_buffer *buffer = pw_stream_dequeue_buffer(p->stream);
    if (!buffer)
        return;
    struct spa_buffer *b = buffer->buffer;
    if (!b->n_datas || !b->datas[0].data) {
        pw_stream_queue_buffer(p->stream, buffer);
        return;
    }
    struct spa_data *d = &b->datas[0];
    size_t bytes = d->maxsize;
    unsigned frame = p->channels * 2;
#if PW_CHECK_VERSION(0, 3, 70)
    if (buffer->requested && buffer->requested < bytes / frame)
        bytes = buffer->requested * frame;
#endif
    bytes -= bytes % frame;
    audio_render(p, d->data, bytes);
    d->chunk->offset = 0;
    d->chunk->stride = (int32_t)frame;
    d->chunk->size = (uint32_t)bytes;
    pw_stream_queue_buffer(p->stream, buffer);
}
static const struct pw_stream_events stream_events = {PW_VERSION_STREAM_EVENTS, .process = process};
int audio_backend_open(struct audio *s, struct audio_stream *p) {
    if (s->test)
        return 0;
    struct audio_backend *b = s->backend;
    if (!audio_backend_ready(b))
        return -1;
    _Static_assert(sizeof(struct spa_hook) <= sizeof(p->listener), "stream listener storage");
    pw_thread_loop_lock(b->loop);
    char name[64];
    snprintf(name, sizeof(name), "leon-%s", p->token);
    p->stream = pw_stream_new(
        b->core, name,
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, p->focus == 2 ? "Notification" : "Music",
                          PW_KEY_APP_ID, p->app, PW_KEY_NODE_NAME, name, NULL));
    int result = -1;
    if (p->stream) {
        pw_stream_add_listener(p->stream, (struct spa_hook *)p->listener, &stream_events, p);
        uint8_t buffer[1024];
        struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        struct spa_audio_info_raw format = {
            .format = SPA_AUDIO_FORMAT_S16_LE, .rate = p->rate, .channels = p->channels};
        if (p->channels == 1)
            format.position[0] = SPA_AUDIO_CHANNEL_MONO;
        else {
            format.position[0] = SPA_AUDIO_CHANNEL_FL;
            format.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        const struct spa_pod *param =
            spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format);
        result =
            pw_stream_connect(p->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                              PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, &param, 1);
    }
    pw_thread_loop_unlock(b->loop);
    return result;
}
void audio_backend_close(struct audio *s, struct audio_stream *p) {
    if (p->stream && s->backend) {
        pw_thread_loop_lock(s->backend->loop);
        pw_stream_destroy(p->stream);
        p->stream = NULL;
        pw_thread_loop_unlock(s->backend->loop);
    }
}
void audio_backend_master(struct audio *s) {
    struct audio_backend *b = s->backend;
    if (!b)
        return;
    pw_thread_loop_lock(b->loop);
    for (size_t i = 0; i < 32; ++i) {
        struct device *d = &b->devices[i];
        if (!d->node || strcmp(d->name, b->default_sink))
            continue;
        struct card *card = NULL;
        int route = -1;
        unsigned channels = d->channels;
        for (size_t j = 0; j < 32; ++j)
            if (b->cards[j].proxy && d->card_id >= 0 && b->cards[j].id == (uint32_t)d->card_id)
                for (size_t k = 0; k < 32; ++k)
                    if (b->cards[j].routes[k].valid && b->cards[j].routes[k].device == d->profile_device) {
                        card = &b->cards[j];
                        route = card->routes[k].index;
                        channels = card->routes[k].channels;
                        break;
                    }
        uint8_t buffer[1024];
        struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        float values[64];
        float volume = (float)s->volume / 100;
        for (unsigned j = 0; j < channels; ++j)
            values[j] = volume * volume * volume;
        const struct spa_pod *param = spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props, SPA_PROP_channelVolumes,
            SPA_POD_Array(sizeof(float), SPA_TYPE_Float, channels, values), SPA_PROP_mute,
            SPA_POD_Bool(s->muted));
        if (card) {
            // ALSA devices expose their effective volume through the active
            // Route. Node-only Props would leave hardware volume unchanged.
            const struct spa_pod *r = spa_pod_builder_add_object(
                &builder, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route,
                SPA_PARAM_ROUTE_index, SPA_POD_Int(route),
                SPA_PARAM_ROUTE_device, SPA_POD_Int(d->profile_device),
                SPA_PARAM_ROUTE_props, SPA_POD_Pod(param), SPA_PARAM_ROUTE_save, SPA_POD_Bool(true));
            pw_device_set_param(card->proxy, SPA_PARAM_Route, 0, r);
        } else
            pw_node_set_param(d->node, SPA_PARAM_Props, 0, param);
    }
    b->route_changed = 0;
    pw_thread_loop_unlock(b->loop);
}
int audio_backend_route(struct audio *s, unsigned id) {
    struct audio_backend *b = s->backend;
    if (!b)
        return -1;
    int result = -1;
    pw_thread_loop_lock(b->loop);
    for (size_t i = 0; i < 32; ++i)
        if (b->metadata && b->devices[i].node && b->devices[i].id == id) {
            lm_json *v = lm_object();
            lm_set_string(v, "name", b->devices[i].name);
            char *text = lm_dump(v);
            lm_free(v);
            if (text) {
                result = pw_metadata_set_property(b->metadata, PW_ID_CORE,
                                                  "default.configured.audio.sink",
                                                  "Spa:String:JSON", text);
                free(text);
            }
            break;
        }
    pw_thread_loop_unlock(b->loop);
    return result;
}
lm_json *audio_backend_devices(struct audio *s) {
    lm_json *out = lm_array();
    struct audio_backend *b = s->backend;
    if (!b)
        return out;
    pw_thread_loop_lock(b->loop);
    for (size_t i = 0; i < 32; ++i)
        if (b->devices[i].node) {
            lm_json *d = lm_object();
            lm_set_integer(d, "id", b->devices[i].id);
            lm_set_string(d, "name", b->devices[i].name);
            lm_set_string(d, "description", b->devices[i].description);
            lm_set_boolean(d, "default", !strcmp(b->devices[i].name, b->default_sink));
            lm_append(out, d);
        }
    pw_thread_loop_unlock(b->loop);
    return out;
}
