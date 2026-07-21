#ifdef _DOS

#include "setup_strings.h"

// Spanish accents are encoded with octal escapes that map to the same byte in
// both CP437 and CP850 (the DOS BIOS text codepages SETUP renders through):
//   a-acute \240   e-acute \202   i-acute \241   o-acute \242   u-acute \243
//   n-tilde \244   u-diaer \201   inv? \250      inv! \255
// Uppercase accented vowels are avoided (CP437 has no glyph for them).

struct SetupStringPair
{
	const char* en;
	const char* es;
};

static const SetupStringPair kStrings[STR_COUNT] =
{
	/* STR_TITLE */
	{ "ADP DOS SETUP",                 "Configuraci\242n de ADP (DOS)" },
	/* STR_STATUS_SOUND  (args: video name, card name, rate/1000) */
	{ "Video: %s     Sound: %s, %u kHz",
	  "V\241deo: %s     Sonido: %s, %u kHz" },
	/* STR_STATUS_NOSOUND */
	{ "Video: %s     Sound: disabled",
	  "V\241deo: %s     Sonido: desactivado" },
	/* STR_MENU_HEADER */
	{ "Configuration",                 "Configuraci\242n" },
	/* STR_MENU_VIDEO */
	{ "Configure video",               "Configurar v\241deo" },
	/* STR_MENU_SOUND */
	{ "Configure sound",               "Configurar sonido" },
	/* STR_MENU_TEST */
	{ "Sound Test",                    "Probar sonido" },
	/* STR_MENU_SAVE */
	{ "Save settings and exit",        "Guardar y salir" },
	/* STR_MENU_EXIT */
	{ "Exit without saving",           "Salir sin guardar" },

	/* STR_VIDEO_TITLE */
	{ "Video mode",                    "Modo de v\241deo" },
	/* STR_VIDEO_AUTODETECT */
	{ "Autodetect graphics adapter",   "Detectar tarjeta gr\240fica" },
	/* STR_VIDEO_CGA */
	{ "CGA - 4 colours",               "CGA - 4 colores" },
	/* STR_VIDEO_EGA */
	{ "EGA - 16 colours",              "EGA - 16 colores" },
	/* STR_VIDEO_VGA16 */
	{ "VGA - 16 colours",              "VGA - 16 colores" },
	/* STR_VIDEO_VGA */
	{ "VGA - 320x200, 256 colours",    "VGA - 320x200, 256 colores" },
	/* STR_VIDEO_SVGA */
	{ "SVGA - 640x400, 256 colours",   "SVGA - 640x400, 256 colores" },
	/* STR_VIDEO_DETECT_TITLE */
	{ "Video detection",               "Detecci\242n de v\241deo" },
	/* STR_VIDEO_DETECT_CONFIRM */
	{ "The BIOS hardware check may be unreliable on some old systems.\nRun autodetection now?",
	  "La comprobaci\242n por BIOS puede no ser fiable en equipos antiguos.\n\250Ejecutar la detecci\242n autom\240tica ahora?" },
	/* STR_VIDEO_DETECT_NONE */
	{ "No compatible adapter was detected for this game's data files.",
	  "No se detect\242 ning\243n adaptador compatible con los datos de este juego." },
	/* STR_VIDEO_DETECT_OK */
	{ "Selected %s",                   "Ha seleccionado %s" },

	/* STR_QUALITY_TITLE */
	{ "Playback quality",              "Calidad de reproducci\242n" },

	/* STR_SB_PORT */
	{ "Sound Blaster base port",       "Puerto base" },
	/* STR_SB_IRQ */
	{ "Sound Blaster IRQ",             "IRQ" },
	/* STR_SB_DMA8 */
	{ "8-bit DMA channel",             "Canal DMA de 8 bits" },
	/* STR_SB_DMA16 */
	{ "16-bit DMA channel",            "Canal DMA de 16 bits" },
	/* STR_SB_DETECT_TITLE */
	{ "Sound card detection",          "Detecci\242n de tarjeta de sonido" },
	/* STR_SB_DETECT_CONFIRM */
	{ "SETUP will try to detect your sound card.\nRun the detection now?",
	  "SETUP intentar\240 detectar su tarjeta de sonido.\n\250Ejecutar la detecci\242n ahora?" },
	/* STR_SB_DETECT_NONE */
	{ "No sound card was detected.",
	  "No se detect\242 ninguna tarjeta de sonido." },
	/* STR_SB_DETECT_OK */
	{ "Sound Blaster resources were detected and copied to the setup file.",
	  "Se detectaron los recursos del Sound Blaster y se guardaron en la configuraci\242n." },

	/* STR_SOUND_TITLE */
	{ "Sound setup",                   "Configuraci\242n de sonido" },
	/* STR_SOUND_ENABLE_FIRST */
	{ "Select a sound card first.",    "Seleccione primero una tarjeta de sonido." },

	/* STR_CARD_TITLE */
	{ "Sound card",                    "Tarjeta de sonido" },
	/* STR_CARD_NONE */
	{ "No sound",                      "Sin sonido" },
	/* STR_CARD_AUTODETECT */
	{ "Autodetect",                    "Detectar autom\240ticamente" },
	/* STR_CARD_DETECTED  (arg: card name) */
	{ "Detected: %s",                  "Detectado: %s" },

	/* STR_TEST_TITLE */
	{ "Sound test",                    "Prueba de sonido" },
	/* STR_TEST_DISABLED */
	{ "Sound Blaster output is disabled.",
	  "La salida de Sound Blaster est\240 desactivada." },
	/* STR_TEST_BEGIN */
	{ "SETUP will play an ADP sound effect.\nPress ENTER to begin.",
	  "SETUP reproducir\240 un efecto de sonido de ADP.\nPulse INTRO para empezar." },
	/* STR_TEST_NORESPONSE */
	{ "The Sound Blaster did not respond at the selected resources.",
	  "El Sound Blaster no respondi\242 en los recursos seleccionados." },
	/* STR_TEST_DMAFAIL */
	{ "The DMA channel could not be started with these settings.",
	  "No se pudo iniciar el canal DMA con estos ajustes." },
	/* STR_TEST_HEARD */
	{ "Did you hear the sound effect?", "\250Escuch\242 el efecto de sonido?" },
	/* STR_TEST_RETRY */
	{ "Check that the resources match your hardware\nand run the test again.",
	  "Compruebe que los recursos coinciden con su hardware\ny repita la prueba." },

	/* STR_WIZ_TITLE */
	{ "Welcome to ADP SETUP",          "Bienvenido a la configuraci\242n de ADP" },
	/* STR_WIZ_INTRO */
	{ "This is the first time you run SETUP.\n\n"
	  "SETUP can detect your graphics adapter and Sound Blaster and\n"
	  "save the best configuration automatically. The hardware probe\n"
	  "may hang on a few old systems; choose No to set things up by hand.\n\n"
	  "Detect the hardware now?",
	  "Es la primera vez que ejecuta SETUP.\n\n"
	  "SETUP puede detectar su tarjeta gr\240fica y de sonido y\n"
	  "guardar autom\240ticamente la mejor configuraci\242n. La detecci\242n\n"
	  "puede bloquearse en algunos equipos antiguos; elija No para\n"
	  "configurar a mano.\n\n"
	  "\250Detectar el hardware ahora?" },
	/* STR_WIZ_RESULT */
	{ "Detected configuration:\n\n"
	  "  Video: %s\n"
	  "  Sound: %s\n\n"
	  "Choose \"Save settings and exit\" to keep it, or adjust any\n"
	  "setting below.",
	  "Configuraci\242n detectada:\n\n"
	  "  V\241deo: %s\n"
	  "  Sonido: %s\n\n"
	  "Elija \"Guardar y salir\" para conservarla, o cambie\n"
	  "cualquier ajuste m\240s abajo." },
	/* STR_WIZ_RESULT_TITLE */
	{ "Auto-detection complete",       "Detecci\242n completada" },
	/* STR_WIZ_NOSB */
	{ "no sound card found",           "no se encontr\242 tarjeta de sonido" },

	/* STR_SAVE_ERR_TITLE */
	{ "Save error",                    "Error al guardar" },
	/* STR_SAVE_ERR */
	{ "ADPSETUP.CFG could not be written.",
	  "No se pudo escribir ADPSETUP.CFG." },
	/* STR_EXIT_TITLE */
	{ "Exit setup",                    "Salir de la configuraci\242n" },
	/* STR_EXIT_CONFIRM */
	{ "Discard changes and exit without saving?",
	  "\250Descartar los cambios y salir sin guardar?" },
	/* STR_NODATA_TITLE */
	{ "Game data",                     "Datos del juego" },
	/* STR_NODATA */
	{ "No DOS video data files were found in this directory.\nRun SETUP.EXE from the game's directory.",
	  "No se encontraron archivos de v\241deo DOS en este directorio.\nEjecute SETUP.EXE desde el directorio del juego." },

	/* STR_UI_YES  (7-column button) */
	{ "  Yes  ",                       "  S\241   " },
	/* STR_UI_NO   (7-column button) */
	{ "  No   ",                       "  No   " },
	/* STR_UI_MENUHELP */
	{ "Arrow keys move   ENTER selects   ESC returns",
	  "Flechas mueven   INTRO elige   ESC vuelve" },
	/* STR_UI_MSGHELP */
	{ "Press ENTER or ESC",            "Pulse INTRO o ESC" },
};

static DDB_Language uiLanguage = DDB_ENGLISH;

void SetupSetLanguage(DDB_Language language)
{
	uiLanguage = (language == DDB_SPANISH) ? DDB_SPANISH : DDB_ENGLISH;
}

const char* SetupText(SetupStringId id)
{
	if (id < 0 || id >= STR_COUNT)
		return "";
	const SetupStringPair* s = &kStrings[id];
	if (uiLanguage == DDB_SPANISH && s->es != 0)
		return s->es;
	return s->en;
}

#endif
