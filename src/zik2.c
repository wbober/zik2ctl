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
#include "zikconnection.h"
#include "zikmessage.h"
#include "zik2info.h"
#include "zik2api.h"

#define UNKNOWN_STR "unknown"
#define DEFAULT_NOISE_CONTROL_STRENGTH 1
#define DEFAULT_AUTO_POWER_OFF_TIMEOUT 0

enum
{
  PROP_0,
  PROP_NAME,
  PROP_ADDRESS,
  PROP_CONNECTION,
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
  PROP_SOUND_EFFECT,
  PROP_SOUND_EFFECT_ROOM,
  PROP_SOUND_EFFECT_ANGLE,
  PROP_AUTO_CONNECTION,
  PROP_TRACK_METADATA,
  PROP_EQUALIZER,
  PROP_SMART_AUDIO_TUNE,
  PROP_AUTO_POWER_OFF_TIMEOUT,
  PROP_TTS,
};

struct _Zik2Private
{
  gchar *name;
  gchar *address;

  ZikConnection *conn;

  /* audio */
  gboolean noise_control;
  Zik2NoiseControlMode noise_control_mode;
  guint noise_control_strength;
  gchar *source;
  guint volume;
  gboolean sound_effect;
  Zik2SoundEffectRoom sound_effect_room;
  Zik2SoundEffectAngle sound_effect_angle;
  Zik2MetadataInfo *track_metadata;
  gboolean equalizer;
  gboolean smart_audio_tune;

  /* software */
  gchar *software_version;
  gboolean tts;

  /* system */
  gchar *battery_state;
  guint battery_percentage;
  Zik2Color color;
  gboolean head_detection;
  gchar *serial;
  gboolean auto_connection;
  guint auto_power_off_timeout;

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

#define ZIK2_SOUND_EFFECT_ROOM_TYPE (zik2_sound_effect_room_get_type ())
static GType
zik2_sound_effect_room_get_type (void)
{
  static volatile GType type;
  static const GEnumValue rooms[] = {
    { ZIK2_SOUND_EFFECT_ROOM_UNKNOWN, "Unknown", "unknown" },
    { ZIK2_SOUND_EFFECT_ROOM_SILENT, "Silent", "silent" },
    { ZIK2_SOUND_EFFECT_ROOM_LIVING, "Living", "living" },
    { ZIK2_SOUND_EFFECT_ROOM_JAZZ, "Jazz", "jazz" },
    { ZIK2_SOUND_EFFECT_ROOM_CONCERT, "Concert", "concert" },
    { 0, NULL, NULL }
  };

  if (g_once_init_enter (&type)) {
    GType _type = g_enum_register_static ("ZikSoundEffectRoom", rooms);
    g_once_init_leave (&type, _type);
  }

  return type;
}

Zik2SoundEffectRoom
zik2_sound_effect_room_from_string (const gchar * str)
{
  GEnumClass *klass;
  GEnumValue *value;

  klass = G_ENUM_CLASS (g_type_class_peek (ZIK2_SOUND_EFFECT_ROOM_TYPE));
  value = g_enum_get_value_by_nick (klass, str);
  if (value == NULL)
    return ZIK2_SOUND_EFFECT_ROOM_UNKNOWN;

  return value->value;
}

const gchar *
zik2_sound_effect_room_name (Zik2SoundEffectRoom room)
{
  GEnumClass *klass;
  GEnumValue *value;

  klass = G_ENUM_CLASS (g_type_class_peek (ZIK2_SOUND_EFFECT_ROOM_TYPE));
  value = g_enum_get_value (klass, room);
  if (value == NULL)
    return "unknown";

  return value->value_nick;
}

#define ZIK2_SOUND_EFFECT_ANGLE_TYPE (zik2_sound_effect_angle_get_type ())
static GType
zik2_sound_effect_angle_get_type (void)
{
  static volatile GType type;
  static const GEnumValue angles[] = {
    { ZIK2_SOUND_EFFECT_ANGLE_UNKNOWN, "0", "0" },
    { ZIK2_SOUND_EFFECT_ANGLE_30, "30", "30" },
    { ZIK2_SOUND_EFFECT_ANGLE_60, "60", "60" },
    { ZIK2_SOUND_EFFECT_ANGLE_90, "90", "90" },
    { ZIK2_SOUND_EFFECT_ANGLE_120, "120", "120" },
    { ZIK2_SOUND_EFFECT_ANGLE_150, "150", "150" },
    { ZIK2_SOUND_EFFECT_ANGLE_180, "180", "150" },
    { 0, NULL, NULL }
  };

  if (g_once_init_enter (&type)) {
    GType _type = g_enum_register_static ("ZikSoundEffectAngle", angles);
    g_once_init_leave (&type, _type);
  }

  return type;
}

static inline void
_string_replace (gchar ** old, const gchar * new)
{
  g_free (*old);
  *old = g_strdup (new);
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

  g_object_class_install_property (gobject_class, PROP_CONNECTION,
      g_param_spec_boxed ("connection", "Connection", "Zik connection object",
          ZIK_CONNECTION_TYPE,
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

  g_object_class_install_property (gobject_class, PROP_SOUND_EFFECT,
      g_param_spec_boolean ("sound-effect", "Sound effect",
          "Whether sound effect (Concert Hall) is active or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOUND_EFFECT_ROOM,
      g_param_spec_enum ("sound-effect-room", "Sound effect room",
          "Set the room type used by sound effect", ZIK2_SOUND_EFFECT_ROOM_TYPE,
          ZIK2_SOUND_EFFECT_ROOM_UNKNOWN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SOUND_EFFECT_ANGLE,
      g_param_spec_enum ("sound-effect-angle", "Sound effect angle",
          "Set the sound effect angle", ZIK2_SOUND_EFFECT_ANGLE_TYPE,
          ZIK2_SOUND_EFFECT_ANGLE_120,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTO_CONNECTION,
      g_param_spec_boolean ("auto-connection", "Auto connection",
          "Whether device should connect automatically", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* GVariant dictionnary: a{sv}
   * playing --> <boolean>
   * title --> <string>
   * artist --> <string>
   * album --> <string>
   * genre --> <string>
   */
  g_object_class_install_property (gobject_class, PROP_TRACK_METADATA,
      g_param_spec_variant ("track-metadata", "Track metadata",
        "Current metadata of the played song", G_VARIANT_TYPE_VARDICT,
        NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EQUALIZER,
      g_param_spec_boolean ("equalizer", "Equalizer",
          "Whether equalizer is active or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SMART_AUDIO_TUNE,
      g_param_spec_boolean ("smart-audio-tune", "Smart audio tune",
          "Whether smart audio tune is active or not", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTO_POWER_OFF_TIMEOUT,
      g_param_spec_uint ("auto-power-off-timeout", "Auto power off timeout",
        "Power off device after specified time in minutes (0 means disable)",
        0, 60, DEFAULT_AUTO_POWER_OFF_TIMEOUT,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TTS,
      g_param_spec_boolean ("tts", "TTS",
          "Whether text to speech is active or not", FALSE,
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

  if (priv->track_metadata)
    zik2_metadata_info_unref (priv->track_metadata);

  if (priv->conn)
    zik_connection_unref (priv->conn);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* reply: allow-none */
static gboolean
zik2_do_request (Zik2 * zik2, const gchar * path, const gchar * method,
    const gchar * args, ZikRequestReplyData ** reply_data)
{
  ZikMessage *msg;
  ZikMessage *reply = NULL;
  ZikRequestReplyData *result;
  gboolean ret = FALSE;

  msg = zik_message_new_request (path, method, args);

  if (!zik_connection_send_message (zik2->priv->conn, msg, &reply)) {
    g_critical ("failed to send request '%s/%s with args %s'", path, method,
        args);
    goto out;
  }

  if (!zik_message_parse_request_reply (reply, &result)) {
    g_critical ("failed to parse request reply '%s/%s with args %s'", path,
        method, args);
    goto out;
  }

  if (zik_request_reply_data_error (result)) {
    g_warning ("device reply with error '%s/%s with args %s'", path, method,
        args);
    zik_request_reply_data_free (result);
    goto out;
  }

  if (reply_data)
    *reply_data = result;
  else
    zik_request_reply_data_free (result);

  ret = TRUE;

out:
  zik_message_free (msg);

  if (reply)
    zik_message_free (reply);

  return ret;
}

/* send a get request, parse reply and return info for type if found */
static gpointer
zik2_request_info (Zik2 * zik2, const gchar * path, GType type)
{
  ZikRequestReplyData *reply = NULL;
  gpointer info;

  if (!zik2_do_request (zik2, path, "get", NULL, &reply))
    return NULL;

  info = zik_request_reply_data_find_node_info (reply, type);
  if (info == NULL)
    goto out;

  /* make a copy to free reply */
  info = g_boxed_copy (type, info);

out:
  zik_request_reply_data_free (reply);

  return info;
}

static void
zik2_sync_serial (Zik2 * zik2)
{
  Zik2SystemInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_PI_PATH,
      ZIK2_SYSTEM_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get serial");
    return;
  }

  _string_replace (&zik2->priv->serial, info->pi);
  zik2_system_info_unref (info);
}

static void
zik2_sync_noise_control (Zik2 * zik2)
{
  Zik2NoiseControlInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_NOISE_CONTROL_ENABLED_PATH,
      ZIK2_NOISE_CONTROL_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get noise control status");
    return;
  }

  zik2->priv->noise_control = info->enabled;
  zik2_noise_control_info_unref (info);
}

static void
zik2_sync_noise_control_mode_and_strength (Zik2 * zik2)
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
  zik2_noise_control_info_unref (info);
}

static gboolean
zik2_set_noise_control_mode_and_strength (Zik2 * zik2,
    Zik2NoiseControlMode mode, guint strength)
{
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
  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_NOISE_CONTROL_PATH, "set", args,
      NULL);
  g_free (args);

  return ret;
}

static void
zik2_sync_software_version (Zik2 * zik2)
{
  Zik2SoftwareInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SOFTWARE_VERSION_PATH,
      ZIK2_SOFTWARE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get software info");
    return;
  }

  _string_replace (&zik2->priv->software_version, info->sip6);
  zik2_software_info_unref (info);
}

static void
zik2_sync_source (Zik2 * zik2)
{
  Zik2SourceInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_SOURCE_PATH,
      ZIK2_SOURCE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get audio source");
    return;
  }

  _string_replace (&zik2->priv->source, info->type);
  zik2_source_info_unref (info);
}

static void
zik2_sync_battery (Zik2 * zik2)
{
  Zik2BatteryInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_BATTERY_PATH,
      ZIK2_BATTERY_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get system battery");
    return;
  }

  _string_replace (&zik2->priv->battery_state, info->state);
  zik2->priv->battery_percentage = info->percent;
  zik2_battery_info_unref (info);
}

static void
zik2_sync_volume (Zik2 * zik2)
{
  Zik2VolumeInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_VOLUME_PATH,
      ZIK2_VOLUME_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get audio volume");
    return;
  }

  zik2->priv->volume = info->volume;
  zik2_volume_info_unref (info);
}

static void
zik2_sync_head_detection (Zik2 * zik2)
{
  Zik2HeadDetectionInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_HEAD_DETECTION_ENABLED_PATH,
    ZIK2_HEAD_DETECTION_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get head detection");
    return;
  }

  zik2->priv->head_detection = info->enabled;
  zik2_head_detection_info_unref (info);
}

static void
zik2_sync_color (Zik2 * zik2)
{
  Zik2ColorInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_COLOR_PATH,
      ZIK2_COLOR_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get color");
    return;
  }

  zik2->priv->color = info->value;
  zik2_color_info_unref (info);
}

static void
zik2_sync_flight_mode (Zik2 * zik2)
{
  Zik2FlightModeInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_FLIGHT_MODE_PATH,
      ZIK2_FLIGHT_MODE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get flight mode");
    return;
  }

  zik2->priv->flight_mode = info->enabled;
  zik2_flight_mode_info_unref (info);
}

static void
zik2_sync_friendlyname (Zik2 * zik2)
{
  Zik2BluetoothInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_BLUETOOTH_FRIENDLY_NAME_PATH,
      ZIK2_BLUETOOTH_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get friendly name");
    return;
  }

  _string_replace (&zik2->priv->friendlyname, info->friendlyname);
  zik2_bluetooth_info_unref (info);
}

static void
zik2_sync_sound_effect (Zik2 * zik2)
{
  Zik2SoundEffectInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_SOUND_EFFECT_PATH,
      ZIK2_SOUND_EFFECT_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get sound effect info");
    return;
  }

  zik2->priv->sound_effect = info->enabled;
  zik2->priv->sound_effect_room =
      zik2_sound_effect_room_from_string (info->room_size);
  zik2->priv->sound_effect_angle = info->angle;
  zik2_sound_effect_info_unref (info);
}

static void
zik2_sync_auto_connection (Zik2 * zik2)
{
  Zik2AutoConnectionInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_AUTO_CONNECTION_ENABLED_PATH,
      ZIK2_AUTO_CONNECTION_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get auto-connection info");
    return;
  }

  zik2->priv->auto_connection = info->enabled;
  zik2_auto_connection_info_unref (info);
}

static void
zik2_sync_track_metadata (Zik2 * zik2)
{
  Zik2MetadataInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_TRACK_METADATA_PATH,
      ZIK2_METADATA_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get track metadata");
    return;
  }

  if (zik2->priv->track_metadata)
    zik2_metadata_info_unref (zik2->priv->track_metadata);

  zik2->priv->track_metadata = info;
}

static void
zik2_sync_equalizer (Zik2 * zik2)
{
  Zik2EqualizerInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_EQUALIZER_ENABLED_PATH,
      ZIK2_EQUALIZER_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get equalizer status");
    return;
  }

  zik2->priv->equalizer = info->enabled;
  zik2_equalizer_info_unref (info);
}

static void
zik2_sync_smart_audio_tune (Zik2 * zik2)
{
  Zik2SmartAudioTuneInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_AUDIO_SMART_AUDIO_TUNE_PATH,
      ZIK2_SMART_AUDIO_TUNE_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get smart audio tune status");
    return;
  }

  zik2->priv->smart_audio_tune = info->enabled;
  zik2_smart_audio_tune_info_unref (info);
}

static void
zik2_sync_auto_power_off (Zik2 * zik2)
{
  Zik2AutoPowerOffInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SYSTEM_AUTO_POWER_OFF_PATH,
      ZIK2_AUTO_POWER_OFF_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get auto-power-off status");
    return;
  }

  zik2->priv->auto_power_off_timeout = info->value;
  zik2_auto_power_off_info_unref (info);
}

static void
zik2_sync_tts (Zik2 * zik2)
{
  Zik2TTSInfo *info;

  info = zik2_request_info (zik2, ZIK2_API_SOFTWARE_TTS_PATH,
      ZIK2_TTS_INFO_TYPE);
  if (info == NULL) {
    g_warning ("failed to get tts status");
    return;
  }

  zik2->priv->tts = info->enabled;
  zik2_tts_info_unref (info);
}

static void
zik2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec *pspec)
{
  Zik2 *zik2 = ZIK2 (object);

  switch (prop_id) {
    case PROP_NAME:
      g_value_set_string (value, zik2_get_name (zik2));
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, zik2_get_address (zik2));
      break;
    case PROP_CONNECTION:
      g_value_set_boxed (value, zik2_get_connection (zik2));
      break;
    case PROP_SERIAL:
      g_value_set_string (value, zik2_get_serial (zik2));
      break;
    case PROP_SOFTWARE_VERSION:
      g_value_set_string (value, zik2_get_software_version (zik2));
      break;
    case PROP_SOURCE:
      g_value_set_string (value, zik2_get_source (zik2));
      break;
    case PROP_NOISE_CONTROL:
      g_value_set_boolean (value, zik2_is_noise_control_active (zik2));
      break;
    case PROP_NOISE_CONTROL_MODE:
      g_value_set_enum (value, zik2_get_noise_control_mode (zik2));
      break;
    case PROP_NOISE_CONTROL_STRENGTH:
      g_value_set_uint (value, zik2_get_noise_control_strength (zik2));
      break;
    case PROP_BATTERY_STATE:
      g_value_set_string (value, zik2_get_battery_state (zik2));
      break;
    case PROP_BATTERY_PERCENT:
      g_value_set_uint (value, zik2_get_battery_percentage (zik2));
      break;
    case PROP_VOLUME:
      g_value_set_uint (value, zik2_get_volume (zik2));
      break;
    case PROP_HEAD_DETECTION:
      g_value_set_boolean (value, zik2_is_head_detection_active (zik2));
      break;
    case PROP_COLOR:
      g_value_set_enum (value, zik2_get_color (zik2));
      break;
    case PROP_FLIGHT_MODE:
      g_value_set_boolean (value, zik2_is_flight_mode_active (zik2));
      break;
    case PROP_FRIENDLYNAME:
      g_value_set_string (value, zik2_get_friendlyname (zik2));
      break;
    case PROP_SOUND_EFFECT:
      g_value_set_boolean (value, zik2_is_sound_effect_active (zik2));
      break;
    case PROP_SOUND_EFFECT_ROOM:
      g_value_set_enum (value, zik2_get_sound_effect_room (zik2));
      break;
    case PROP_SOUND_EFFECT_ANGLE:
      g_value_set_enum (value, zik2_get_sound_effect_angle (zik2));
      break;
    case PROP_AUTO_CONNECTION:
      g_value_set_boolean (value, zik2_is_auto_connection_active (zik2));
    case PROP_TRACK_METADATA:
      {
        const Zik2MetadataInfo *info;
        GVariant *var;
        const gchar *var_format =
            "{%s:<%b>, %s:<%s>, %s:<%s>, %s:<%s>, %s:<%s>}";

        zik2_sync_track_metadata (zik2);
        info = zik2->priv->track_metadata;
        if (info != NULL) {
          var = g_variant_new_parsed (var_format, "playing", info->playing,
              "title", info->title, "artist", info->artist, "album",
              info->album, "genre", info->genre);
        } else {
          var = g_variant_new_parsed (var_format, "playing", FALSE, "title",
              NULL, "artist", NULL, "album", NULL, "genre", NULL);
        }

        g_value_set_variant (value, var);
        break;
      }
    case PROP_EQUALIZER:
      g_value_set_boolean (value, zik2_is_equalizer_active (zik2));
      break;
    case PROP_SMART_AUDIO_TUNE:
      g_value_set_boolean (value, zik2_is_smart_audio_tune_active (zik2));
      break;
    case PROP_AUTO_POWER_OFF_TIMEOUT:
      g_value_set_uint (value, zik2_get_auto_power_off_timeout (zik2));
      break;
    case PROP_TTS:
      g_value_set_boolean (value, zik2_is_tts_active (zik2));
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
    case PROP_CONNECTION:
      priv->conn = g_value_dup_boxed (value);
      break;
    case PROP_NOISE_CONTROL:
      if (!zik2_set_noise_control_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to set noise control enabled");

      break;
    case PROP_NOISE_CONTROL_MODE:
      if (!zik2_set_noise_control_mode (zik2, g_value_get_enum (value)))
        g_warning ("failed to set noise control mode");

      break;
    case PROP_NOISE_CONTROL_STRENGTH:
      if (!zik2_set_noise_control_strength (zik2, g_value_get_uint (value)))
        g_warning ("failed to set noise control strength");

      break;
    case PROP_HEAD_DETECTION:
      if (!zik2_set_head_detection_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable head detection");

      break;
    case PROP_FLIGHT_MODE:
      if (!zik2_set_flight_mode_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable flight mode");

      break;
    case PROP_FRIENDLYNAME:
      if (!zik2_set_friendlyname (zik2, g_value_get_string (value)))
        g_warning ("failed to set friendlyname");

      break;
    case PROP_SOUND_EFFECT:
      if (!zik2_set_sound_effect_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable sound effect");

      break;
    case PROP_SOUND_EFFECT_ROOM:
      if (!zik2_set_sound_effect_room (zik2, g_value_get_enum (value)))
        g_warning ("failed to enable/disable sound effect room");

      break;
    case PROP_SOUND_EFFECT_ANGLE:
      if (!zik2_set_sound_effect_angle (zik2, g_value_get_enum (value)))
        g_warning ("failed to enable/disable sound effect angle");

      break;
    case PROP_AUTO_CONNECTION:
      if (!zik2_set_auto_connection_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable auto-connection");

      break;
    case PROP_EQUALIZER:
      if (!zik2_set_equalizer_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable equalizer");

      break;

    case PROP_SMART_AUDIO_TUNE:
      if (!zik2_set_smart_audio_tune_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enable/disable smart audio tune");

      break;
    case PROP_AUTO_POWER_OFF_TIMEOUT:
      if (!zik2_set_auto_power_off_timeout (zik2, g_value_get_uint (value)))
        g_warning ("failed to set auto power off timeout");

      break;
    case PROP_TTS:
      if (!zik2_set_tts_active (zik2, g_value_get_boolean (value)))
        g_warning ("failed to enabled/disable tts");

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Static properties are the one which not change at all or only change
 * with user action */
static void
zik2_sync_static_properties (Zik2 * zik2)
{
  /* audio */
  zik2_sync_noise_control (zik2);
  zik2_sync_noise_control_mode_and_strength (zik2);
  zik2_sync_sound_effect (zik2);
  zik2_sync_equalizer (zik2);
  zik2_sync_smart_audio_tune (zik2);

  /* software and system */
  zik2_sync_software_version (zik2);
  zik2_sync_color (zik2);
  zik2_sync_serial (zik2);
  zik2_sync_head_detection (zik2);
  zik2_sync_flight_mode (zik2);
  zik2_sync_friendlyname (zik2);
  zik2_sync_auto_connection (zik2);
  zik2_sync_auto_power_off (zik2);
  zik2_sync_tts (zik2);
}

/* @conn: (transfer full) */
Zik2 *
zik2_new (const gchar * name, const gchar * address, ZikConnection * conn)
{
  Zik2 *zik2;

  zik2 = g_object_new (ZIK2_TYPE, "name", name, "address", address,
      "connection", conn, NULL);

  zik2_sync_static_properties (zik2);

  return zik2;
}

const gchar *
zik2_get_name (Zik2 * zik2)
{
  return zik2->priv->name;
}

const gchar *
zik2_get_address (Zik2 * zik2)
{
  return zik2->priv->address;
}

/* transfer none */
ZikConnection *
zik2_get_connection (Zik2 * zik2)
{
  return zik2->priv->conn;
}

gboolean
zik2_is_noise_control_active (Zik2 * zik2)
{
  return zik2->priv->noise_control;
}

gboolean
zik2_set_noise_control_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_NOISE_CONTROL_ENABLED_PATH, "set",
      active ? "true" : "false", NULL);
  if (ret) {
    /* resync all noise controls mode and strength because their are modified
     * by set_active call */
    zik2_sync_noise_control_mode_and_strength (zik2);
    zik2->priv->noise_control = active;
  }

  return ret;
}

Zik2NoiseControlMode
zik2_get_noise_control_mode (Zik2 * zik2)
{
  return zik2->priv->noise_control_mode;
}

gboolean
zik2_set_noise_control_mode (Zik2 * zik2, Zik2NoiseControlMode mode)
{
  gboolean ret;

  ret = zik2_set_noise_control_mode_and_strength (zik2, mode,
      zik2->priv->noise_control_strength);
  if (ret) {
    /* resync noise control status as it is modified by this call */
    zik2_sync_noise_control (zik2);
    zik2_sync_noise_control_mode_and_strength (zik2);
    zik2->priv->noise_control_mode = mode;
  }

  return ret;
}

guint
zik2_get_noise_control_strength (Zik2 * zik2)
{
  return zik2->priv->noise_control_strength;
}

gboolean
zik2_set_noise_control_strength (Zik2 * zik2, guint strength)
{
  gboolean ret;

  /* Setting strength while noise control is off has no effect, but device
   * doesn't reply with error, so make return false here. */
  if (!zik2->priv->noise_control ||
      zik2->priv->noise_control_mode == ZIK2_NOISE_CONTROL_MODE_OFF)
    return FALSE;

  ret = zik2_set_noise_control_mode_and_strength (zik2,
      zik2->priv->noise_control_mode, strength);
  if (ret)
    zik2->priv->noise_control_strength = strength;

  return ret;
}

const gchar *
zik2_get_source (Zik2 * zik2)
{
  zik2_sync_source (zik2);
  return zik2->priv->source;
}

guint
zik2_get_volume (Zik2 * zik2)
{
  zik2_sync_volume (zik2);
  return zik2->priv->volume;
}

gboolean
zik2_is_sound_effect_active (Zik2 * zik2)
{
  return zik2->priv->sound_effect;
}

gboolean
zik2_set_sound_effect_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_SOUND_EFFECT_ENABLED_PATH, "set",
      active ? "true" : "false", NULL);
  if (ret)
    zik2->priv->sound_effect = active;

  return ret;
}

Zik2SoundEffectRoom
zik2_get_sound_effect_room (Zik2 * zik2)
{
  return zik2->priv->sound_effect_room;
}

gboolean
zik2_set_sound_effect_room (Zik2 * zik2, Zik2SoundEffectRoom room)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_SOUND_EFFECT_ROOM_SIZE_PATH,
      "set", zik2_sound_effect_room_name (room), NULL);
  if (ret) {
    zik2_sync_sound_effect (zik2);
    zik2->priv->sound_effect_room = room;
  }

  return ret;
}

Zik2SoundEffectAngle
zik2_get_sound_effect_angle (Zik2 * zik2)
{
  return zik2->priv->sound_effect_angle;
}

gboolean
zik2_set_sound_effect_angle (Zik2 * zik2, Zik2SoundEffectAngle angle)
{
  gboolean ret;
  gchar *args;

  args = g_strdup_printf ("%u", angle);
  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_SOUND_EFFECT_ANGLE_PATH, "set",
      args, NULL);
  if (ret) {
    zik2_sync_sound_effect (zik2);
    zik2->priv->sound_effect_angle = angle;
  }

  g_free (args);
  return ret;
}

const gchar *
zik2_get_software_version (Zik2 * zik2)
{
  return zik2->priv->software_version;
}

const gchar *
zik2_get_battery_state (Zik2 * zik2)
{
  zik2_sync_battery (zik2);
  return zik2->priv->battery_state;
}

guint
zik2_get_battery_percentage (Zik2 * zik2)
{
  zik2_sync_battery (zik2);
  return zik2->priv->battery_percentage;
}

Zik2Color
zik2_get_color (Zik2 * zik2)
{
  return zik2->priv->color;
}

gboolean
zik2_is_head_detection_active (Zik2 * zik2)
{
  return zik2->priv->head_detection;
}

gboolean
zik2_set_head_detection_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_SYSTEM_HEAD_DETECTION_ENABLED_PATH,
      "set", active ? "true" : "false", NULL);
  if (ret)
    zik2->priv->head_detection = active;

  return ret;
}

const gchar *
zik2_get_serial (Zik2 * zik2)
{
  return zik2->priv->serial;
}

gboolean
zik2_is_flight_mode_active (Zik2 * zik2)
{
  return zik2->priv->flight_mode;
}

gboolean
zik2_set_flight_mode_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;
  const gchar *method;

  if (active)
    method = "enable";
  else
    method = "disable";

  ret = zik2_do_request (zik2, ZIK2_API_FLIGHT_MODE_PATH, method, NULL, NULL);
  if (ret)
    zik2->priv->flight_mode = active;

  return ret;
}

const gchar *
zik2_get_friendlyname (Zik2 * zik2)
{
  return zik2->priv->friendlyname;
}

gboolean
zik2_set_friendlyname (Zik2 * zik2, const gchar * name)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_BLUETOOTH_FRIENDLY_NAME_PATH, "set",
      name, NULL);
  if (ret)
    _string_replace (&zik2->priv->friendlyname, name);

  return ret;
}

gboolean
zik2_is_auto_connection_active (Zik2 * zik2)
{
  return zik2->priv->auto_connection;
}

gboolean
zik2_set_auto_connection_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_SYSTEM_AUTO_CONNECTION_ENABLED_PATH,
      "set", active ? "true" : "false", NULL);
  if (ret)
    zik2->priv->auto_connection = active;

  return ret;
}

void
zik2_get_track_metadata (Zik2 * zik2, gboolean * playing, const gchar ** title,
    const gchar ** artist, const gchar ** album, const gchar ** genre)
{
  const Zik2MetadataInfo *info;

  zik2_sync_track_metadata (zik2);
  info = zik2->priv->track_metadata;
  if (info == NULL)
    return;

  if (playing)
    *playing = info->playing;

  if (title)
    *title = info->title;

  if (artist)
    *artist = info->artist;

  if (album)
    *album = info->album;

  if (genre)
    *genre = info->genre;
}

gboolean
zik2_is_equalizer_active (Zik2 * zik2)
{
  return zik2->priv->equalizer;
}

gboolean
zik2_set_equalizer_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_EQUALIZER_ENABLED_PATH, "set",
      active ? "true" : "false", NULL);
  if (ret)
    zik2->priv->equalizer = active;

  return ret;
}

gboolean
zik2_is_smart_audio_tune_active (Zik2 * zik2)
{
  return zik2->priv->smart_audio_tune;
}

gboolean
zik2_set_smart_audio_tune_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;

  ret = zik2_do_request (zik2, ZIK2_API_AUDIO_SMART_AUDIO_TUNE_PATH, "set",
      active ? "true" : "false", NULL);
  if (ret)
    zik2->priv->smart_audio_tune = active;

  return ret;
}

guint
zik2_get_auto_power_off_timeout (Zik2 * zik2)
{
  return zik2->priv->auto_power_off_timeout;
}

gboolean
zik2_set_auto_power_off_timeout (Zik2 * zik2, guint timeout_min)
{
  gboolean ret;
  gchar *args;

  args = g_strdup_printf ("%u", timeout_min);
  ret = zik2_do_request (zik2, ZIK2_API_SYSTEM_AUTO_POWER_OFF_PATH, "set", args,
      NULL);
  if (ret)
    zik2->priv->auto_power_off_timeout = timeout_min;

  g_free (args);
  return ret;
}

gboolean
zik2_is_tts_active (Zik2 * zik2)
{
  return zik2->priv->tts;
}

gboolean
zik2_set_tts_active (Zik2 * zik2, gboolean active)
{
  gboolean ret;
  const gchar *method;

  if (active)
    method = "enable";
  else
    method = "disable";

  ret = zik2_do_request (zik2, ZIK2_API_SOFTWARE_TTS_PATH, method, NULL, NULL);
  if (ret)
    zik2->priv->tts = active;

  return ret;
}
