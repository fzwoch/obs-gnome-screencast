/*
 * obs-gnome-mutter-screencast. OBS Studio source plugin.
 * Copyright (C) 2019 Florian Zwoch <fzwoch@gmail.com>
 *
 * This file is part of obs-gnome-mutter-screencast.
 *
 * obs-gnome-mutter-screencast is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * obs-gnome-mutter-screencast is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obs-gnome-mutter-screencast. If not, see <http://www.gnu.org/licenses/>.
 */

#include <obs/obs-module.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/video/video.h>

OBS_DECLARE_MODULE()

typedef struct {
	GstElement* pipe;
	gchar* session_path;
	obs_source_t* source;
	obs_data_t* settings;
} data_t;

static const char* get_name(void* type_data)
{
	return "GNOME Mutter Screen Cast";
}

static gboolean bus_callback(GstBus* bus, GstMessage* message, gpointer user_data)
{
	data_t* data = user_data;

	switch (GST_MESSAGE_TYPE(message))
	{
		case GST_MESSAGE_EOS:
			obs_source_output_video(data->source, NULL);
			break;
		case GST_MESSAGE_ERROR:
			{
				GError* err;
				gst_message_parse_error(message, &err, NULL);
				blog(LOG_ERROR, "%s", err->message);
				g_error_free(err);
			}
			gst_element_set_state(data->pipe, GST_STATE_NULL);
			obs_source_output_video(data->source, NULL);
			break;
		default:
			break;
	}

	return TRUE;
}

static GstFlowReturn new_sample(GstAppSink* appsink, gpointer user_data)
{
	data_t* data = user_data;
	GstSample* sample = gst_app_sink_pull_sample(appsink);
	GstBuffer* buffer = gst_sample_get_buffer(sample);
	GstCaps* caps = gst_sample_get_caps(sample);
	GstMapInfo info;
	GstVideoInfo video_info;

	gst_video_info_from_caps(&video_info, caps);
	gst_buffer_map(buffer, &info, GST_MAP_READ);

	struct obs_source_frame frame = {};

	frame.width = video_info.width;
	frame.height = video_info.height;
	frame.linesize[0] = video_info.stride[0];
	frame.linesize[1] = video_info.stride[1];
	frame.linesize[2] = video_info.stride[2];
	frame.data[0] = info.data + video_info.offset[0];
	frame.data[1] = info.data + video_info.offset[1];
	frame.data[2] = info.data + video_info.offset[2];

	enum video_range_type range = VIDEO_RANGE_DEFAULT;
	switch (video_info.colorimetry.range)
	{
		case GST_VIDEO_COLOR_RANGE_0_255:
			range = VIDEO_RANGE_FULL;
			frame.full_range = 1;
			break;
		case GST_VIDEO_COLOR_RANGE_16_235:
			range = VIDEO_RANGE_PARTIAL;
			break;
		default:
			break;
	}

	enum video_colorspace cs = VIDEO_CS_DEFAULT;
	switch (video_info.colorimetry.matrix)
	{
		case GST_VIDEO_COLOR_MATRIX_BT709:
			cs = VIDEO_CS_709;
			break;
		case GST_VIDEO_COLOR_MATRIX_BT601:
			cs = VIDEO_CS_601;
			break;
		default:
			break;
	}

	video_format_get_parameters(cs, range, frame.color_matrix, frame.color_range_min, frame.color_range_max);

	switch (video_info.finfo->format)
	{
		case GST_VIDEO_FORMAT_I420:
			frame.format = VIDEO_FORMAT_I420;
			break;
		case GST_VIDEO_FORMAT_NV12:
			frame.format = VIDEO_FORMAT_NV12;
			break;
		case GST_VIDEO_FORMAT_BGRx:
			frame.format = VIDEO_FORMAT_BGRX;
			break;
		case GST_VIDEO_FORMAT_BGRA:
			frame.format = VIDEO_FORMAT_BGRA;
			break;
		case GST_VIDEO_FORMAT_RGBx:
		case GST_VIDEO_FORMAT_RGBA:
			frame.format = VIDEO_FORMAT_RGBA;
			break;
		case GST_VIDEO_FORMAT_UYVY:
			frame.format = VIDEO_FORMAT_UYVY;
			break;
		case GST_VIDEO_FORMAT_YUY2:
			frame.format = VIDEO_FORMAT_YUY2;
			break;
		case GST_VIDEO_FORMAT_YVYU:
			frame.format = VIDEO_FORMAT_YVYU;
			break;
		default:
			frame.format = VIDEO_FORMAT_NONE;
			blog(LOG_ERROR, "Unknown video format: %s", video_info.finfo->name);
			break;
	}

	obs_source_output_video(data->source, &frame);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static void dbus_stream_closed_cb(GDBusConnection *connection,
	const gchar *sender_name,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *signal_name,
	GVariant *parameters,
	gpointer user_data)
{
	data_t* data = user_data;

	gst_element_set_state(data->pipe, GST_STATE_NULL);

	gst_object_unref(data->pipe);
	data->pipe = NULL;
}

static void dbus_cb(GDBusConnection *connection,
	const gchar *sender_name,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *signal_name,
	GVariant *parameters,
	gpointer user_data)
{
	GError *err = NULL;
	data_t* data = user_data;
	guint node_id = 0;

	g_variant_get(parameters, "(u)", &node_id, NULL);

	gchar* pipeline = g_strdup_printf("pipewiresrc always-copy=true client-name=obs-studio path=%u ! video/x-raw ! queue ! appsink name=appsink", node_id);

	data->pipe = gst_parse_launch(pipeline, &err);
	g_free(pipeline);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Error gst_parse_launch(): %s", err->message);
		g_error_free(err);

		return;
	}

	GstAppSinkCallbacks appsink_cbs = {
		NULL,
		NULL,
		new_sample
	};

	GstElement* appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");
	gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_cbs, data, NULL);

	if (!obs_data_get_bool(data->settings, "sync_appsink"))
		g_object_set(appsink, "sync", FALSE, NULL);

	gst_object_unref(appsink);

	GstBus* bus = gst_element_get_bus(data->pipe);
	gst_bus_add_watch(bus, bus_callback, data);
	gst_object_unref(bus);

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);
}

static void start(data_t* data)
{
	GError* err = NULL;

	GDBusConnection* dbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	GVariantBuilder* builder = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);

	g_variant_builder_add_value(builder, g_variant_new_parsed("{'dummy' : <0>}"));

	GVariant* session_res = g_dbus_connection_call_sync(dbus,
		"org.gnome.Mutter.ScreenCast",
		"/org/gnome/Mutter/ScreenCast",
		"org.gnome.Mutter.ScreenCast",
		"CreateSession",
		g_variant_builder_end(builder),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	g_variant_builder_unref(builder);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot call CreateSession() on DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	gchar* session_path;
	g_variant_get(session_res, "(o)", &session_path, NULL);

	data->session_path = g_strdup(session_path);

	builder = g_variant_builder_new(G_VARIANT_TYPE_TUPLE);

	g_variant_builder_add_value(builder, g_variant_new_string(""));
	g_variant_builder_add_value(builder, g_variant_new_parsed("{'dummy' : <0>}"));

	GVariant* stream_res = g_dbus_connection_call_sync(dbus,
		"org.gnome.Mutter.ScreenCast",
		session_path,
		"org.gnome.Mutter.ScreenCast.Session",
		"RecordMonitor",
		g_variant_builder_end(builder),
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	g_variant_builder_unref(builder);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot call RecordMonitor() on DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	gchar* stream_path;
	g_variant_get(stream_res, "(o)", &stream_path, NULL);

	g_dbus_connection_signal_subscribe(dbus,
		NULL,
		NULL,
		"Closed",
		session_path,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		dbus_stream_closed_cb,
		data,
		NULL);

	g_dbus_connection_signal_subscribe(dbus,
		NULL,
		NULL,
		"PipeWireStreamAdded",
		stream_path,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		dbus_cb,
		data,
		NULL);

	g_dbus_connection_call_sync(dbus,
		"org.gnome.Mutter.ScreenCast",
		session_path,
		"org.gnome.Mutter.ScreenCast.Session",
		"Start",
		NULL,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	g_variant_unref(session_res);
	g_variant_unref(stream_res);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot call Start() on DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	g_object_unref(dbus);
}

static void* create(obs_data_t* settings, obs_source_t* source)
{
	data_t* data = g_new0(data_t, 1);

	data->source = source;
	data->settings = settings;

	return data;
}

static void stop(data_t* data)
{
	GError* err = NULL;

	if (data->pipe == NULL)
	{
		return;
	}

	GDBusConnection* dbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	g_dbus_connection_call_sync(dbus,
		"org.gnome.Mutter.ScreenCast",
		data->session_path,
		"org.gnome.Mutter.ScreenCast.Session",
		"Stop",
		NULL,
		NULL,
		G_DBUS_CALL_FLAGS_NONE,
		-1,
		NULL,
		&err);

	if (err != NULL)
	{
		blog(LOG_ERROR, "Cannot call Stop() on DBus: %s", err->message);
		g_error_free(err);

		return;
	}

	g_free(data->session_path);
	data->session_path = NULL;

	g_object_unref(dbus);
}

static void destroy(void* data)
{
	stop(data);
	g_free(data);
}

static void get_defaults(obs_data_t* settings)
{
	obs_data_set_default_bool(settings, "sync_appsink", false);
}

static obs_properties_t* get_properties(void* data)
{
	obs_properties_t* props = obs_properties_create();

	obs_properties_add_bool(props, "sync_appsink", "Sync appsink to clock");

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
		.id = "gnome-mutter-screencast-source",
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
