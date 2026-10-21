// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Pad::FrameSequence
{
	// In DualShock 2 bind-index order. Only digital buttons are supported in v1.
	inline constexpr std::array<std::string_view, 16> BUTTON_NAMES = {
		"Up", "Right", "Down", "Left", "Triangle", "Circle", "Cross", "Square",
		"Select", "Start", "L1", "L2", "R1", "R2", "L3", "R3"};
	inline constexpr std::size_t MAX_STEPS = 256;
	inline constexpr std::uint32_t MAX_FRAMES = 36000;

	struct Step
	{
		std::uint16_t buttons = 0;
		std::uint32_t frames = 1;
	};

	inline std::string_view Trim(std::string_view text)
	{
		constexpr std::string_view whitespace = " \t\r\n";
		const auto first = text.find_first_not_of(whitespace);
		return first == std::string_view::npos ? std::string_view() :
			text.substr(first, text.find_last_not_of(whitespace) - first + 1);
	}

	inline std::string FormatButtons(std::uint16_t buttons)
	{
		std::string result;
		for (std::size_t i = 0; i < BUTTON_NAMES.size(); i++)
		{
			if ((buttons & (1u << i)) == 0)
				continue;
			if (!result.empty())
				result.append(" & ");
			result.append(BUTTON_NAMES[i]);
		}
		return result.empty() ? "Neutral" : result;
	}

	// Stored as "2:Down;1:Down & Right;1:Right & Square;5:Neutral".
	// Reject the entire program on malformed input; never play a partially parsed combo.
	inline std::optional<std::vector<Step>> Parse(std::string_view text)
	{
		if (text.empty() || text.size() > 32768)
			return std::nullopt;

		std::vector<Step> steps;
		std::uint32_t total_frames = 0;
		while (!text.empty())
		{
			if (steps.size() == MAX_STEPS)
				return std::nullopt;
			const auto end = text.find(';');
			const std::string_view row = Trim(text.substr(0, end));
			const auto colon = row.find(':');
			if (colon == std::string_view::npos)
				return std::nullopt;

			Step step;
			const std::string_view duration = Trim(row.substr(0, colon));
			if (duration.empty())
				return std::nullopt;
			const auto [ptr, error] = std::from_chars(duration.data(), duration.data() + duration.size(), step.frames);
			if (error != std::errc() || ptr != duration.data() + duration.size() ||
				step.frames == 0 || step.frames > MAX_FRAMES - total_frames)
			{
				return std::nullopt;
			}

			std::string_view buttons = Trim(row.substr(colon + 1));
			if (buttons.empty())
				return std::nullopt;
			if (buttons != "Neutral")
			{
				while (!buttons.empty())
				{
					const auto separator = buttons.find('&');
					const auto name = Trim(buttons.substr(0, separator));
					std::size_t index = 0;
					while (index < BUTTON_NAMES.size() && BUTTON_NAMES[index] != name)
						index++;
					if (index == BUTTON_NAMES.size() || (step.buttons & (1u << index)) != 0)
						return std::nullopt;
					step.buttons |= static_cast<std::uint16_t>(1u << index);
					if (separator == std::string_view::npos)
						break;
					buttons.remove_prefix(separator + 1);
					if (buttons.empty())
						return std::nullopt;
				}
			}

			total_frames += step.frames;
			steps.push_back(step);
			if (end == std::string_view::npos)
				break;
			text.remove_prefix(end + 1);
			if (text.empty())
				return std::nullopt;
		}
		return steps;
	}

	inline std::string Serialize(std::span<const Step> steps)
	{
		std::string result;
		for (const Step& step : steps)
		{
			if (!result.empty())
				result.push_back(';');
			result.append(std::to_string(step.frames)).append(":").append(FormatButtons(step.buttons));
		}
		return result;
	}

	// The editor presents individual frames; the runtime/settings retain run-length encoded steps.
	inline std::optional<std::vector<std::uint16_t>> Expand(std::span<const Step> steps)
	{
		if (steps.empty() || steps.size() > MAX_STEPS)
			return std::nullopt;
		std::vector<std::uint16_t> frames;
		for (const Step& step : steps)
		{
			if (step.frames == 0 || step.frames > MAX_FRAMES - frames.size())
				return std::nullopt;
			frames.insert(frames.end(), step.frames, step.buttons);
		}
		return frames;
	}

	inline std::optional<std::vector<Step>> Compress(std::span<const std::uint16_t> frames)
	{
		if (frames.empty() || frames.size() > MAX_FRAMES)
			return std::nullopt;
		std::vector<Step> steps;
		for (const auto buttons : frames)
		{
			if (!steps.empty() && steps.back().buttons == buttons)
				steps.back().frames++;
			else
			{
				if (steps.size() == MAX_STEPS)
					return std::nullopt;
				steps.push_back({buttons, 1});
			}
		}
		return steps;
	}

	class Player
	{
	public:
		bool IsActive() const { return !m_steps.empty(); }
		std::uint32_t GetPosition() const { return m_position; }
		std::uint32_t GetTotalFrames() const { return m_total; }

		void Start(std::span<const Step> steps)
		{
			Cancel();
			if (steps.empty() || steps.size() > MAX_STEPS)
				return;
			std::uint32_t total = 0;
			for (const Step& step : steps)
			{
				if (step.frames == 0 || step.frames > MAX_FRAMES - total)
					return;
				total += step.frames;
			}
			m_steps.assign(steps.begin(), steps.end());
			m_total = total;
			m_index = 0;
			m_remaining = 0;
		}

		void Cancel()
		{
			m_steps.clear();
			m_position = 0;
			m_total = 0;
			m_index = 0;
			m_remaining = 0;
		}

		// Called exactly once at the emulated frame input boundary, never from idle polling.
		// nullopt means release the override; zero means an intentional neutral frame.
		std::optional<std::uint16_t> NextFrame()
		{
			if (!IsActive())
				return std::nullopt;
			if (m_remaining == 0)
			{
				if (m_index == m_steps.size())
				{
					Cancel();
					return std::nullopt;
				}
				m_remaining = m_steps[m_index++].frames;
			}
			m_remaining--;
			m_position++;
			return m_steps[m_index - 1].buttons;
		}

	private:
		std::vector<Step> m_steps;
		std::size_t m_index = 0;
		std::uint32_t m_remaining = 0;
		std::uint32_t m_position = 0;
		std::uint32_t m_total = 0;
	};
} // namespace Pad::FrameSequence
