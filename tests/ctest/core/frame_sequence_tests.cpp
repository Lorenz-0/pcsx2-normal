// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "SIO/Pad/FrameSequence.h"

#include <gtest/gtest.h>

using namespace Pad::FrameSequence;

TEST(FrameSequence, ParsesDirectionsChordsAndNeutral)
{
	const auto steps = Parse("2:Down;1:Down & Right;1:Right & Square;5:Neutral");
	ASSERT_TRUE(steps.has_value());
	ASSERT_EQ(steps->size(), 4u);
	EXPECT_EQ((*steps)[0].buttons, 1u << 2);
	EXPECT_EQ((*steps)[0].frames, 2u);
	EXPECT_EQ((*steps)[1].buttons, (1u << 2) | (1u << 1));
	EXPECT_EQ((*steps)[2].buttons, (1u << 1) | (1u << 7));
	EXPECT_EQ((*steps)[3].buttons, 0u);
	EXPECT_EQ((*steps)[3].frames, 5u);
}

TEST(FrameSequence, AllDigitalButtonsRoundTrip)
{
	const std::array<Step, 1> steps = {{{0xffff, 1}}};
	const auto parsed = Parse(Serialize(steps));
	ASSERT_TRUE(parsed.has_value());
	ASSERT_EQ(parsed->size(), 1u);
	EXPECT_EQ(parsed->front().buttons, 0xffff);
}

TEST(FrameSequence, AcceptsWhitespace)
{
	const auto parsed = Parse(" 2 : Down ; 1 : Right & Square ; 3 : Neutral ");
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(Serialize(parsed.value()), "2:Down;1:Right & Square;3:Neutral");
}

TEST(FrameSequence, RejectsMalformedProgramsWithoutPartialPlayback)
{
	for (const char* text : {"", " ", ":Down", "0:Down", "-1:Down", "+1:Down", "1.5:Down", "1x:Down",
		"4294967296:Down", "36001:Down", "1:", "1:Unknown", "1:down", "1:Analog", "1:LUp",
		"1:Neutral & Down", "1:Down & Down", "1:Down &", "1:&Down", "1:Down && Right",
		"1:Down;", ";1:Down", "1:Down;;1:Right", "1:Down;invalid", "1:Down:Right"})
	{
		EXPECT_FALSE(Parse(text).has_value()) << text;
	}
}

TEST(FrameSequence, BoundsTotalDurationAndStepCount)
{
	EXPECT_TRUE(Parse("36000:Neutral").has_value());
	EXPECT_TRUE(Parse("35999:Neutral;1:Square").has_value());
	EXPECT_FALSE(Parse("36000:Neutral;1:Square").has_value());
	std::string text;
	for (std::size_t i = 0; i < MAX_STEPS; i++)
	{
		if (!text.empty())
			text.push_back(';');
		text.append("1:Square");
	}
	EXPECT_TRUE(Parse(text).has_value());
	EXPECT_FALSE(Parse(text + ";1:Square").has_value());
	EXPECT_FALSE(Parse(std::string(32769, ' ')).has_value());
}

TEST(FrameSequence, EachStepLastsExactlyItsConfiguredNumberOfFrames)
{
	Player player;
	const auto steps = Parse("2:Down;1:Down & Right;1:Right & Square;5:Neutral");
	ASSERT_TRUE(steps.has_value());
	player.Start(steps.value());
	const std::array<std::uint16_t, 9> expected = {4, 4, 6, 130, 0, 0, 0, 0, 0};
	for (const auto buttons : expected)
	{
		ASSERT_TRUE(player.IsActive());
		EXPECT_EQ(player.NextFrame(), buttons);
	}
	// Keep the final state for its whole last frame, then release at the next boundary.
	EXPECT_TRUE(player.IsActive());
	EXPECT_FALSE(player.NextFrame().has_value());
	EXPECT_FALSE(player.IsActive());
	EXPECT_FALSE(player.NextFrame().has_value());
}

TEST(FrameSequence, OneFramePressIsNotLost)
{
	Player player;
	const auto steps = Parse("1:Square");
	ASSERT_TRUE(steps.has_value());
	player.Start(steps.value());
	EXPECT_EQ(player.NextFrame(), 128u);
	EXPECT_FALSE(player.NextFrame().has_value());
}

TEST(FrameSequence, NeutralIsAnOverrideNotEndOfPlayback)
{
	Player player;
	const auto steps = Parse("1:Neutral;1:Square");
	ASSERT_TRUE(steps.has_value());
	player.Start(steps.value());
	const auto neutral = player.NextFrame();
	ASSERT_TRUE(neutral.has_value());
	EXPECT_EQ(neutral.value(), 0u);
	EXPECT_TRUE(player.IsActive());
	EXPECT_EQ(player.NextFrame(), 128u);
	EXPECT_FALSE(player.NextFrame().has_value());
}

TEST(FrameSequence, DoesNotProgressUntilAnEmulatedFrameIsConsumed)
{
	Player player;
	const auto steps = Parse("1:Down;1:Square");
	ASSERT_TRUE(steps.has_value());
	player.Start(steps.value());
	for (int idle_poll = 0; idle_poll < 1000; idle_poll++)
		EXPECT_TRUE(player.IsActive());
	EXPECT_EQ(player.NextFrame(), 4u);
	for (int idle_poll = 0; idle_poll < 1000; idle_poll++)
		EXPECT_TRUE(player.IsActive());
	EXPECT_EQ(player.NextFrame(), 128u);
}

TEST(FrameSequence, CancellationAndRestartReleaseOldState)
{
	Player player;
	const auto first = Parse("10:Square");
	const auto second = Parse("1:Down");
	ASSERT_TRUE(first.has_value());
	ASSERT_TRUE(second.has_value());
	player.Start(first.value());
	EXPECT_EQ(player.NextFrame(), 128u);
	player.Cancel();
	EXPECT_FALSE(player.IsActive());
	EXPECT_FALSE(player.NextFrame().has_value());
	player.Start(second.value());
	EXPECT_EQ(player.NextFrame(), 4u);
	EXPECT_FALSE(player.NextFrame().has_value());
}

TEST(FrameSequence, PlaybackOwnsItsProgram)
{
	Player player;
	{
		const auto steps = Parse("1:Square");
		ASSERT_TRUE(steps.has_value());
		player.Start(steps.value());
	}
	EXPECT_EQ(player.NextFrame(), 128u);
	EXPECT_FALSE(player.NextFrame().has_value());
}

TEST(FrameSequence, RejectsInvalidDirectPrograms)
{
	Player player;
	player.Start({});
	EXPECT_FALSE(player.IsActive());
	const std::array<Step, 1> zero = {{{1, 0}}};
	player.Start(zero);
	EXPECT_FALSE(player.IsActive());
	const std::array<Step, 2> too_long = {{{1, MAX_FRAMES}, {0, 1}}};
	player.Start(too_long);
	EXPECT_FALSE(player.IsActive());
}
