#ifdef _DOS

#include "game_modes.h"
#include "text_ui.h"

#include "mixer.h"
#include "sb.h"
#include "setup_config.h"
#include "ddb_data.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static const uint32_t SetupSupportedVideoModes = SETUP_SELECTABLE_VIDEO_MODES;

static bool HasCGA()
{
	union REGS registers;
	int86(0x11, &registers, &registers);
	uint16_t display = (registers.w.ax >> 4) & 0x03;
	return display == 1 || display == 2 || display == 3;
}

static bool HasEGA()
{
	union REGS registers;
	registers.w.ax = 0x1200;
	registers.h.bl = 0x10;
	int86(0x10, &registers, &registers);
	return registers.h.bl != 0x10;
}

static bool HasVGA()
{
	union REGS registers;
	registers.w.ax = 0x1A00;
	int86(0x10, &registers, &registers);
	return registers.h.al == 0x1A;
}

#pragma pack(push, 1)
struct SetupVBEModeInfo
{
	uint16_t modeAttributes;
	uint8_t winAAttributes;
	uint8_t winBAttributes;
	uint16_t winGranularity;
	uint16_t winSize;
	uint16_t winASegment;
	uint16_t winBSegment;
	uint32_t winFuncPtr;
	uint16_t bytesPerScanLine;
	uint16_t xResolution;
	uint16_t yResolution;
	uint8_t xCharSize;
	uint8_t yCharSize;
	uint8_t numberOfPlanes;
	uint8_t bitsPerPixel;
	uint8_t numberOfBanks;
	uint8_t memoryModel;
	uint8_t reserved[228];
};
#pragma pack(pop)

static bool HasSVGA()
{
	SetupVBEModeInfo modeInfo;
	for (uint16_t mode = 0x0100; mode <= 0x01FF; mode++)
	{
		union REGS registers;
		struct SREGS segments;
		memset(&modeInfo, 0, sizeof(modeInfo));
		memset(&registers, 0, sizeof(registers));
		memset(&segments, 0, sizeof(segments));
		registers.w.ax = 0x4F01;
		registers.w.cx = mode;
		registers.w.di = FP_OFF(&modeInfo);
		segments.es = FP_SEG(&modeInfo);
		int86x(0x10, &registers, &registers, &segments);

		if (registers.w.ax == 0x004F &&
			(modeInfo.modeAttributes & 0x0011) == 0x0011 &&
			modeInfo.xResolution == 640 && modeInfo.yResolution == 400 &&
			modeInfo.numberOfPlanes == 1 && modeInfo.bitsPerPixel == 8 &&
			modeInfo.memoryModel == 4 && modeInfo.bytesPerScanLine >= 640)
			return true;
	}
	return false;
}

static uint32_t VideoModeFlag(DDB_ScreenMode mode)
{
	switch (mode)
	{
		case ScreenMode_CGA: return DDB_DataFileMode_CGA;
		case ScreenMode_EGA: return DDB_DataFileMode_EGA;
		case ScreenMode_VGA16: return DDB_DataFileMode_VGA16;
		case ScreenMode_VGA: return DDB_DataFileMode_VGA;
		case ScreenMode_SHiRes: return DDB_DataFileMode_SHiRes;
		default: return 0;
	}
}

static void FormatStatus(char* status, const ADPSetupConfig& config)
{
	if (config.soundEnabled)
	{
		sprintf(status, "Video: %s     Sound Blaster: %Xh, IRQ %u, DMA %u",
			ADPSetup_VideoName(config.videoMode), config.sbPort,
			config.sbIrq, config.sbDMA8);
	}
	else
	{
		sprintf(status, "Video: %s     Sound: disabled",
			ADPSetup_VideoName(config.videoMode));
	}
}

static DDB_ScreenMode DetectVideoHardware(uint32_t gameModes)
{
	if ((gameModes & DDB_DataFileMode_SHiRes) && HasSVGA())
		return ScreenMode_SHiRes;
	if (HasVGA())
	{
		if (gameModes & DDB_DataFileMode_VGA)
			return ScreenMode_VGA;
		if (gameModes & DDB_DataFileMode_VGA16)
			return ScreenMode_VGA16;
	}
	if (HasEGA() && (gameModes & DDB_DataFileMode_EGA))
		return ScreenMode_EGA;
	if (HasCGA() && (gameModes & DDB_DataFileMode_CGA))
		return ScreenMode_CGA;
	return ScreenMode_Default;
}

static void ConfigureVideo(ADPSetupConfig& config, uint32_t gameModes)
{
	const char* menuItems[5];
	DDB_ScreenMode menuModes[5];
	int itemCount = 0;
	int selected = 0;

	menuItems[itemCount] = "Autodetect compatible adapter";
	menuModes[itemCount++] = ScreenMode_Default;

	static const DDB_ScreenMode modes[] = {
		ScreenMode_CGA, ScreenMode_EGA, ScreenMode_VGA16,
		ScreenMode_VGA, ScreenMode_SHiRes
	};
	static const char* names[] = {
		"CGA - 4 colours", "EGA - 16 colours",
		"VGA16 - 16-colour game data", "VGA - 320x200, 256 colours",
		"SVGA - 640x400, 256 colours"
	};

	for (int index = 0; index < 5; index++)
	{
		if ((gameModes & VideoModeFlag(modes[index])) == 0)
			continue;
		menuItems[itemCount] = names[index];
		menuModes[itemCount] = modes[index];
		if (config.videoMode == modes[index])
			selected = itemCount;
		itemCount++;
	}

	int choice = UI_SelectMenu("Video mode", menuItems, itemCount, selected);
	if (choice < 0)
		return;

	if (menuModes[choice] != ScreenMode_Default)
	{
		config.videoMode = menuModes[choice];
		return;
	}

	if (!UI_Confirm("Video detection",
		"The BIOS hardware check may be unreliable on some old systems.\nRun autodetection now?"))
		return;

	DDB_ScreenMode detected = DetectVideoHardware(gameModes);
	if (detected == ScreenMode_Default)
	{
		UI_ShowMessageBox("Video detection",
			"No compatible adapter was detected for this game's data files.");
		return;
	}

	config.videoMode = detected;
	char message[64];
	sprintf(message, "Selected %s for this game.", ADPSetup_VideoName(detected));
	UI_ShowMessageBox("Video detection", message);
}

static const uint16_t kSampleRates[] = { 11025, 22050, 30000, 44100 };
static const int      kSampleRateCount = 4;

// The best playback rate the detected Sound Blaster can sustain: the classic
// SB 1.x tops out near 22 kHz, while SB 2.0 / Pro / 16 (DSP >= 2.01) handle the
// full 44 kHz. A zero/failed DSP version falls back to the safe 22 kHz.
static uint16_t BestSampleRate(uint16_t dspVersion)
{
	return (dspVersion != 0 && dspVersion != 0xFFFF && dspVersion >= 0x201)
		? 44100 : 22050;
}

static void ConfigureSoundQuality(ADPSetupConfig& config)
{
	static const char* options[] = { "11 kHz", "22 kHz", "30 kHz", "44 kHz" };

	int selected = 1;
	for (int index = 0; index < kSampleRateCount; index++)
	{
		if (config.sampleRate == kSampleRates[index])
		{
			selected = index;
			break;
		}
	}

	int choice = UI_SelectMenu("Playback quality", options, kSampleRateCount, selected);
	if (choice < 0)
		return;

	config.sampleRate = kSampleRates[choice];
	config.soundMode = 0;
}

static int SelectResource(const char* title, const char* const* options,
	int optionCount, int selected)
{
	int choice = UI_SelectMenu(title, options, optionCount, selected);
	return choice < 0 ? selected : choice;
}

static void ConfigureManualSound(ADPSetupConfig& config)
{
	const char* ports[] = { "220h", "240h", "260h", "280h" };
	const char* irqs[] = { "IRQ 5", "IRQ 7", "IRQ 10" };
	const char* dma8[] = { "DMA 1", "DMA 3" };
	const char* dma16[] = { "DMA 5", "DMA 6", "DMA 7" };

	int port = (config.sbPort - 0x220) / 0x20;
	if (port < 0 || port > 3)
		port = 0;
	port = SelectResource("Sound Blaster base port", ports, 4, port);

	int irq = config.sbIrq == 7 ? 1 : config.sbIrq == 10 ? 2 : 0;
	irq = SelectResource("Sound Blaster IRQ", irqs, 3, irq);

	int lowDMA = config.sbDMA8 == 3 ? 1 : 0;
	lowDMA = SelectResource("8-bit DMA channel", dma8, 2, lowDMA);

	int highDMA = config.sbDMA16 == 6 ? 1 : config.sbDMA16 == 7 ? 2 : 0;
	highDMA = SelectResource("16-bit DMA channel", dma16, 3, highDMA);

	config.soundEnabled = true;
	config.sbPort = (uint16_t)(0x220 + port * 0x20);
	config.sbIrq = irq == 1 ? 7 : irq == 2 ? 10 : 5;
	config.sbDMA8 = lowDMA == 1 ? 3 : 1;
	config.sbDMA16 = highDMA == 1 ? 6 : highDMA == 2 ? 7 : 5;
	ConfigureSoundQuality(config);
}

static void DetectSoundBlaster(ADPSetupConfig& config)
{
	if (!UI_Confirm("Sound Blaster detection",
		"SETUP will test the resources in the BLASTER variable.\nRun the hardware test now?"))
		return;

	SB_Disable();
	if (!SB_Init(config.soundMode, config.sampleRate))
	{
		UI_ShowMessageBox("Sound Blaster detection",
			"No Sound Blaster responded at the configured resources.");
		return;
	}

	config.soundEnabled = true;
	SB_GetConfiguration(&config.sbPort, &config.sbIrq,
		&config.sbDMA8, &config.sbDMA16);
	SB_Exit();
	ConfigureSoundQuality(config);
	UI_ShowMessageBox("Sound Blaster detection",
		"Sound Blaster resources were detected and copied to the setup file.");
}

static void ConfigureSound(ADPSetupConfig& config)
{
	const char* options[] = {
		"Disabled",
		"Detect resources from BLASTER",
		"Configure resources manually",
		"Change playback quality"
	};
	int selected = config.soundEnabled ? 2 : 0;
	int choice = UI_SelectMenu("Sound setup", options, 4, selected);

	switch (choice)
	{
		case 0:
			config.soundEnabled = false;
			break;
		case 1:
			DetectSoundBlaster(config);
			break;
		case 2:
			ConfigureManualSound(config);
			break;
		case 3:
			if (config.soundEnabled)
				ConfigureSoundQuality(config);
			else
				UI_ShowMessageBox("Playback quality", "Enable Sound Blaster sound first.");
			break;
	}
}

static void TestSound(const ADPSetupConfig& config)
{
	if (!config.soundEnabled)
	{
		UI_ShowMessageBox("Sound test", "Sound Blaster output is disabled.");
		return;
	}

	UI_ShowMessageBox("Sound test",
		"SETUP will play an ADP sound effect.\nPress ENTER to begin.");
	SB_Configure(config.sbPort, config.sbIrq, config.sbDMA8, config.sbDMA16);
	SB_ConfigureOutput(config.soundMode, config.sampleRate);
	if (!SB_InitializeConfigured())
	{
		UI_ShowMessageBox("Sound test",
			"The Sound Blaster did not respond at the selected resources.");
		return;
	}

	if (!SB_Start())
	{
		SB_Exit();
		UI_ShowMessageBox("Sound test",
			"The DMA channel could not be started with these settings.");
		return;
	}

	MIX_PlaySample(beepSample, beepSampleSize, 11025, 192);
	// Any keypress aborts the wait, and a timeout protects against a
	// misconfiguration leaving the transfer stalled forever.
	int timeout = 10000;
	while (MIX_IsPlaying() && timeout-- > 0)
	{
		if (kbhit())
		{
			while (kbhit())
				_getch();
			break;
		}
		SB_Update();
		delay(1);
	}
	SB_Stop();
	SB_Exit();

	if (!UI_Confirm("Sound test", "Did you hear the sound effect?"))
		UI_ShowMessageBox("Sound test",
			"Check that the resources match your hardware\nand run the test again.");
}

// Non-interactive Sound Blaster probe for the first-run wizard: reads the
// BLASTER resources, pings the card and picks the best rate for its DSP. The
// caller warns the user first (this can hang on a machine without a card).
static bool AutodetectSound(ADPSetupConfig& config)
{
	SB_Disable();
	if (!SB_Init(0, kSampleRates[1]))
	{
		config.soundEnabled = false;
		return false;
	}
	config.soundEnabled = true;
	config.soundMode = 0;
	SB_GetConfiguration(&config.sbPort, &config.sbIrq, &config.sbDMA8, &config.sbDMA16);
	config.sampleRate = BestSampleRate(SB_GetDetectedVersion());
	SB_Exit();
	return true;
}

// On the very first run (no ADPSETUP.CFG yet) offer to detect the hardware and
// save a working configuration up front, so a player who just opens and closes
// SETUP still gets the best video mode and sound instead of the bare defaults.
// The confirmation comes first: the VESA and Sound Blaster probes can hang on a
// few old systems, so a cautious user can decline and configure by hand.
static void RunFirstTimeSetup(ADPSetupConfig& config, uint32_t gameModes)
{
	if (!UI_Confirm("Welcome to ADP SETUP",
		"This looks like the first time you run SETUP.\n\n"
		"SETUP can detect your graphics adapter and Sound Blaster and\n"
		"save the best configuration automatically. The hardware probe\n"
		"may hang on a few old systems; choose No to set things up by hand.\n\n"
		"Detect the hardware now?", true))
		return;

	DDB_ScreenMode detected = DetectVideoHardware(gameModes);
	if (detected != ScreenMode_Default)
		config.videoMode = detected;

	bool sound = AutodetectSound(config);

	char rate[16];
	rate[0] = 0;
	if (sound)
		sprintf(rate, "%u kHz", (unsigned)(config.sampleRate / 1000));

	if (!ADPSetup_Save(ADP_SETUP_CONFIG_FILE, &config))
	{
		UI_ShowMessageBox("Save error", "ADPSETUP.CFG could not be written.");
		return;
	}

	char message[192];
	sprintf(message,
		"Detected configuration:\n\n"
		"  Video: %s\n"
		"  Sound: %s\n\n"
		"Saved to ADPSETUP.CFG. You can start the game now, or change\n"
		"any setting below and save again.",
		ADPSetup_VideoName(config.videoMode),
		sound ? rate : "no Sound Blaster found");
	UI_ShowMessageBox("Auto-detection complete", message);
}

int main()
{
	ADPSetupConfig config;
	bool firstRun = !ADPSetup_Load(ADP_SETUP_CONFIG_FILE, &config);
	if (firstRun)
		ADPSetup_Default(&config);

	uint32_t gameModes = Setup_DetectGameVideoModes();
	uint32_t selectableModes = gameModes & SetupSupportedVideoModes;
	UI_Initialize();

	if (selectableModes == 0)
	{
		UI_ShowMessageBox("Game data",
			"No DOS video data files were found in this directory.\nRun SETUP.EXE from the game's directory.");
		UI_Shutdown();
		return 1;
	}

	if ((selectableModes & VideoModeFlag(config.videoMode)) == 0)
	{
		if (selectableModes & DDB_DataFileMode_VGA)
			config.videoMode = ScreenMode_VGA;
		else if (selectableModes & DDB_DataFileMode_SHiRes)
			config.videoMode = ScreenMode_SHiRes;
		else if (selectableModes & DDB_DataFileMode_VGA16)
			config.videoMode = ScreenMode_VGA16;
		else if (selectableModes & DDB_DataFileMode_EGA)
			config.videoMode = ScreenMode_EGA;
		else
			config.videoMode = ScreenMode_CGA;
	}

	if (firstRun)
		RunFirstTimeSetup(config, selectableModes);

	const char* mainMenu[] = {
		"Configure video",
		"Configure sound",
		"Test Sound Blaster",
		"Save settings and exit",
		"Exit without saving"
	};
	int selected = 0;

	while (true)
	{
		char status[80];
		FormatStatus(status, config);
		UI_DrawTitle("ADP DOS SETUP", status);
		int choice = UI_SelectMenu("Configuration", mainMenu, 5, selected);
		if (choice >= 0)
			selected = choice;

		switch (choice)
		{
			case 0: ConfigureVideo(config, selectableModes); break;
			case 1: ConfigureSound(config); break;
			case 2: TestSound(config); break;
			case 3:
				if (ADPSetup_Save(ADP_SETUP_CONFIG_FILE, &config))
				{
					UI_Shutdown();
					return 0;
				}
				UI_ShowMessageBox("Save error", "ADPSETUP.CFG could not be written.");
				break;
			case 4:
			case -1:
				if (UI_Confirm("Exit setup", "Discard changes and exit without saving?"))
				{
					UI_Shutdown();
					return 0;
				}
				break;
		}
	}
}

#endif
