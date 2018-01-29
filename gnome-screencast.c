/*
 * obs-gnome-screencast. OBS Studio source plugin.
 * Copyright (C) 2018 Florian Zwoch <fzwoch@gmail.com>
 *
 * This file is part of obs-gnome-screencast.
 *
 * obs-gnome-screencast is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * obs-gnome-screencast is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obs-gnome-screencast. If not, see <http://www.gnu.org/licenses/>.
 */

#include <obs/obs-module.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gdk/gdk.h>

OBS_DECLARE_MODULE()

typedef struct {
	GstElement* pipe;
	obs_source_t* source;
	obs_data_t* settings;
	gint64 frame_count;
	GdkRectangle rect;
	gulong handler_id;
} data_t;

static GstFlowReturn new_sample(GstAppSink* appsink, gpointer user_data)
{
	data_t* data = user_data;
	GstSample* sample = gst_app_sink_pull_sample(appsink);
	GstBuffer* buffer = gst_sample_get_buffer(sample);
	GstMapInfo info;

	gst_buffer_map(buffer, &info, GST_MAP_READ);

	struct obs_source_frame frame = {
		.width = data->rect.width,
		.height = data->rect.height,
		.format = VIDEO_FORMAT_BGRA,
		.timestamp = data->frame_count++,
		.full_range = true,
		.linesize[0] = data->rect.width * 4,
		.data[0] = info.data,
	};

	obs_source_output_video(data->source, &frame);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static const char* get_name(void* type_data)
{
	return "GNOME Screen Cast";
}

static void start(data_t* data)
{
	GError* err = NULL;

	gint screen = obs_data_get_int(data->settings, "screen");
	if (screen >= gdk_display_get_n_monitors(gdk_display_get_default()))
	{
		screen = 0;
	}

	gdk_monitor_get_geometry(gdk_display_get_monitor(gdk_display_get_default(), screen), &data->rect);

	gchar variant_string[1024];
	g_snprintf(variant_string, sizeof(variant_string), "{'draw-cursor' : <%s>, 'framerate' : <%lld>, 'pipeline' : <'queue ! shmsink socket-path=%s wait-for-connection=false sync=false'>}", obs_data_get_bool(data->settings, "show_cursor") ? "true" : "false", obs_data_get_int(data->settings, "frame_rate"), obs_data_get_string(data->settings, "shm_socket"));

	GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		NULL,
		"org.gnome.Shell.Screencast",
		"/org/gnome/Shell/Screencast",
		"org.gnome.Shell.Screencast",
		NULL,
		&err);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add_value(builder, g_variant_new_int32(data->rect.x));
	g_variant_builder_add_value(builder, g_variant_new_int32(data->rect.y));
	g_variant_builder_add_value(builder, g_variant_new_int32(data->rect.width));
	g_variant_builder_add_value(builder, g_variant_new_int32(data->rect.height));
	g_variant_builder_add_value(builder, g_variant_new_string("/dev/null"));
	g_variant_builder_add_value(builder, g_variant_new_parsed(variant_string));

	GVariant* res = g_dbus_proxy_call_sync(
		proxy,
		"ScreencastArea",
		g_variant_builder_end(builder),
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	g_variant_builder_unref(builder);
	g_object_unref(proxy);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot start GNOME Screen Cast - DBus call failed: %s", err->message);
		g_error_free(err);

		return;
	}

	gboolean success = FALSE;
	g_variant_get(res, "(bs)", &success, NULL);
	g_variant_unref(res);

	if (success != TRUE)
	{
		blog(LOG_ERROR, "Cannot start GNOME Screen Cast");

		return;
	}

	gchar pipe[1024];
	g_snprintf(pipe, sizeof(pipe), "shmsrc socket-path=%s ! rawvideoparse format=bgrx width=%d height=%d ! appsink max-buffers=10 drop=true name=appsink sync=false", obs_data_get_string(data->settings, "shm_socket"), data->rect.width, data->rect.height);

	data->pipe = gst_parse_launch(pipe, &err);
	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot start GStreamer: %s", err->message);
		g_error_free(err);

		return;
	}

	GstAppSinkCallbacks cbs = {
		NULL,
		NULL,
		new_sample
	};

	GstElement* appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");
	gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &cbs, data, NULL);
	gst_object_unref(appsink);

	data->frame_count = 0;

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);
}

static void update(void* data, obs_data_t* settings);

static gboolean timed_update(gpointer user_data)
{
	data_t* data = user_data;

	update(data, data->settings);

	return FALSE;
}

static void monitors_changed(GdkScreen* screen, gpointer user_data)
{
	data_t* data = user_data;

	g_timeout_add(5000, timed_update, data); // FIXME: we need to delay or fail.. could we sync this better?
}

static void* create(obs_data_t* settings, obs_source_t* source)
{
	data_t* data = g_new0(data_t, 1);

	data->source = source;
	data->settings = settings;
	data->handler_id = g_signal_connect(gdk_display_get_default_screen(gdk_display_get_default()), "monitors-changed", G_CALLBACK(monitors_changed), data);

	return data;
}

static void stop(data_t* data)
{
	GError* err = NULL;

	if (data->pipe == NULL)
	{
		return;
	}

	gst_element_set_state(data->pipe, GST_STATE_NULL);
	gst_object_unref(data->pipe);
	data->pipe = NULL;

	GDBusProxy* proxy = g_dbus_proxy_new_for_bus_sync(
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		NULL,
		"org.gnome.Shell.Screencast",
		"/org/gnome/Shell/Screencast",
		"org.gnome.Shell.Screencast",
		NULL,
		&err);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	GVariant* res = g_dbus_proxy_call_sync(
		proxy,
		"StopScreencast",
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	g_object_unref(proxy);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot stop GNOME Screen Cast - DBus call failed: %s", err->message);
		g_error_free(err);

		return;
	}

	gboolean success = FALSE;
	g_variant_get(res, "(b)", &success);
	g_variant_unref(res);

	if (success != TRUE)
	{
		blog(LOG_ERROR, "Cannot stop GNOME Screen Cast");
	}
}

static void destroy(void* data)
{
	g_signal_handler_disconnect(gdk_display_get_default_screen(gdk_display_get_default()), ((data_t*)data)->handler_id);
	stop(data);
	g_free(data);
}

static void get_defaults(obs_data_t* settings)
{
	obs_data_set_default_int(settings, "screen", 0);
	obs_data_set_default_string(settings, "shm_socket", "/tmp/obs-gnome-screencast.sock");
	obs_data_set_default_bool(settings, "show_cursor", true);
	obs_data_set_default_int(settings, "frame_rate", 30);
}

static obs_properties_t* get_properties(void* data)
{
	obs_properties_t* props = obs_properties_create();
	obs_property_t* prop;

	prop = obs_properties_add_list(props, "screen", "Screen", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	for (int i = 0; i < gdk_display_get_n_monitors(gdk_display_get_default()); i++)
	{
		gchar name[32];
		g_snprintf(name, sizeof(name), "Screen #%d", i);
		obs_property_list_add_int(prop, name, i);
	}

	obs_properties_add_text(props, "shm_socket", "SHM Socket", OBS_TEXT_DEFAULT);
	obs_properties_add_bool(props, "show_cursor", "Capture Cursor (10 FPS only)");
	obs_properties_add_int(props, "frame_rate", "FPS", 1, 1000, 1);

	return props;
}

static void update(void* data, obs_data_t* settings)
{
	if (((data_t*)data)->pipe == NULL)
	{
		return;
	}

	stop(data);
	start(data);
}

static void show(void* data)
{
	start(data);
}

static void hide(void* data)
{
	stop(data);
}

bool obs_module_load(void)
{
	struct obs_source_info info = {
		.id = "gnome-screencast-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,

		.get_name = get_name,
		.create = create,
		.destroy = destroy,

		.get_defaults = get_defaults,
		.get_properties = get_properties,
		.update = update,
		.show = show,
		.hide = hide,
	};

	obs_register_source(&info);

	gst_init(NULL, NULL);

	return true;
}
