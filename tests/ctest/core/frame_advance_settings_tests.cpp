// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Config.h"

#include "common/MemorySettingsInterface.h"
#include "common/SettingsWrapper.h"

#include <gtest/gtest.h>

using FrameAdvanceOptions = Pcsx2Config::FrameAdvanceOptions;

TEST(FrameAdvanceSettings, DefaultsForExistingConfigurations)
{
	MemorySettingsInterface settings;
	SettingsLoadWrapper loader(settings);
	FrameAdvanceOptions options;
	options.LoadSave(loader);

	EXPECT_EQ(options.HoldDelay, 300);
	EXPECT_EQ(options.RepeatInterval, 25);
}

TEST(FrameAdvanceSettings, CustomTimingsRoundTrip)
{
	MemorySettingsInterface settings;
	FrameAdvanceOptions original;
	original.HoldDelay = 650;
	original.RepeatInterval = 40;
	SettingsSaveWrapper saver(settings);
	original.LoadSave(saver);

	EXPECT_EQ(settings.GetIntValue("EmuCore/FrameAdvance", "HoldDelay", -1), 650);
	EXPECT_EQ(settings.GetIntValue("EmuCore/FrameAdvance", "RepeatInterval", -1), 40);

	FrameAdvanceOptions restored;
	SettingsLoadWrapper loader(settings);
	restored.LoadSave(loader);
	EXPECT_EQ(restored.HoldDelay, original.HoldDelay);
	EXPECT_EQ(restored.RepeatInterval, original.RepeatInterval);
}

TEST(FrameAdvanceSettings, AllowsZeroHoldDelayAndMinimumRepeatInterval)
{
	MemorySettingsInterface settings;
	settings.SetIntValue("EmuCore/FrameAdvance", "HoldDelay", 0);
	settings.SetIntValue("EmuCore/FrameAdvance", "RepeatInterval", 1);
	SettingsLoadWrapper loader(settings);
	FrameAdvanceOptions options;
	options.LoadSave(loader);

	EXPECT_EQ(options.HoldDelay, 0);
	EXPECT_EQ(options.RepeatInterval, 1);
}

TEST(FrameAdvanceSettings, ClampsInvalidLowerBounds)
{
	MemorySettingsInterface settings;
	settings.SetIntValue("EmuCore/FrameAdvance", "HoldDelay", -1);
	settings.SetIntValue("EmuCore/FrameAdvance", "RepeatInterval", 0);
	SettingsLoadWrapper loader(settings);
	FrameAdvanceOptions options;
	options.LoadSave(loader);

	EXPECT_EQ(options.HoldDelay, 0);
	EXPECT_EQ(options.RepeatInterval, 1);
}

TEST(FrameAdvanceSettings, ClampsInvalidUpperBounds)
{
	MemorySettingsInterface settings;
	settings.SetIntValue("EmuCore/FrameAdvance", "HoldDelay", FrameAdvanceOptions::MAX_INTERVAL + 1);
	settings.SetIntValue("EmuCore/FrameAdvance", "RepeatInterval", FrameAdvanceOptions::MAX_INTERVAL + 1);
	SettingsLoadWrapper loader(settings);
	FrameAdvanceOptions options;
	options.LoadSave(loader);

	EXPECT_EQ(options.HoldDelay, FrameAdvanceOptions::MAX_INTERVAL);
	EXPECT_EQ(options.RepeatInterval, FrameAdvanceOptions::MAX_INTERVAL);
}
