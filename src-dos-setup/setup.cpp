#ifdef _DOS

#include "game_modes.h"
#include "text_ui.h"
#include "setup_strings.h"
#include "setup_sample.h"

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
	if (config.card != SoundCard_None)
	{
		sprintf(status, SetupText(STR_STATUS_SOUND),
			ADPSetup_VideoName(config.videoMode),
			ADPSetup_CardName(config.card),
			(unsigned)(config.sampleRate / 1000));
	}
	else
	{
		sprintf(status, SetupText(STR_STATUS_NOSOUND),
			ADPSetup_VideoName(config.videoMode));
	}
}

// Map a detected SB DSP version to the closest card family (0/0xFFFF = none).
static ADPSoundCard CardFromDSPVersion(uint16_t version)
{
	if (version == 0 || version == 0xFFFF)
		return SoundCard_None;
	if (version >= 0x400)
		return SoundCard_SB16;
	if (version >= 0x300)
		return SoundCard_SBPro;
	return SoundCard_SB;
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

	menuItems[itemCount] = SetupText(STR_VIDEO_AUTODETECT);
	menuModes[itemCount++] = ScreenMode_Default;

	static const DDB_ScreenMode modes[] = {
		ScreenMode_CGA, ScreenMode_EGA, ScreenMode_VGA16,
		ScreenMode_VGA, ScreenMode_SHiRes
	};
	static const SetupStringId names[] = {
		STR_VIDEO_CGA, STR_VIDEO_EGA, STR_VIDEO_VGA16,
		STR_VIDEO_VGA, STR_VIDEO_SVGA
	};

	for (int index = 0; index < 5; index++)
	{
		if ((gameModes & VideoModeFlag(modes[index])) == 0)
			continue;
		menuItems[itemCount] = SetupText(names[index]);
		menuModes[itemCount] = modes[index];
		if (config.videoMode == modes[index])
			selected = itemCount;
		itemCount++;
	}

	int choice = UI_SelectMenu(SetupText(STR_VIDEO_TITLE), menuItems, itemCount, selected);
	if (choice < 0)
		return;

	if (menuModes[choice] != ScreenMode_Default)
	{
		config.videoMode = menuModes[choice];
		return;
	}

	if (!UI_Confirm(SetupText(STR_VIDEO_DETECT_TITLE), SetupText(STR_VIDEO_DETECT_CONFIRM)))
		return;

	DDB_ScreenMode detected = DetectVideoHardware(gameModes);
	if (detected == ScreenMode_Default)
	{
		UI_ShowMessageBox(SetupText(STR_VIDEO_DETECT_TITLE), SetupText(STR_VIDEO_DETECT_NONE));
		return;
	}

	config.videoMode = detected;
	char message[64];
	sprintf(message, SetupText(STR_VIDEO_DETECT_OK), ADPSetup_VideoName(detected));
	UI_ShowMessageBox(SetupText(STR_VIDEO_DETECT_TITLE), message);
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
	static const char* labels[] = { "11 kHz", "22 kHz", "30 kHz", "44 kHz" };

	// Only offer rates the selected card can sustain (a plain SB tops at 22 kHz).
	uint16_t maxRate = ADPSetup_CardMaxRate(config.card);
	const char* options[4];
	uint16_t    rates[4];
	int count = 0, selected = 0;
	for (int index = 0; index < kSampleRateCount; index++)
	{
		if (kSampleRates[index] > maxRate)
			continue;
		options[count] = labels[index];
		rates[count] = kSampleRates[index];
		if (config.sampleRate == kSampleRates[index])
			selected = count;
		count++;
	}
	if (count == 0)
		return;

	int choice = UI_SelectMenu(SetupText(STR_QUALITY_TITLE), options, count, selected);
	if (choice < 0)
		return;

	config.sampleRate = rates[choice];
	if (config.sampleRate > maxRate)
		config.sampleRate = maxRate;
}

static int SelectResource(const char* title, const char* const* options,
	int optionCount, int selected)
{
	int choice = UI_SelectMenu(title, options, optionCount, selected);
	return choice < 0 ? selected : choice;
}

// SB-family resources (base port / IRQ / 8-bit DMA) then the rate. 8-bit mono
// output never touches the 16-bit DMA channel, so it is not asked for.
static void ConfigureCardResources(ADPSetupConfig& config)
{
	const char* ports[] = { "220h", "240h", "260h", "280h" };
	const char* irqs[] = { "IRQ 5", "IRQ 7", "IRQ 10" };
	const char* dma8[] = { "DMA 1", "DMA 3" };

	int port = (config.sbPort - 0x220) / 0x20;
	if (port < 0 || port > 3)
		port = 0;
	port = SelectResource(SetupText(STR_SB_PORT), ports, 4, port);

	int irq = config.sbIrq == 7 ? 1 : config.sbIrq == 10 ? 2 : 0;
	irq = SelectResource(SetupText(STR_SB_IRQ), irqs, 3, irq);

	int lowDMA = config.sbDMA == 3 ? 1 : 0;
	lowDMA = SelectResource(SetupText(STR_SB_DMA8), dma8, 2, lowDMA);

	config.sbPort = (uint16_t)(0x220 + port * 0x20);
	config.sbIrq = irq == 1 ? 7 : irq == 2 ? 10 : 5;
	config.sbDMA = lowDMA == 1 ? 3 : 1;
	ConfigureSoundQuality(config);
}

// Probe the Sound Blaster (BLASTER resources + DSP version) and set the card
// family and resources from what responded.
static void AutodetectCard(ADPSetupConfig& config)
{
	if (!UI_Confirm(SetupText(STR_SB_DETECT_TITLE), SetupText(STR_SB_DETECT_CONFIRM)))
		return;

	SB_Disable();
	SB_SetMaxVersion(0xFFFF);          // detect the real card, uncapped
	if (!SB_Init(0, kSampleRates[1]))
	{
		UI_ShowMessageBox(SetupText(STR_SB_DETECT_TITLE), SetupText(STR_SB_DETECT_NONE));
		return;
	}

	config.card = CardFromDSPVersion(SB_GetDetectedVersion());
	SB_GetConfiguration(&config.sbPort, &config.sbIrq, &config.sbDMA);
	SB_Exit();

	uint16_t maxRate = ADPSetup_CardMaxRate(config.card);
	config.sampleRate = BestSampleRate(SB_GetDetectedVersion());
	if (config.sampleRate > maxRate)
		config.sampleRate = maxRate;

	char message[64];
	sprintf(message, SetupText(STR_CARD_DETECTED), ADPSetup_CardName(config.card));
	UI_ShowMessageBox(SetupText(STR_SB_DETECT_TITLE), message);
}

static void ConfigureSound(ADPSetupConfig& config)
{
	const char* options[] = {
		SetupText(STR_CARD_NONE),
		ADPSetup_CardName(SoundCard_SB),
		ADPSetup_CardName(SoundCard_SBPro),
		ADPSetup_CardName(SoundCard_SB16),
		SetupText(STR_CARD_AUTODETECT)
	};
	// Menu order maps to the enum: index 0 = None, 1 = SB, 2 = Pro, 3 = SB16.
	int selected = (config.card >= SoundCard_None && config.card <= SoundCard_SB16)
		? (int)config.card : 0;
	int choice = UI_SelectMenu(SetupText(STR_CARD_TITLE), options, 5, selected);

	switch (choice)
	{
		case 0: config.card = SoundCard_None; break;
		case 1: config.card = SoundCard_SB;    ConfigureCardResources(config); break;
		case 2: config.card = SoundCard_SBPro; ConfigureCardResources(config); break;
		case 3: config.card = SoundCard_SB16;  ConfigureCardResources(config); break;
		case 4: AutodetectCard(config); break;
	}
}

static void TestSound(const ADPSetupConfig& config)
{
	if (config.card == SoundCard_None)
	{
		UI_ShowMessageBox(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_DISABLED));
		return;
	}

	UI_ShowMessageBox(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_BEGIN));
	SB_SetMaxVersion(ADPSetup_CardDSPCap(config.card));
	SB_Configure(config.sbPort, config.sbIrq, config.sbDMA);
	SB_ConfigureOutput(0, config.sampleRate);   // 8-bit mono
	if (!SB_InitializeConfigured())
	{
		UI_ShowMessageBox(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_NORESPONSE));
		return;
	}

	if (!SB_Start())
	{
		SB_Exit();
		UI_ShowMessageBox(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_DMAFAIL));
		return;
	}

	MIX_PlaySample((uint8_t*)setupSample, (int32_t)setupSampleSize, SETUP_SAMPLE_RATE, 192);
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

	if (!UI_Confirm(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_HEARD)))
		UI_ShowMessageBox(SetupText(STR_TEST_TITLE), SetupText(STR_TEST_RETRY));
}

// Non-interactive Sound Blaster probe for the first-run wizard: reads the
// BLASTER resources, pings the card, sets the card family and picks the best
// rate for it. The caller warns the user first (can hang without a card).
static bool AutodetectSound(ADPSetupConfig& config)
{
	SB_Disable();
	SB_SetMaxVersion(0xFFFF);
	if (!SB_Init(0, kSampleRates[1]))
	{
		config.card = SoundCard_None;
		return false;
	}
	config.card = CardFromDSPVersion(SB_GetDetectedVersion());
	SB_GetConfiguration(&config.sbPort, &config.sbIrq, &config.sbDMA);
	config.sampleRate = BestSampleRate(SB_GetDetectedVersion());
	uint16_t maxRate = ADPSetup_CardMaxRate(config.card);
	if (config.sampleRate > maxRate)
		config.sampleRate = maxRate;
	SB_Exit();
	return config.card != SoundCard_None;
}

// On the very first run (no ADPSETUP.CFG yet) offer to detect the hardware and
// pre-fill the best video mode and sound card, so a player only has to confirm.
// It does NOT save on its own: the detected values populate the config and the
// user still chooses "Save settings and exit" (or discards). The confirmation
// comes first: the VESA/SB probes can hang on a few old systems, so a cautious
// user can decline and configure by hand.
static void RunFirstTimeSetup(ADPSetupConfig& config, uint32_t gameModes)
{
	if (!UI_Confirm(SetupText(STR_WIZ_TITLE), SetupText(STR_WIZ_INTRO), true))
		return;

	DDB_ScreenMode detected = DetectVideoHardware(gameModes);
	if (detected != ScreenMode_Default)
		config.videoMode = detected;

	AutodetectSound(config);

	char sound[48];
	if (config.card != SoundCard_None)
		sprintf(sound, "%s, %u kHz", ADPSetup_CardName(config.card),
			(unsigned)(config.sampleRate / 1000));
	else
		strcpy(sound, SetupText(STR_WIZ_NOSB));

	char message[256];
	sprintf(message, SetupText(STR_WIZ_RESULT),
		ADPSetup_VideoName(config.videoMode), sound);
	UI_ShowMessageBox(SetupText(STR_WIZ_RESULT_TITLE), message);
}

int main()
{
	ADPSetupConfig config;
	bool firstRun = !ADPSetup_Load(ADP_SETUP_CONFIG_FILE, &config);
	if (firstRun)
		ADPSetup_Default(&config);

	uint32_t gameModes = Setup_DetectGameVideoModes();
	uint32_t selectableModes = gameModes & SetupSupportedVideoModes;

	// Pick SETUP's language from the game's DDB, like the interpreter does.
	SetupSetLanguage(Setup_DetectGameLanguage());
	UILabels labels = {
		SetupText(STR_UI_YES), SetupText(STR_UI_NO),
		SetupText(STR_UI_MENUHELP), SetupText(STR_UI_MSGHELP)
	};
	UI_SetLabels(&labels);
	UI_Initialize();

	if (selectableModes == 0)
	{
		UI_ShowMessageBox(SetupText(STR_NODATA_TITLE), SetupText(STR_NODATA));
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
		SetupText(STR_MENU_VIDEO),
		SetupText(STR_MENU_SOUND),
		SetupText(STR_MENU_TEST),
		SetupText(STR_MENU_SAVE),
		SetupText(STR_MENU_EXIT)
	};
	int selected = 0;

	while (true)
	{
		char status[80];
		FormatStatus(status, config);
		UI_DrawTitle(SetupText(STR_TITLE), status);
		int choice = UI_SelectMenu(SetupText(STR_MENU_HEADER), mainMenu, 5, selected);
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
				UI_ShowMessageBox(SetupText(STR_SAVE_ERR_TITLE), SetupText(STR_SAVE_ERR));
				break;
			case 4:
			case -1:
				if (UI_Confirm(SetupText(STR_EXIT_TITLE), SetupText(STR_EXIT_CONFIRM)))
				{
					UI_Shutdown();
					return 0;
				}
				break;
		}
	}
}

#endif
