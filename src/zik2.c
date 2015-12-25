/* Zik2ctl
 * Copyright (C) 2015 Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>
 *
 * Zik2ctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zik2ctl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Zik2ctl. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "zik2.h"
#include "zik2connection.h"
#include "zik2message.h"
#include "zik2info.h"
#include "zik2api.h"

#define UNKNOWN_STR "unknown"
#define DEFAULT_NOISE_CONTROL_STRENGTH 1

enum
{
  PROP_0,
  PROP_NAME,
  PROP_ADDRESS,
  PROP_SERIAL,
  PROP_SOFTWARE_VERSION,
  PROP_NOISE_CONTROL,
  PROP_NOISE_CONTROL_MODE,
  PROP_NOISE_CONTROL_STRENGTH,
  PROP_SOURCE,
  PROP_BATTERY_STATE,
  PROP_BATTERY_PERCENT,
  PROP_VOLUME,
  PROP_HEAD_DETECTION,
  PROP_COLOR,
  PROP_FLIGHT_MODE,
  PROP_FRIENDLYNAME,
};

struct _Zik2Private
{
  gchar *name;
  gchar *address;

  /* audio */
  gboolean noise_control;
  Zik2NoiseControlMode noise_control_mode;
  guint noise_control_strength;
  gchar *source;
  guint volume;

  /* software */
  gchar *software_version;

  /* system */
  gchar *battery_state;
  guint battery_percentage;
  Zik2Color color;
  gboolean head_detection;
  gchar *serial;

  /* others */
  gboolean flight_mode;
  gchar *friendlyname;  /* the name used to generate the real bluetooth name */
};

#define ZIK2_NOISE_CONTROL_MODE_TYPE (zik2_noise_control_mode_get_type ())
static GType
zik2_noise_control_mode_get_type (void)
{
  static volatile GType type;

  static const GEnumValue modes[] = {
    { ZIK2_NOISE_CONTROL_MODE_OFF, "Disable noise control", "off" },
    { ZIK2_NOISE_CONTROL_MODE_ANC, "Enable noise cancelling", "anc" },
    { ZIK2_NOISE_CONTROL_MODE_AOC, "Enable street mode", "aoc" },
    { 0, NULL, NULL }
  };

  if (g_once_init_enter (&type)) {
    GType _type;

    _type = g_enum_register_static ("Zik2NoiseControlMode", modes);

    g_once_init_leave (&type, _type);
  }

  return type;
}

#define ZIK2_COLOR_TYPE (zik2_color_get_type ())
static GType
zik2_color_get_type (void)
{
  static volatile GType type;
  static const GEnumValue colors[] = {
    { ZIK2_COLOR_UNKNOWN, "Unknown", "unknown" },
    { ZIK2_COLOR_BLACK, "Black", "black" },
    { ZIK2_COLOR_BLUE, "Blue", "blue" },
    { 0, NULL, NULL }
  };

  if (g_once_init_enter (&type)) {
    GType _type = g_enum_register_static ("Zik2Color", colors);
    g_once_init_leave (&type, _type);
  }

  return type;
}

#define parent_class zik2_parent_class
G_DEFINE_TYPE (Zik2, zik2, G_TYPE_OBJECT);

/* GObject methods */
static void zik2_finalize (GObject * object);
static void zik2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec *pspec);
static void zik2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec *pspec);

static void
zik2_class_init (Zik2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (Zik2Private));

  gobject_class->finalize = zik2_finalize;
  gobject_class->get_property = zik2_get_property;
  gobject_class->set_property = zik2_set_property;

  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("name", "Name", "Zik2 name", UNKNOWN_STR,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address", "Zik2 bluetooth address",
          UNKNOWN_STR,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SERIAL,
      g_param_spec_string ("serial", "Serial", "Zik2 serial number",
          UNKNOWN_STR, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOFTWARE_VERSION,
      g_param_spec_string ("software-version", "Software-version",
          "Zik2 software version", UNKNOWN_STR,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOURCE,
      g_param_spec_string ("source", "Source", "Zik2 audio source",
          UNKNOWN_STR, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NOISE_CONTROL,
      g_param_spec_boolean ("noise-control", "Noise control",
          "Whether the noise control is active or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NOISE_CONTROL_MODE,
      g_param_spec_enum ("noise-control-mode", "Noise control mode",
          "Select the noise control mode", ZIK2_NOISE_CONTROL_MODE_TYPE,
          ZIK2_NOISE_CONTROL_MODE_OFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NOISE_CONTROL_STRENGTH,
      g_param_spec_uint ("noise-control-strength", "Noise control strength",
        "Set the noise control strength", 1, 2, DEFAULT_NOISE_CONTROL_STRENGTH,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BATTERY_STATE,
      g_param_spec_string ("battery-state", "Battery state",
        "State of the battery", UNKNOWN_STR,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BATTERY_PERCENT,
      g_param_spec_uint ("battery-percentage", "Battery percentage",
        "Battery charge percentage", 0, 100, 0,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_uint ("volume", "Volume", "Volume", 0, G_MAXUINT, 0,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEAD_DETECTION,
      g_param_spec_boolean ("head-detection", "Head detection",
          "Whether head detection is active or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLOR,
      g_param_spec_enum ("color", "Color", "Zik2 color", ZIK2_COLOR_TYPE,
        ZIK2_COLOR_UNKNOWN, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FLIGHT_MODE,
      g_param_spec_boolean ("flight-mode", "Flight mode",
          "Whether or not flight mode is active", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FRIENDLYNAME,
      g_param_spec_string ("friendlyname", "Friendlyname",
        "Friendly name used to generate the bluetooth one", UNKNOWN_STR,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
zik2_init (Zik2 * zik2)
{
  zik2->priv = G_TYPE_INSTANCE_GET_PRIVATE (zik2, ZIK2_TYPE, Zik2Private);

  zik2->priv->serial = g_strdup (UNKNOWN_STR);
  zik2->priv->software_version = g_strdup (UNKNOWN_STR);
  zik2->priv->source = g_strdup (UNKNOWN_STR);
  zik2->priv->battery_state = g_strdup (UNKNOWN_STR);
  zik2->priv->friendlyname = g_strdup (UNKNOWN_STR);

  zik2->priv->noise_control_strength = DEFAULT_NOISE_CONTROL_STRENGTH;
}

static void
zik2_finalize (GObject * object)
{
  Zik2 *zik2 = ZIK2 (object);
  Zik2Private *priv = zik2->priv;

  g_free (priv->name);
  g_free (priv->address);
  g_free (priv->serial);
  g_free (priv->software_version);
  g_free (priv->source);
  g_free (priv->battery_state);
  g_free (priv->friendlyname);

  if (zik2->conn)
    zik2_connection_free (zik2->conn);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
zik2_get_and_parse_reply (Zik2 * zik2, const gchar * path,
    Zik2RequestReplyData ** reply)
{
  Zik2Message *msg;
  Zik2Message *answer = NULL;
  Zik2RequestReplyData *result;
  gboolean ret = FALSE;

  msg = zik2_message_new_request (path, "get", NULL);

  if (!zik2_connection_send_message (zik2->conn, msg, &answer)) {
    g_critical ("failed to send get request '%s/get'", path);
    goto out;
  }

  if (!zik2_message_parse_request_reply (answer, &result)) {
    g_critical ("failed to parse request reply for '%s/get'", path);
    goto out;
  }

  if (zik2_request_reply_data_error (result)) {
    g_warning ("device reply with error for '%s/get'", path);
    zik2_request_reply_data_free (result);
    goto out;
  }

  *reply = result;
  ret = TRUE;

out:
  zik2_message_free (msg);

  if (answer)
    zik2_message_free (answer);

  return ret;
}

/* send a get request, parse reply and return info for type if found */
static gpointer
zik2_request_info (Zik2 * zik2, const gchar * path, GType type)
{
  Zik2RequestReplyData *reply = NULL;
  gpointer info;

  if (!zik2_get_and_parse_reply (zik2, path, &reply))
    return NULL;

  info = zik2_request_reply_data_find_node_info (reply, type);
  if (info == NULL)
    goto out;

  /* make a copy to free reply */
  info = g_boxed_copy (type, info);

out:
  zik2_request_reply_data_free (reply);

  return info;
}

static void
zik2_get_serial (Zik2 * zik2)
{
  Zik2SystemInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_PI_PATH,
      ZIK2_SYSTEM_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get serial");
    return;
  }

  g_free (zik2->priv->serial);
  zik2->priv->serial = g_strdup (info->pi);
  zik2_system_info_free (info);
}

static void
zik2_get_noise_control (Zik2 * zik2)
{
  Zik2NoiseControlInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_NOISE_CONTROL_ENABLED_PATH,
      ZIK2_NOISE_CONTROL_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get noise control status");
    return;
  }

  zik2->priv->noise_control = info->enabled;
  zik2_noise_control_info_free (info);
}

static gboolean
zik2_set_noise_control (Zik2 * zik2, gboolean active)
{
  Zik2Message *msg;
  gboolean ret;

  msg = zik2_message_new_request (ZIK2_API_AUDIO_NOISE_CONTROL_ENABLED_PATH, "set",
      active ? "true" : "false");
  ret = zik2_connection_send_message (zik2->conn, msg, NULL);
  zik2_message_free (msg);

  return ret;
}

static void
zik2_get_noise_control_mode_and_strength (Zik2 * zik2)
{
  Zik2NoiseControlInfo *info;
  GEnumClass *klass;
  GEnumValue *mode;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_NOISE_CONTROL_PATH,
      ZIK2_NOISE_CONTROL_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get noise control");
    return;
  }

  klass = G_ENUM_CLASS (g_type_class_peek (ZIK2_NOISE_CONTROL_MODE_TYPE));
  mode = g_enum_get_value_by_nick (klass, info->type);
  if (mode == NULL) {
    g_warning ("failed to get enum value associated with '%s'", info->type);
    goto out;
  }
  zik2->priv->noise_control_mode = mode->value;
  zik2->priv->noise_control_strength = info->value;

out:
  zik2_noise_control_info_free (info);
}

static gboolean
zik2_set_noise_control_mode_and_strength (Zik2 * zik2,
    Zik2NoiseControlMode mode, guint strength)
{
  Zik2Message *msg;
  gboolean ret;
  const gchar *type;
  gchar *args;

  switch (mode) {
    case ZIK2_NOISE_CONTROL_MODE_OFF:
      type = "off";
      break;
    case ZIK2_NOISE_CONTROL_MODE_ANC:
      type = "anc";
      break;
    case ZIK2_NOISE_CONTROL_MODE_AOC:
      type = "aoc";
      break;
    default:
      g_assert_not_reached ();
  }

  args = g_strdup_printf ("%s&value=%u", type, strength);
  msg = zik2_message_new_request (ZIK2_API_AUDIO_NOISE_CONTROL_PATH, "set", args);
  g_free (args);

  ret = zik2_connection_send_message (zik2->conn, msg, NULL);
  zik2_message_free (msg);

  return ret;
}

static void
zik2_get_software_version (Zik2 * zik2)
{
  Zik2SoftwareInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SOFTWARE_VERSION_PATH,
      ZIK2_SOFTWARE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get software info");
    return;
  }

  g_free (zik2->priv->software_version);
  zik2->priv->software_version = g_strdup (info->sip6);
  zik2_software_info_free (info);
}

static void
zik2_get_source (Zik2 * zik2)
{
  Zik2SourceInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_SOURCE_PATH,
      ZIK2_SOURCE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get audio source");
    return;
  }

  g_free (zik2->priv->source);
  zik2->priv->source = g_strdup (info->type);
  zik2_source_info_free (info);
}

static void
zik2_get_battery (Zik2 * zik2)
{
  Zik2BatteryInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_BATTERY_PATH,
      ZIK2_BATTERY_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get system battery");
    return;
  }

  g_free (zik2->priv->battery_state);
  zik2->priv->battery_state = g_strdup (info->state);
  zik2->priv->battery_percentage = info->percent;
  zik2_battery_info_free (info);
}

static void
zik2_get_volume (Zik2 * zik2)
{
  Zik2VolumeInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_VOLUME_PATH,
      ZIK2_VOLUME_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get audio volume");
    return;
  }

  zik2->priv->volume = info->volume;
  zik2_volume_info_free (info);
}

static void
zik2_get_head_detection (Zik2 * zik2)
{
  Zik2HeadDetectionInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_HEAD_DETECTION_ENABLED_PATH,
    ZIK2_HEAD_DETECTION_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get head detection");
    return;
  }

  zik2->priv->head_detection = info->enabled;
  zik2_head_detection_info_free (info);
}

static gboolean
zik2_set_head_detection (Zik2 * zik2, gboolean active)
{
  Zik2Message *msg;
  gboolean ret;

  msg = zik2_message_new_request (ZIK2_API_SYSTEM_HEAD_DETECTION_ENABLED_PATH,
      "set", active ? "true" : "false");
  ret = zik2_connection_send_message (zik2->conn, msg, NULL);
  zik2_message_free (msg);

  return ret;
}

static void
zik2_get_color (Zik2 * zik2)
{
  Zik2ColorInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_COLOR_PATH,
      ZIK2_COLOR_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get color");
    return;
  }

  zik2->priv->color = info->value;
  zik2_color_info_free (info);
}

static void
zik2_get_flight_mode (Zik2 * zik2)
{
  Zik2FlightModeInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_FLIGHT_MODE_PATH,
      ZIK2_FLIGHT_MODE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get flight mode");
    return;
  }

  zik2->priv->flight_mode = info->enabled;
  zik2_flight_mode_info_free (info);
}

static gboolean
zik2_set_flight_mode (Zik2 * zik2, gboolean active)
{
  Zik2Message *msg;
  gboolean ret;

  if (active)
    msg = zik2_message_new_request (ZIK2_API_FLIGHT_MODE_PATH, "enable", NULL);
  else
    msg = zik2_message_new_request (ZIK2_API_FLIGHT_MODE_PATH, "disable", NULL);

  ret = zik2_connection_send_message (zik2->conn, msg, NULL);
  zik2_message_free (msg);

  return ret;
}

static void
zik2_get_friendlyname (Zik2 * zik2)
{
  Zik2BluetoothInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_BLUETOOTH_FRIENDLY_NAME_PATH,
      ZIK2_BLUETOOTH_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get friendly name");
    return;
  }

  g_free (zik2->priv->friendlyname);
  zik2->priv->friendlyname = g_strdup (info->friendlyname);
  zik2_bluetooth_info_free (info);
}

static gboolean
zik2_set_friendlyname (Zik2 * zik2, const gchar * name)
{
  Zik2Message *msg;
  gboolean ret;

  msg = zik2_message_new_request (ZIK2_API_BLUETOOTH_FRIENDLY_NAME_PATH, "set",
      name);
  ret = zik2_connection_send_message (zik2->conn, msg, NULL);
  zik2_message_free (msg);

  return ret;
}

static void
zik2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec *pspec)
{
  Zik2 *zik2 = ZIK2 (object);
  Zik2Private *priv = zik2->priv;

  switch (prop_id) {
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, priv->address);
      break;
    case PROP_SERIAL:
      g_value_set_string (value, priv->serial);
      break;
    case PROP_SOFTWARE_VERSION:
      g_value_set_string (value, priv->software_version);
      break;
    case PROP_SOURCE:
      zik2_get_source (zik2);
      g_value_set_string (value, priv->source);
      break;
    case PROP_NOISE_CONTROL:
      g_value_set_boolean (value, priv->noise_control);
      break;
    case PROP_NOISE_CONTROL_MODE:
      g_value_set_enum (value, priv->noise_control_mode);
      break;
    case PROP_NOISE_CONTROL_STRENGTH:
      g_value_set_uint (value, priv->noise_control_strength);
      break;
    case PROP_BATTERY_STATE:
      zik2_get_battery (zik2);
      g_value_set_string (value, priv->battery_state);
      break;
    case PROP_BATTERY_PERCENT:
      zik2_get_battery (zik2);
      g_value_set_uint (value, priv->battery_percentage);
      break;
    case PROP_VOLUME:
      zik2_get_volume (zik2);
      g_value_set_uint (value, priv->volume);
      break;
    case PROP_HEAD_DETECTION:
      zik2_get_head_detection (zik2);
      g_value_set_boolean (value, priv->head_detection);
      break;
    case PROP_COLOR:
      zik2_get_color (zik2);
      g_value_set_enum (value, priv->color);
      break;
    case PROP_FLIGHT_MODE:
      zik2_get_flight_mode (zik2);
      g_value_set_boolean (value, priv->flight_mode);
      break;
    case PROP_FRIENDLYNAME:
      zik2_get_friendlyname (zik2);
      g_value_set_string (value, priv->friendlyname);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
zik2_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec *pspec)
{
  Zik2 *zik2 = ZIK2 (object);
  Zik2Private *priv = zik2->priv;

  switch (prop_id) {
    case PROP_NAME:
      priv->name = g_value_dup_string (value);
      break;
    case PROP_ADDRESS:
      priv->address = g_value_dup_string (value);
      break;
    case PROP_NOISE_CONTROL:
      {
        gboolean tmp;

        tmp = g_value_get_boolean (value);
        if (zik2_set_noise_control (zik2, tmp))
          priv->noise_control = tmp;
        else
          g_warning ("failed to set noise control enabled");
      }
      break;
    case PROP_NOISE_CONTROL_MODE:
      {
        Zik2NoiseControlMode tmp;

        tmp = g_value_get_enum (value);
        if (zik2_set_noise_control_mode_and_strength (zik2, tmp,
              priv->noise_control_strength))
          priv->noise_control_mode = tmp;
        else
           g_warning ("failed to set noise control mode");
      }
      break;
    case PROP_NOISE_CONTROL_STRENGTH:
      {
        guint tmp;

        tmp = g_value_get_uint (value);
        if (zik2_set_noise_control_mode_and_strength (zik2,
              priv->noise_control_mode, tmp))
          priv->noise_control_strength = tmp;
        else
          g_warning ("failed to set noise control strength");
      }
      break;
    case PROP_HEAD_DETECTION:
      {
        guint tmp;

        tmp = g_value_get_boolean (value);
        if (zik2_set_head_detection (zik2, tmp))
          priv->head_detection = tmp;
        else
          g_warning ("failed to enable/disable head detection");
      }
      break;
    case PROP_FLIGHT_MODE:
      {
        gboolean tmp;

        tmp = g_value_get_boolean (value);
        if (zik2_set_flight_mode (zik2, tmp))
          priv->flight_mode = tmp;
        else
          g_warning ("failed to enable/disable flight mode");
      }
      break;
    case PROP_FRIENDLYNAME:
      {
        const gchar *tmp;

        tmp = g_value_get_string (value);
        if (zik2_set_friendlyname (zik2, tmp)) {
          g_free (priv->friendlyname);
          priv->friendlyname = g_strdup (tmp);
        } else {
          g_warning ("failed to set friendlyname");
        }
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* @conn: (transfer full) */
Zik2 *
zik2_new (const gchar * name, const gchar * address, Zik2Connection * conn)
{
  Zik2 *zik2;

  zik2 = g_object_new (ZIK2_TYPE, "name", name, "address", address, NULL);
  zik2->conn = conn;

  /* sync with devices */
  zik2_get_serial (zik2);
  zik2_get_noise_control (zik2);
  zik2_get_noise_control_mode_and_strength (zik2);
  zik2_get_software_version (zik2);
  zik2_get_source (zik2);
  zik2_get_color (zik2);

  return zik2;
}
