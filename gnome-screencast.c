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
#include <obs/util/platform.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gdk/gdk.h>

OBS_DECLARE_MODULE()

typedef struct {
	GDBusConnection* connection;
	GstElement* pipe;
	obs_source_t* source;
	obs_data_t* settings;
	gint64 frame_count;
} gnome_screencast_data_t;

static GstFlowReturn gnome_screencast_new_sample(GstAppSink* appsink, gpointer user_data)
{
	gnome_screencast_data_t* data = user_data;
	struct obs_source_frame frame = {};
	GstMapInfo info;
	GstSample* sample = gst_app_sink_pull_sample(appsink);

	GstCaps* caps = gst_sample_get_caps(sample);
	GstStructure* structure = gst_caps_get_structure(caps, 0);
	gst_structure_get_int(structure, "width", (gint*)&frame.width);
	gst_structure_get_int(structure, "height", (gint*)&frame.height);

	GstBuffer* buffer = gst_sample_get_buffer(sample);
	gst_buffer_map(buffer, &info, GST_MAP_READ);

	frame.format = VIDEO_FORMAT_BGRA;
	frame.timestamp = data->frame_count++;
	frame.full_range = true;
	frame.linesize[0] = frame.width * 4;
	frame.data[0] = info.data;

	obs_source_output_video(data->source, &frame);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static const char* gnome_screencast_get_name(void* type_data)
{
	return "GNOME Screen Cast";
}

static void gnome_screencast_start(gnome_screencast_data_t* data, obs_data_t* settings)
{
	GError* err = NULL;

	gint screen = obs_data_get_int(settings, "screen");
	if (screen > gdk_display_get_n_monitors(gdk_display_get_default()) - 1)
	{
		screen = 0;
	}

	GdkRectangle rect;
	gdk_monitor_get_geometry(gdk_display_get_monitor(gdk_display_get_default(), screen), &rect);

	gchar* tmp_socket = g_strdup_printf("/tmp/obs-gnome-screencast-%d", g_random_int_range(0,10000000)); // FIXME: make me really unique
	gchar* variant = g_strdup_printf("{'draw-cursor' : <%s>, 'framerate' : <%lld>, 'pipeline' : <'tee name=tee ! queue ! shmsink socket-path=%s wait-for-connection=false sync=false tee. ! queue'>}", obs_data_get_bool(settings, "show_cursor") ? "true" : "false", obs_data_get_int(settings, "frame_rate"), tmp_socket);

	GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add_value(builder, g_variant_new_int32(rect.x));
	g_variant_builder_add_value(builder, g_variant_new_int32(rect.y));
	g_variant_builder_add_value(builder, g_variant_new_int32(rect.width));
	g_variant_builder_add_value(builder, g_variant_new_int32(rect.height));
	g_variant_builder_add_value(builder, g_variant_new_string("/dev/null"));
	g_variant_builder_add_value(builder, g_variant_new_parsed(variant));
	g_free(variant);

	data->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);
		err = NULL;

		return;
	}

	GVariant* res = g_dbus_connection_call_sync(
		data->connection,
		"org.gnome.Shell.Screencast",
		"/org/gnome/Shell/Screencast",
		"org.gnome.Shell.Screencast",
		"ScreencastArea",
		g_variant_builder_end(builder),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot start GNOME Screen Cast - DBus call failed: %s", err->message);
		g_error_free(err);
		err = NULL;

		g_object_unref(data->connection);
		data->connection = NULL;

		return;
	}

	gboolean success = FALSE;
	gchar* file = NULL;
	g_variant_get(res, "(bs)", &success, &file);
	g_variant_unref(res);
	g_variant_builder_unref(builder);

	if (success != TRUE)
	{
		blog(LOG_ERROR, "Cannot start GNOME Screen Cast");

		g_object_unref(data->connection);
		data->connection = NULL;

		return;
	}

	gchar* pipe = g_strdup_printf("shmsrc socket-path=%s ! rawvideoparse format=bgrx width=%d height=%d ! appsink max-buffers=10 drop=true name=appsink sync=false", tmp_socket, rect.width, rect.height);
	data->pipe = gst_parse_launch(pipe, &err);
	g_free(pipe);
	g_free(tmp_socket);

	GstAppSinkCallbacks cbs = {
		NULL,
		NULL,
		gnome_screencast_new_sample
	};

	GstElement* appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");
	gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &cbs, data, NULL);
	gst_object_unref(appsink);

	data->frame_count = 0;

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);
}

static void* gnome_screencast_create(obs_data_t* settings, obs_source_t* source)
{
	gnome_screencast_data_t* data = g_new0(gnome_screencast_data_t, 1);

	data->source = source;
	data->settings = settings;

	gnome_screencast_start(data, settings);

	return data;
}

static void gnome_screencast_stop(gnome_screencast_data_t* data)
{
	GError* err = NULL;

	if (data->pipe != NULL)
	{
		gst_element_set_state(data->pipe, GST_STATE_NULL);
		gst_object_unref(data->pipe);
		data->pipe = NULL;
	}

	if (data->connection == NULL)
	{
		return;
	}

	GVariant* res = g_dbus_connection_call_sync(
		data->connection,
		"org.gnome.Shell.Screencast",
		"/org/gnome/Shell/Screencast",
		"org.gnome.Shell.Screencast",
		"StopScreencast",
		NULL,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot stop GNOME Screen Cast - DBus call failed: %s", err->message);
		g_error_free(err);
		err = NULL;
	}

	gboolean success = FALSE;
	g_variant_get(res, "(b)", &success);

	if (success != TRUE)
	{
		blog(LOG_ERROR, "Cannot stop GNOME Screen Cast");
	}

	g_variant_unref(res);

	g_object_unref(data->connection);
	data->connection = NULL;
}

static void gnome_screencast_destroy(void* data)
{
	gnome_screencast_stop(data);
	g_free(data);
}

static void gnome_screencast_get_defaults(obs_data_t* settings)
{
	obs_data_set_default_int(settings, "screen", 0);
	obs_data_set_default_bool(settings, "show_cursor", true);
	obs_data_set_default_int(settings, "frame_rate", 60);
}

static obs_properties_t* gnome_screencat_get_properties(void* data)
{
	obs_properties_t* props = obs_properties_create();

	obs_property_t* screens = obs_properties_add_list(props, "screen", "Screen", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_properties_add_bool(props, "show_cursor", "Capture cursor");
	obs_properties_add_int(props, "frame_rate", "Frame rate", 1, 200, 1);

	for (int i = 0; i < gdk_display_get_n_monitors(gdk_display_get_default()); i++)
	{
		gchar* name = g_strdup_printf("Screen #%d", i);
		obs_property_list_add_int(screens, name, i);
		g_free(name);
	}

	return props;
}

static void gnome_screencast_update(void* data, obs_data_t* settings)
{
	gnome_screencast_stop(data);
	gnome_screencast_start(data, settings);
}

bool obs_module_load(void)
{
	struct obs_source_info info = {};

	info.id = "gnome-screencast-source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;

	info.get_name = gnome_screencast_get_name;
	info.create = gnome_screencast_create;
	info.destroy = gnome_screencast_destroy;

	info.get_defaults = gnome_screencast_get_defaults;
	info.get_properties = gnome_screencat_get_properties;
	info.update = gnome_screencast_update;

	obs_register_source(&info);

	gst_init(NULL, NULL);

	return true;
}
