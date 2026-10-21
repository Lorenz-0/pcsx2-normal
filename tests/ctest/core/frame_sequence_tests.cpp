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

TEST(FrameSequence, TimelineRoundTripPreservesEveryFrame)
{
	const auto steps = Parse("2:Down;1:Down & Right;1:Right & Square;5:Neutral");
	ASSERT_TRUE(steps.has_value());
	const auto frames = Expand(steps.value());
	ASSERT_TRUE(frames.has_value());
	EXPECT_EQ(frames.value(), (std::vector<std::uint16_t>{4, 4, 6, 130, 0, 0, 0, 0, 0}));
	const auto compressed = Compress(frames.value());
	ASSERT_TRUE(compressed.has_value());
	EXPECT_EQ(Serialize(compressed.value()), Serialize(steps.value()));
}

TEST(FrameSequence, TimelineEditsSplitAndMergeRuns)
{
	std::vector<std::uint16_t> frames = {0, 0, 0};
	frames[1] = 128;
	const auto split = Compress(frames);
	ASSERT_TRUE(split.has_value());
	EXPECT_EQ(Serialize(split.value()), "1:Neutral;1:Square;1:Neutral");
	frames[0] = 128;
	const auto merged = Compress(frames);
	ASSERT_TRUE(merged.has_value());
	EXPECT_EQ(Serialize(merged.value()), "2:Square;1:Neutral");
}

TEST(FrameSequence, TimelineLimitsAndEmptyInputs)
{
	EXPECT_FALSE(Expand({}).has_value());
	EXPECT_FALSE(Compress({}).has_value());
	const std::array<Step, 1> invalid = {{{0, 0}}};
	EXPECT_FALSE(Expand(invalid).has_value());
	std::vector<std::uint16_t> frames(MAX_FRAMES, 0);
	EXPECT_TRUE(Compress(frames).has_value());
	frames.push_back(0);
	EXPECT_FALSE(Compress(frames).has_value());
	frames.resize(MAX_STEPS + 1);
	for (std::size_t i = 0; i < frames.size(); i++)
		frames[i] = static_cast<std::uint16_t>(i % 2);
	EXPECT_FALSE(Compress(frames).has_value());
}

TEST(FrameSequence, PlayheadCountsPreparedFramesAndResets)
{
	Player player;
	const auto steps = Parse("2:Square;1:Neutral");
	ASSERT_TRUE(steps.has_value());
	player.Start(steps.value());
	EXPECT_EQ(player.GetTotalFrames(), 3u);
	EXPECT_EQ(player.GetPosition(), 0u);
	player.NextFrame();
	EXPECT_EQ(player.GetPosition(), 1u);
	player.NextFrame();
	EXPECT_EQ(player.GetPosition(), 2u);
	player.NextFrame();
	EXPECT_EQ(player.GetPosition(), 3u);
	EXPECT_FALSE(player.NextFrame().has_value());
	EXPECT_EQ(player.GetPosition(), 0u);
	EXPECT_EQ(player.GetTotalFrames(), 0u);
}

TEST(FrameSequence, CompactsNeutralAndIdenticalHeldFramesWithoutChangingInputs)
{
	const auto steps = Parse("20:Neutral;4:Down & Square;1:Square;5:Neutral");
	ASSERT_TRUE(steps.has_value());
	const auto frames = Expand(steps.value());
	ASSERT_TRUE(frames.has_value());
	const auto ranges = BuildFrameRanges(frames.value(), true);
	ASSERT_EQ(ranges.size(), 4u);
	const std::array<std::size_t, 4> starts = {0, 20, 24, 25};
	const std::array<std::size_t, 4> counts = {20, 4, 1, 5};
	std::vector<std::uint16_t> reconstructed;
	for (std::size_t row = 0; row < ranges.size(); row++)
	{
		EXPECT_EQ(ranges[row].first, starts[row]);
		EXPECT_EQ(ranges[row].count, counts[row]);
		reconstructed.insert(reconstructed.end(), ranges[row].count, frames.value()[ranges[row].first]);
	}
	EXPECT_EQ(reconstructed, frames.value());
}

TEST(FrameSequence, ExpandedRowsPreserveEveryIndividualFrame)
{
	const std::vector<std::uint16_t> frames = {0, 0, 132, 132, 128, 0};
	const auto ranges = BuildFrameRanges(frames, false);
	ASSERT_EQ(ranges.size(), frames.size());
	for (std::size_t row = 0; row < ranges.size(); row++)
	{
		EXPECT_EQ(ranges[row].first, row);
		EXPECT_EQ(ranges[row].count, 1u);
		EXPECT_EQ(FindFrameRow(ranges, row), static_cast<int>(row));
	}
	EXPECT_EQ(FindFrameRow(ranges, frames.size()), -1);
}

TEST(FrameSequence, CompactedPlayheadMapsFirstLastAndBoundaryFrames)
{
	const std::vector<std::uint16_t> frames = {0, 0, 132, 132, 128, 0};
	const auto ranges = BuildFrameRanges(frames, true);
	const std::array<int, 6> expected = {0, 0, 1, 1, 2, 3};
	for (std::size_t frame = 0; frame < expected.size(); frame++)
		EXPECT_EQ(FindFrameRow(ranges, frame), expected[frame]);
	EXPECT_EQ(FindFrameRow(ranges, frames.size()), -1);
	EXPECT_EQ(FindFrameRow({}, 0), -1);
	EXPECT_TRUE(BuildFrameRanges({}, true).empty());
}

TEST(FrameSequence, CompactViewCanDisplayAndRepairOverLimitDrafts)
{
	std::vector<std::uint16_t> frames(MAX_STEPS + 1);
	for (std::size_t frame = 0; frame < frames.size(); frame++)
		frames[frame] = static_cast<std::uint16_t>(frame % 2);
	EXPECT_FALSE(Compress(frames).has_value());
	const auto ranges = BuildFrameRanges(frames, true);
	EXPECT_EQ(ranges.size(), frames.size());
	EXPECT_EQ(FindFrameRow(ranges, frames.size() - 1), static_cast<int>(frames.size() - 1));
	frames.assign(MAX_FRAMES, 0);
	const auto neutral = BuildFrameRanges(frames, true);
	ASSERT_EQ(neutral.size(), 1u);
	EXPECT_EQ(neutral[0].count, MAX_FRAMES);
	EXPECT_EQ(FindFrameRow(neutral, MAX_FRAMES - 1), 0);
}

TEST(FrameSequence, InsertedNeutralRowStaysSeparateFromIdenticalNeighbors)
{
	const std::vector<std::uint16_t> frames(21, 0);
	std::vector<std::uint32_t> isolated(frames.size(), 0);
	isolated[0] = 1;
	const auto ranges = BuildFrameRanges(frames, true, isolated);
	ASSERT_EQ(ranges.size(), 2u);
	EXPECT_EQ(ranges[0].first, 0u);
	EXPECT_EQ(ranges[0].count, 1u);
	EXPECT_EQ(ranges[1].first, 1u);
	EXPECT_EQ(ranges[1].count, 20u);
	EXPECT_EQ(FindFrameRow(ranges, 0), 0);
	EXPECT_EQ(FindFrameRow(ranges, 1), 1);
	EXPECT_EQ(FindFrameRow(ranges, 20), 1);
	// Display separation must not introduce extra runtime steps or change duration.
	const auto playback = Compress(frames);
	ASSERT_TRUE(playback.has_value());
	EXPECT_EQ(Serialize(playback.value()), "21:Neutral");
}

TEST(FrameSequence, SeparatelyInsertedRowsDoNotMergeWithEachOther)
{
	const std::vector<std::uint16_t> frames(8, 132);
	const std::vector<std::uint32_t> isolated = {0, 0, 1, 2, 2, 0, 0, 0};
	const auto ranges = BuildFrameRanges(frames, true, isolated);
	ASSERT_EQ(ranges.size(), 4u);
	EXPECT_EQ(ranges[0].count, 2u);
	EXPECT_EQ(ranges[1].count, 1u);
	EXPECT_EQ(ranges[2].count, 2u);
	EXPECT_EQ(ranges[3].count, 3u);
	const auto expanded = BuildFrameRanges(frames, false, isolated);
	EXPECT_EQ(expanded.size(), frames.size());
	EXPECT_EQ(BuildFrameRanges(frames, true).size(), 1u);
	EXPECT_EQ(BuildFrameRanges(frames, true, std::span(isolated).first(2)).size(), 1u);
}

TEST(FrameSequence, DuplicateFrameUpdatesCannotEatAOneFramePress)
{
	Player player;
	player.Start(Parse("1:Square;1:Neutral").value());
	for (int update = 0; update < 10000; update++)
	{
		EXPECT_EQ(player.NextFrame(50), 128u);
		EXPECT_EQ(player.GetPosition(), 1u);
	}
	for (int update = 0; update < 10000; update++)
	{
		EXPECT_EQ(player.NextFrame(51), 0u);
		EXPECT_EQ(player.GetPosition(), 2u);
	}
	EXPECT_FALSE(player.NextFrame(52).has_value());
	EXPECT_FALSE(player.HasTimingError());
}

TEST(FrameSequence, VariableHostWorkAndStallsDoNotChangeTheFrameTrace)
{
	const auto frames = Expand(Parse("2:Down;1:Down & Right;1:Right & Square;5:Neutral").value()).value();
	Player reference, stalled;
	const auto steps = Compress(frames).value();
	reference.Start(steps);
	stalled.Start(steps);
	// Irregular numbers of same-frame updates stand in for variable host work,
	// repeated polls and a long stall. Only an emulated counter change advances.
	const std::array<int, 9> repeats = {1, 500, 3, 10000, 2, 3000, 17, 1, 999};
	for (std::uint32_t frame = 0; frame < frames.size(); frame++)
	{
		EXPECT_EQ(reference.NextFrame(100 + frame), frames[frame]);
		for (int poll = 0; poll < repeats[frame]; poll++)
		{
			EXPECT_EQ(stalled.NextFrame(100 + frame), frames[frame]);
			EXPECT_EQ(stalled.GetPosition(), frame + 1);
		}
	}
	EXPECT_FALSE(reference.NextFrame(109).has_value());
	EXPECT_FALSE(stalled.NextFrame(109).has_value());
	EXPECT_FALSE(stalled.HasTimingError());
}

TEST(FrameSequence, CounterWrapIsAValidSingleFrameAdvance)
{
	Player player;
	player.Start(Parse("1:Down;1:Square;1:Neutral").value());
	EXPECT_EQ(player.NextFrame(UINT32_MAX - 1), 4u);
	EXPECT_EQ(player.NextFrame(UINT32_MAX), 128u);
	EXPECT_EQ(player.NextFrame(UINT32_MAX), 128u);
	EXPECT_EQ(player.NextFrame(0), 0u);
	EXPECT_FALSE(player.NextFrame(1).has_value());
	EXPECT_FALSE(player.HasTimingError());
}

TEST(FrameSequence, ActualCounterDiscontinuitiesStopInsteadOfSkippingOrCatchingUp)
{
	for (const std::uint32_t discontinuity : {42u, 39u, 10000u})
	{
		Player player;
		player.Start(Parse("10:Square;1:Neutral").value());
		ASSERT_EQ(player.NextFrame(40), 128u);
		EXPECT_FALSE(player.NextFrame(discontinuity).has_value());
		EXPECT_FALSE(player.IsActive());
		EXPECT_TRUE(player.HasTimingError());
		EXPECT_EQ(player.GetPosition(), 0u);
		EXPECT_FALSE(player.NextFrame(discontinuity).has_value());
	}
}

TEST(FrameSequence, RestartAndCancelClearTheFrameLatchAndTimingFault)
{
	Player player;
	player.Start(Parse("1:Down").value());
	EXPECT_EQ(player.NextFrame(0), 4u);
	player.Start(Parse("1:Square").value());
	EXPECT_EQ(player.NextFrame(0), 128u);
	EXPECT_FALSE(player.NextFrame(2).has_value());
	ASSERT_TRUE(player.HasTimingError());
	player.Start(Parse("1:Neutral").value());
	EXPECT_FALSE(player.HasTimingError());
	EXPECT_EQ(player.NextFrame(2), 0u);
	player.Cancel();
	EXPECT_FALSE(player.NextFrame(2).has_value());
	EXPECT_FALSE(player.HasTimingError());
}

TEST(FrameSequence, MaximumLengthHoldStillConsumesOneInputPerEmulatedFrame)
{
	Player player;
	player.Start(Parse("36000:Square").value());
	for (std::uint32_t frame = 0; frame < MAX_FRAMES; frame++)
	{
		EXPECT_EQ(player.NextFrame(frame), 128u);
		EXPECT_EQ(player.NextFrame(frame), 128u);
		EXPECT_EQ(player.GetPosition(), frame + 1);
	}
	EXPECT_FALSE(player.NextFrame(MAX_FRAMES).has_value());
	EXPECT_FALSE(player.HasTimingError());
}
