// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"
#include "Counters.h"
#include "Host.h"
#include "Recording/InputRecording.h"
#include "SIO/Pad/FrameSequence.h"
#include "Input/InputManager.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadDualshock2.h"
#include "SIO/Pad/PadGuitar.h"
#include "SIO/Pad/PadJogcon.h"
#include "SIO/Pad/PadNegcon.h"
#include "SIO/Pad/PadPopn.h"
#include "SIO/Pad/PadNotConnected.h"
#include "SIO/Sio.h"

#include "Input/SDLInputSource.h"

#include "IconsFontAwesome.h"

#include "VMManager.h"
#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <algorithm>
#include <vector>

//Map of actively pressed keys so that chords work
using KeyMap = std::unordered_multimap<u64, bool>;

namespace Pad
{
	struct MacroButton
	{
		std::vector<u32> buttons; ///< Buttons to activate.
		KeyMap active_buttons; ///< Currently active buttons.
		float pressure; ///< Pressure to apply when macro is active.
		u16 toggle_frequency; ///< Interval at which the buttons will be toggled, if not 0.
		u16 toggle_counter; ///< When this counter reaches zero, buttons will be toggled.
		bool toggle_state; ///< Current state for turbo.
		bool trigger_state; ///< Whether the macro button is active.
		bool trigger_toggle; ///< Whether the macro is trigged by holding or press.
	};

	static const char* GetControllerTypeName(Pad::ControllerType type);

	static PadBase* CreatePad(u8 unifiedSlot, Pad::ControllerType controllerType, size_t ejectTicks = 0);

	static void LoadMacroButtonConfig(
		const SettingsInterface& si, u32 pad, const ControllerInfo* ci, const std::string& section);
	static void ApplyMacroButton(u32 controller, const MacroButton& mb);

	static std::array<std::array<MacroButton, NUM_MACRO_BUTTONS_PER_CONTROLLER>, NUM_CONTROLLER_PORTS> s_macro_buttons;
	static std::array<std::unique_ptr<PadBase>, NUM_CONTROLLER_PORTS> s_controllers;

	static std::array<std::vector<FrameSequence::Step>, NUM_MACRO_BUTTONS_PER_CONTROLLER> s_sequences;
	static FrameSequence::Player s_sequence_player;
	static std::optional<u32> s_sequence_slot;
	// Keep live events even during an override, so held/released buttons restore correctly.
	static std::array<float, PadDualshock2::Inputs::LENGTH> s_sequence_live_inputs = {};
	static void RestoreSequenceLiveInputs();
	static_assert(PadDualshock2::Inputs::PAD_R3 + 1 == FrameSequence::BUTTON_NAMES.size());

	bool mtapPort0LastState;
	bool mtapPort1LastState;
} // namespace Pad

bool Pad::Initialize()
{
	return true;
}

void Pad::Shutdown()
{
	CancelInputSequence();
	s_sequence_live_inputs = {};
	for (auto& port : s_controllers)
		port.reset();
}

const char* Pad::ControllerInfo::GetLocalizedName() const
{
	return Host::TranslateToCString("Pad", display_name);
}

std::optional<u32> Pad::ControllerInfo::GetBindIndex(const std::string_view name) const
{
	for (u32 i = 0; i < static_cast<u32>(bindings.size()); i++)
	{
		if (name == bindings[i].name)
			return i;
	}

	return std::nullopt;
}

void Pad::LoadConfig(const SettingsInterface& si)
{
	CancelInputSequence();
	s_sequences = {};
	s_macro_buttons = {};

	const bool mtapPort0Changed = EmuConfig.Pad.MultitapPort0_Enabled != Pad::mtapPort0LastState;
	const bool mtapPort1Changed = EmuConfig.Pad.MultitapPort1_Enabled != Pad::mtapPort1LastState;

	for (u32 i = 0; i < Pad::NUM_CONTROLLER_PORTS; i++)
	{
		const std::string section = GetConfigSection(i);
		const ControllerInfo* ci = GetControllerInfo(EmuConfig.Pad.Ports[i].Type);
		pxAssert(ci);

		PadBase* pad = Pad::GetPad(i);

		// If pad pointer is not occupied yet, type in settings no longer matches the current type, or a multitap was slotted in/out,
		// then reconstruct a new pad.
		if (!pad || pad->GetType() != ci->type || (mtapPort0Changed && (i <= 4 && i != 1)) || (mtapPort1Changed && (i >= 5 || i == 1)))
		{
			// If the slot is a multitap slot, and the multitap is not plugged in, then the pad should be forced to Not Connected.
			if (i > 1 && ((i <= 4 && !EmuConfig.Pad.MultitapPort0_Enabled) || (i > 4 && !EmuConfig.Pad.MultitapPort1_Enabled)))
			{
				pad = Pad::CreatePad(i, Pad::ControllerType::NotConnected, (VMManager::GetState() != VMState::Shutdown ? Pad::DEFAULT_EJECT_TICKS : 0));
			}
			else
			{
				// Create the new pad. If the VM is in any kind of running state at all, set eject ticks so the PS2 will think
				// there was some kind of pad ejection event and properly detect the new one, and properly initiate its config sequence.
				pad = Pad::CreatePad(i, ci->type, (VMManager::GetState() != VMState::Shutdown ? Pad::DEFAULT_EJECT_TICKS : 0));
			}

			if (i == 0)
				s_sequence_live_inputs = {};

			pxAssert(pad);
		}

		const float axis_deadzone = si.GetFloatValue(section.c_str(), "Deadzone", Pad::DEFAULT_STICK_DEADZONE);
		const float axis_scale = si.GetFloatValue(section.c_str(), "AxisScale", Pad::DEFAULT_STICK_SCALE);
		const bool use_diagonal_scale_correction = si.GetBoolValue(section.c_str(), "UseDiagonalScaleCorrection", Pad::DEFAULT_USE_DIAGONAL_SCALE_CORRECTION);
		const float button_deadzone = si.GetFloatValue(section.c_str(), "ButtonDeadzone", Pad::DEFAULT_BUTTON_DEADZONE);
		pad->SetAxisScale(axis_deadzone, axis_scale);
		pad->SetDiagonalScaleCorrection(use_diagonal_scale_correction);
		pad->SetButtonDeadzone(button_deadzone);

		if (ci->vibration_caps != Pad::VibrationCapabilities::NoVibration)
		{
			const float large_motor_scale = si.GetFloatValue(section.c_str(), "LargeMotorScale", Pad::DEFAULT_MOTOR_SCALE);
			const float small_motor_scale = si.GetFloatValue(section.c_str(), "SmallMotorScale", Pad::DEFAULT_MOTOR_SCALE);
			pad->SetVibrationScale(0, large_motor_scale);
			pad->SetVibrationScale(1, small_motor_scale);
		}

		const float pressure_modifier = si.GetFloatValue(section.c_str(), "PressureModifier", 1.0f);
		pad->SetPressureModifier(pressure_modifier);

		const int invert_l = si.GetIntValue(section.c_str(), "InvertL", 0);
		const int invert_r = si.GetIntValue(section.c_str(), "InvertR", 0);
		pad->SetAnalogInvertL((invert_l & 1) != 0, (invert_l & 2) != 0);
		pad->SetAnalogInvertR((invert_r & 1) != 0, (invert_r & 2) != 0);
		LoadMacroButtonConfig(si, i, ci, section);
	}

	Pad::mtapPort0LastState = EmuConfig.Pad.MultitapPort0_Enabled;
	Pad::mtapPort1LastState = EmuConfig.Pad.MultitapPort1_Enabled;
}

Pad::ControllerType Pad::GetDefaultPadType(u32 pad)
{
	return (pad == 0) ? ControllerType::DualShock2 : ControllerType::NotConnected;
}

void Pad::SetDefaultControllerConfig(SettingsInterface& si)
{
	si.ClearSection("InputSources");
	si.ClearSection("Hotkeys");
	si.ClearSection("Pad");

	// PCSX2 Controller Settings - Global Settings
	for (u32 i = 0; i < static_cast<u32>(InputSourceType::Count); i++)
	{
		si.SetBoolValue("InputSources",
			InputManager::InputSourceToString(static_cast<InputSourceType>(i)),
			InputManager::GetInputSourceDefaultEnabled(static_cast<InputSourceType>(i)));
	}
	si.SetBoolValue("InputSources", "SDLControllerEnhancedMode", true);
	si.SetBoolValue("InputSources", "SDLPS5PlayerLED", true);
	si.SetBoolValue("Pad", "MultitapPort1", false);
	si.SetBoolValue("Pad", "MultitapPort2", false);
	si.SetFloatValue("Pad", "PointerXScale", 8.0f);
	si.SetFloatValue("Pad", "PointerYScale", 8.0f);
	SDLInputSource::ResetRGBForAllPlayers(si);

	// PCSX2 Controller Settings - Default pad types and parameters.
	for (u32 i = 0; i < Pad::NUM_CONTROLLER_PORTS; i++)
	{
		const std::string section = GetConfigSection(i);
		const ControllerInfo* ci = GetControllerInfo(GetDefaultPadType(i));
		si.ClearSection(section.c_str());
		si.SetStringValue(section.c_str(), "Type", ci->name);

		for (const SettingInfo& csi : ci->settings)
		{
			switch (csi.type)
			{
				case SettingInfo::Type::Boolean:
					si.SetBoolValue(section.c_str(), csi.name, csi.BooleanDefaultValue());
					break;
				case SettingInfo::Type::Integer:
				case SettingInfo::Type::IntegerList:
					si.SetIntValue(section.c_str(), csi.name, csi.IntegerDefaultValue());
					break;
				case SettingInfo::Type::Float:
					si.SetFloatValue(section.c_str(), csi.name, csi.FloatDefaultValue());
					break;
				case SettingInfo::Type::String:
				case SettingInfo::Type::StringList:
				case SettingInfo::Type::Path:
					si.SetStringValue(section.c_str(), csi.name, csi.StringDefaultValue());
					break;
				default:
					break;
			}
		}
	}

	// PCSX2 Controller Settings - Controller 1 / Controller 2 / ...
	// Use the automapper to set this up.
	MapController(si, 0, InputManager::GetGenericBindingMapping("Keyboard"));
}

void Pad::SetDefaultHotkeyConfig(SettingsInterface& si)
{
	// PCSX2 Controller Settings - Hotkeys

	// PCSX2 Controller Settings - Hotkeys - General
	si.SetStringValue("Hotkeys", "ToggleFullscreen", "Keyboard/Alt & Keyboard/Return");

	// PCSX2 Controller Settings - Hotkeys - Graphics
	si.SetStringValue("Hotkeys", "CycleAspectRatio", "Keyboard/F6");
	si.SetStringValue("Hotkeys", "CycleInterlaceMode", "Keyboard/F5");
	// si.SetStringValue("Hotkeys", "CycleTVShader", "Keyboard/"); TBD
	// si.SetStringValue("Hotkeys", "CycleBlendingAccuracy", "Keyboard/"); TBD
	si.SetStringValue("Hotkeys", "ToggleMipmapMode", "Keyboard/Insert");
	//	si.SetStringValue("Hotkeys", "DecreaseUpscaleMultiplier", "Keyboard"); TBD
	//	si.SetStringValue("Hotkeys", "IncreaseUpscaleMultiplier", "Keyboard"); TBD
	//  si.SetStringValue("Hotkeys", "ReloadTextureReplacements", "Keyboard"); TBD
	si.SetStringValue("Hotkeys", "GSDumpMultiFrame", "Keyboard/Control & Keyboard/Shift & Keyboard/F8");
	si.SetStringValue("Hotkeys", "Screenshot", "Keyboard/F8");
	si.SetStringValue("Hotkeys", "GSDumpSingleFrame", "Keyboard/Shift & Keyboard/F8");
	si.SetStringValue("Hotkeys", "GSStartSavingMetricsVariableFrames", "Keyboard/F10");
	si.SetStringValue("Hotkeys", "GSStartSavingMetricsFixedFrames", "Keyboard/Control & Keyboard/F10");
	si.SetStringValue("Hotkeys", "ToggleSoftwareRendering", "Keyboard/F9");
	//  si.SetStringValue("Hotkeys", "ToggleTextureDumping", "Keyboard"); TBD
	//  si.SetStringValue("Hotkeys", "ToggleTextureReplacements", "Keyboard"); TBD
	si.SetStringValue("Hotkeys", "ZoomIn", "Keyboard/Control & Keyboard/Plus");
	si.SetStringValue("Hotkeys", "ZoomOut", "Keyboard/Control & Keyboard/Minus");
	// Missing hotkey for resetting zoom back to 100 with Keyboard/Control & Keyboard/Asterisk

	// PCSX2 Controller Settings - Hotkeys - Graphics
	si.SetStringValue("Hotkeys", "Mute", "Keyboard/Control & Keyboard/M");

	// PCSX2 Controller Settings - Hotkeys - Input Recording
	si.SetStringValue("Hotkeys", "InputRecToggleMode", "Keyboard/Shift & Keyboard/R");

	// PCSX2 Controller Settings - Hotkeys - Save States
	si.SetStringValue("Hotkeys", "LoadStateFromSlot", "Keyboard/F3");
	si.SetStringValue("Hotkeys", "SaveStateToSlot", "Keyboard/F1");
	si.SetStringValue("Hotkeys", "NextSaveStateSlot", "Keyboard/F2");
	si.SetStringValue("Hotkeys", "PreviousSaveStateSlot", "Keyboard/Shift & Keyboard/F2");

	// PCSX2 Controller Settings - Hotkeys - System
	//	si.SetStringValue("Hotkeys", "DecreaseSpeed", "Keyboard"); TBD
	//  si.SetStringValue("Hotkeys", "FrameAdvance", "Keyboard"); TBD
	//	si.SetStringValue("Hotkeys", "IncreaseSpeed", "Keyboard"); TBD
	//  si.SetStringValue("Hotkeys", "ResetVM", "Keyboard"); TBD
	//  si.SetStringValue("Hotkeys", "ShutdownVM", "Keyboard"); TBD
	si.SetStringValue("Hotkeys", "OpenPauseMenu", "Keyboard/Escape");
	si.SetStringValue("Hotkeys", "ToggleFrameLimit", "Keyboard/F4");
	si.SetStringValue("Hotkeys", "TogglePause", "Keyboard/Space");
	si.SetStringValue("Hotkeys", "ToggleSlowMotion", "Keyboard/Shift & Keyboard/Backtab");
	si.SetStringValue("Hotkeys", "ToggleTurbo", "Keyboard/Tab");
	si.SetStringValue("Hotkeys", "HoldTurbo", "Keyboard/Period");
}

static const Pad::ControllerInfo* s_controller_info[] = {
	&PadNotConnected::ControllerInfo,
	&PadDualshock2::ControllerInfo,
	&PadGuitar::ControllerInfo,
	&PadJogcon::ControllerInfo,
	&PadNegcon::ControllerInfo,
	&PadPopn::ControllerInfo,
};

const Pad::ControllerInfo* Pad::GetControllerInfo(Pad::ControllerType type)
{
	for (const ControllerInfo* info : s_controller_info)
	{
		if (type == info->type)
			return info;
	}

	return nullptr;
}

const Pad::ControllerInfo* Pad::GetControllerInfoByName(const std::string_view name)
{
	for (const ControllerInfo* info : s_controller_info)
	{
		if (name == info->name)
			return info;
	}

	return nullptr;
}

const char* Pad::GetControllerTypeName(Pad::ControllerType type)
{
	// Not localized, because it should never happen.
	const ControllerInfo* ci = GetControllerInfo(type);
	return ci ? ci->GetLocalizedName() : "UNKNOWN";
}

const std::vector<std::pair<const char*, const char*>> Pad::GetControllerTypeNames()
{
	std::vector<std::pair<const char*, const char*>> ret;
	for (const ControllerInfo* info : s_controller_info)
		ret.emplace_back(info->name, info->GetLocalizedName());

	return ret;
}

const Pad::ControllerInfo* Pad::GetConfigControllerType(const SettingsInterface& si, const char* section, u32 port)
{
	return GetControllerInfoByName(
		si.GetStringValue(section, "Type", GetControllerInfo(GetDefaultPadType(port))->name).c_str());
}

void Pad::ClearPortBindings(SettingsInterface& si, u32 port)
{
	// Why don't these just access EmuConfig? Input Profiles.
	const std::string section = GetConfigSection(port);
	const ControllerInfo* info = GetConfigControllerType(si, section.c_str(), port);
	if (!info)
		return;

	for (const InputBindingInfo& bi : info->bindings)
		si.DeleteValue(section.c_str(), bi.name);
}

void Pad::CopyConfiguration(SettingsInterface* dest_si, const SettingsInterface& src_si,
	bool copy_pad_config, bool copy_pad_bindings, bool copy_hotkey_bindings)
{
	if (copy_pad_config)
	{
		dest_si->CopyBoolValue(src_si, "Pad", "MultitapPort1");
		dest_si->CopyBoolValue(src_si, "Pad", "MultitapPort2");
		dest_si->CopyBoolValue(src_si, "Pad", "MultitapPort1");
		dest_si->CopyBoolValue(src_si, "Pad", "MultitapPort2");
		dest_si->CopyFloatValue(src_si, "Pad", "PointerXScale");
		dest_si->CopyFloatValue(src_si, "Pad", "PointerYScale");
		for (u32 i = 0; i < static_cast<u32>(InputSourceType::Count); i++)
		{
			dest_si->CopyBoolValue(src_si, "InputSources",
				InputManager::InputSourceToString(static_cast<InputSourceType>(i)));
		}
		dest_si->CopyBoolValue(src_si, "InputSources", "SDLControllerEnhancedMode");
		dest_si->CopyBoolValue(src_si, "InputSources", "SDLPS5PlayerLED");
	}

	for (u32 port = 0; port < Pad::NUM_CONTROLLER_PORTS; port++)
	{
		const std::string section = GetConfigSection(port);
		if (copy_pad_config)
			dest_si->CopyStringValue(src_si, section.c_str(), "Type");

		const Pad::ControllerInfo* info = GetConfigControllerType(src_si, section.c_str(), port);
		if (!info)
			continue;

		if (copy_pad_bindings)
		{
			for (const InputBindingInfo& bi : info->bindings)
				dest_si->CopyStringListValue(src_si, section.c_str(), bi.name);

			for (u32 i = 0; i < NUM_MACRO_BUTTONS_PER_CONTROLLER; i++)
			{
				dest_si->CopyStringListValue(src_si, section.c_str(), TinyString::from_format("Macro{}", i + 1));
				dest_si->CopyFloatValue(src_si, section.c_str(), TinyString::from_format("Macro{}Pressure", i + 1));
				dest_si->CopyFloatValue(src_si, section.c_str(), TinyString::from_format("Macro{}Deadzone", i + 1));
				dest_si->CopyStringValue(src_si, section.c_str(), TinyString::from_format("Macro{}Binds", i + 1));
				dest_si->CopyUIntValue(src_si, section.c_str(), TinyString::from_format("Macro{}Frequency", i + 1));
				dest_si->CopyBoolValue(src_si, section.c_str(), TinyString::from_format("Macro{}Toggle", i + 1));
				if (port == 0)
				{
					dest_si->CopyBoolValue(src_si, section.c_str(), TinyString::from_format("Macro{}SequenceEnabled", i + 1));
					dest_si->CopyStringValue(src_si, section.c_str(), TinyString::from_format("Macro{}Sequence", i + 1));
					dest_si->CopyStringValue(src_si, section.c_str(), TinyString::from_format("Macro{}SequenceName", i + 1));
				}
			}
		}

		if (copy_pad_config)
		{
			dest_si->CopyFloatValue(src_si, section.c_str(), "AxisScale");

			if (info->vibration_caps != Pad::VibrationCapabilities::NoVibration)
			{
				dest_si->CopyFloatValue(src_si, section.c_str(), "LargeMotorScale");
				dest_si->CopyFloatValue(src_si, section.c_str(), "SmallMotorScale");
			}

			for (const SettingInfo& csi : info->settings)
			{
				switch (csi.type)
				{
					case SettingInfo::Type::Boolean:
						dest_si->CopyBoolValue(src_si, section.c_str(), csi.name);
						break;
					case SettingInfo::Type::Integer:
					case SettingInfo::Type::IntegerList:
						dest_si->CopyIntValue(src_si, section.c_str(), csi.name);
						break;
					case SettingInfo::Type::Float:
						dest_si->CopyFloatValue(src_si, section.c_str(), csi.name);
						break;
					case SettingInfo::Type::String:
					case SettingInfo::Type::StringList:
					case SettingInfo::Type::Path:
						dest_si->CopyStringValue(src_si, section.c_str(), csi.name);
						break;
					default:
						break;
				}
			}
		}
	}

	if (copy_hotkey_bindings)
	{
		std::vector<const HotkeyInfo*> hotkeys(InputManager::GetHotkeyList());
		for (const HotkeyInfo* hki : hotkeys)
			dest_si->CopyStringListValue(src_si, "Hotkeys", hki->name);
	}
}

static u32 TryMapGenericMapping(SettingsInterface& si, const std::string& section,
	const InputManager::GenericInputBindingMapping& mapping, GenericInputBinding generic_name,
	const char* bind_name)
{
	// find the mapping it corresponds to
	const std::string* found_mapping = nullptr;
	for (const std::pair<GenericInputBinding, std::string>& it : mapping)
	{
		if (it.first == generic_name)
		{
			found_mapping = &it.second;
			break;
		}
	}

	if (found_mapping)
	{
		Console.WriteLn("(MapController) Map %s/%s to '%s'", section.c_str(), bind_name, found_mapping->c_str());
		si.SetStringValue(section.c_str(), bind_name, found_mapping->c_str());
		return 1;
	}
	else
	{
		si.DeleteValue(section.c_str(), bind_name);
		return 0;
	}
}


bool Pad::MapController(SettingsInterface& si, u32 controller,
	const std::vector<std::pair<GenericInputBinding, std::string>>& mapping)
{
	const std::string section = GetConfigSection(controller);
	const ControllerInfo* info = GetConfigControllerType(si, section.c_str(), controller);
	if (!info)
		return false;

	u32 num_mappings = 0;
	for (const InputBindingInfo& bi : info->bindings)
	{
		if (bi.generic_mapping == GenericInputBinding::Unknown)
			continue;

		num_mappings += TryMapGenericMapping(si, section, mapping, bi.generic_mapping, bi.name);
	}
	if (info->vibration_caps == Pad::VibrationCapabilities::LargeSmallMotors)
	{
		num_mappings += TryMapGenericMapping(si, section, mapping, GenericInputBinding::SmallMotor, "SmallMotor");
		num_mappings += TryMapGenericMapping(si, section, mapping, GenericInputBinding::LargeMotor, "LargeMotor");
	}
	else if (info->vibration_caps == Pad::VibrationCapabilities::SingleMotor)
	{
		if (TryMapGenericMapping(si, section, mapping, GenericInputBinding::LargeMotor, "Motor") == 0)
			num_mappings += TryMapGenericMapping(si, section, mapping, GenericInputBinding::SmallMotor, "Motor");
		else
			num_mappings++;
	}

	return (num_mappings > 0);
}

std::vector<std::string> Pad::GetInputProfileNames()
{
	FileSystem::FindResultsArray results;
	FileSystem::FindFiles(EmuFolders::InputProfiles.c_str(), "*.ini",
		FILESYSTEM_FIND_FILES | FILESYSTEM_FIND_HIDDEN_FILES | FILESYSTEM_FIND_RELATIVE_PATHS |
			FILESYSTEM_FIND_SORT_BY_NAME,
		&results);

	std::vector<std::string> ret;
	ret.reserve(results.size());
	for (FILESYSTEM_FIND_DATA& fd : results)
		ret.emplace_back(Path::GetFileTitle(fd.FileName));
	return ret;
}

std::string Pad::GetConfigSection(u32 pad_index)
{
	return fmt::format("Pad{}", pad_index + 1);
}

// Create a new pad instance, update the smart pointer for this pad slot, and return a dumb pointer to the new pad.
PadBase* Pad::CreatePad(u8 unifiedSlot, ControllerType controllerType, size_t ejectTicks)
{
	switch (controllerType)
	{
		case ControllerType::DualShock2:
			s_controllers[unifiedSlot] = std::make_unique<PadDualshock2>(unifiedSlot, ejectTicks);
			break;
		case ControllerType::Guitar:
			s_controllers[unifiedSlot] = std::make_unique<PadGuitar>(unifiedSlot, ejectTicks);
			break;
		case ControllerType::Jogcon:
			s_controllers[unifiedSlot] = std::make_unique<PadJogcon>(unifiedSlot, ejectTicks);
			break;
		case ControllerType::Negcon:
			s_controllers[unifiedSlot] = std::make_unique<PadNegcon>(unifiedSlot, ejectTicks);
			break;
		case ControllerType::Popn:
			s_controllers[unifiedSlot] = std::make_unique<PadPopn>(unifiedSlot, ejectTicks);
			break;
		default:
			s_controllers[unifiedSlot] = std::make_unique<PadNotConnected>(unifiedSlot, ejectTicks);
			break;
	}

	return s_controllers[unifiedSlot].get();
}

bool Pad::HasConnectedPad(u8 unifiedSlot)
{
	return (
		unifiedSlot < NUM_CONTROLLER_PORTS && s_controllers[unifiedSlot]->GetType() != ControllerType::NotConnected);
}

PadBase* Pad::GetPad(u8 port, u8 slot)
{
	const u8 unifiedSlot = sioConvertPortAndSlotToPad(port, slot);
	return s_controllers[unifiedSlot].get();
}

PadBase* Pad::GetPad(const u8 unifiedSlot)
{
	return s_controllers[unifiedSlot].get();
}

void Pad::SetControllerState(u32 controller, u32 bind, float value)
{
	if (controller >= NUM_CONTROLLER_PORTS)
		return;

	if (controller == 0 && bind < s_sequence_live_inputs.size())
	{
		s_sequence_live_inputs[bind] = value;
		if (s_sequence_player.IsActive())
			return;
	}
	if (s_controllers[controller])
		s_controllers[controller]->Set(bind, value);
}

static void Pad::RestoreSequenceLiveInputs()
{
	PadBase* pad = GetPad(0);
	if (!pad || pad->GetType() != ControllerType::DualShock2)
		return;

	// Restore the pressure modifier before recomputing digital pressures. Analog mode is a
	// toggle event, not a held game input: replaying it here could flip the controller mode.
	pad->Set(PadDualshock2::Inputs::PAD_PRESSURE, s_sequence_live_inputs[PadDualshock2::Inputs::PAD_PRESSURE]);
	for (u32 bind = 0; bind < s_sequence_live_inputs.size(); bind++)
	{
		if (bind != PadDualshock2::Inputs::PAD_ANALOG && bind != PadDualshock2::Inputs::PAD_PRESSURE)
			pad->Set(bind, s_sequence_live_inputs[bind]);
	}
}

void Pad::CancelInputSequence()
{
	const bool was_active = s_sequence_player.IsActive();
	s_sequence_player.Cancel();
	s_sequence_slot.reset();
	if (was_active)
		RestoreSequenceLiveInputs();
}

void Pad::SetInputSequenceTrigger(u32 index, s32 pressed)
{
	if (pressed < 0)
	{
		if (s_sequence_slot == index)
			CancelInputSequence();
		return;
	}
	if (pressed == 0 || index >= s_sequences.size() || s_sequences[index].empty() || !VMManager::HasValidVM())
		return;

	// Pressing the same trigger again cancels. A different sequence replaces the old one.
	if (s_sequence_slot == index)
		CancelInputSequence();
	else if (StartInputSequence(s_sequences[index]))
		s_sequence_slot = index;
}

bool Pad::StartInputSequence(std::span<const FrameSequence::Step> steps)
{
	PadBase* pad = GetPad(0);
	if (!VMManager::HasValidVM() || !pad || pad->GetType() != ControllerType::DualShock2)
		return false;
	if (Achievements::IsHardcoreModeActive() || g_InputRecording.isActive())
	{
		Host::AddOSDMessage(TRANSLATE_STR("Pad", "Input sequences are unavailable during Hardcore Mode or input recording/playback."),
			Host::OSD_WARNING_DURATION);
		return false;
	}

	FrameSequence::Player next;
	next.Start(steps);
	if (!next.IsActive())
		return false;
	CancelInputSequence();
	s_sequence_player = std::move(next);
	// Arm frame one immediately, including while paused. Only VSync consumes later frames.
	UpdateInputSequence();
	return true;
}

Pad::InputSequenceStatus Pad::GetInputSequenceStatus()
{
	return {s_sequence_player.IsActive(), s_sequence_player.GetPosition(), s_sequence_player.GetTotalFrames(),
		s_sequence_slot.has_value() ? static_cast<int>(s_sequence_slot.value()) : -1};
}

void Pad::UpdateInputSequence()
{
	if (!s_sequence_player.IsActive())
		return;

	PadBase* pad = GetPad(0);
	if (!pad || pad->GetType() != ControllerType::DualShock2 ||
		Achievements::IsHardcoreModeActive() || g_InputRecording.isActive())
	{
		CancelInputSequence();
		return;
	}

	const auto buttons = s_sequence_player.NextFrame(g_FrameCount);
	if (!buttons.has_value())
	{
		if (s_sequence_player.HasTimingError())
		{
			Host::AddOSDMessage(TRANSLATE_STR("Pad", "Input sequence stopped: emulated-frame synchronization was lost. Restart the combo from a known game state."),
				Host::OSD_WARNING_DURATION);
		}
		s_sequence_slot.reset();
		RestoreSequenceLiveInputs();
		return;
	}

	// Override the complete digital state and neutralize both sticks. Real inputs remain shadowed.
	pad->SetRawAnalogs({ANALOG_NEUTRAL_POSITION, ANALOG_NEUTRAL_POSITION},
		{ANALOG_NEUTRAL_POSITION, ANALOG_NEUTRAL_POSITION});
	for (u32 bind = 0; bind < FrameSequence::BUTTON_NAMES.size(); bind++)
	{
		const bool down = (buttons.value() & (1u << bind)) != 0;
		pad->SetRawPressureButton(bind, {down, down ? 255 : 0});
	}
}

void Pad::ResetControllerInputs(u32 controller)
{
	if (controller == 0)
	{
		CancelInputSequence();
		s_sequence_live_inputs = {};
	}
	if (controller >= NUM_CONTROLLER_PORTS || !HasConnectedPad(controller))
		return;

	for (InputBindingInfo binding : s_controllers[controller]->GetInfo().bindings)
	{
		switch (binding.bind_type)
		{
			case InputBindingInfo::Type::Button:
			case InputBindingInfo::Type::Axis:
			case InputBindingInfo::Type::HalfAxis:
				s_controllers[controller]->Set(binding.bind_index, 0);
				break;
			default:
				break;
		}
	}
}

void Pad::ResetAllControllerInputs()
{
	for (u32 port = 0; port < NUM_CONTROLLER_PORTS; port++)
		ResetControllerInputs(port);
}

bool Pad::Freeze(StateWrapper& sw)
{
	const bool restore_live_inputs = sw.IsReading() && (s_sequence_player.IsActive() ||
														   std::any_of(s_sequences.begin(), s_sequences.end(), [](const auto& sequence) { return !sequence.empty(); }));
	if (sw.IsReading())
	{
		CancelInputSequence();
		if (!sw.DoMarker("PAD"))
		{
			Console.Error("PAD state is invalid! Leaving the current state in place.");
			return false;
		}

		for (u32 unifiedSlot = 0; unifiedSlot < NUM_CONTROLLER_PORTS; unifiedSlot++)
		{
			PadBase* currentPad = GetPad(unifiedSlot);
			ControllerType statePadType;

			sw.Do(&statePadType);

			if (sw.HasError())
				return false;

			if (!currentPad)
			{
				pxAssertMsg(false, fmt::format("Pad::Freeze (on read) Existing Pad {0} was nullptr", unifiedSlot).c_str());
			}
			// If the currently configured pad is of a different type than the pad which was used during the savestate...
			else if (currentPad->GetType() != statePadType)
			{
				const ControllerType currentPadType = currentPad->GetType();

				const auto& [port, slot] = sioConvertPadToPortAndSlot(unifiedSlot);
				Host::AddIconOSDMessage(fmt::format("UnfreezePad{}Changed", unifiedSlot), ICON_FA_GAMEPAD,
					//: {0} and {1} are the port and multitap slot, {2} and {3} are controller types (e.g. "DualShock 2", "Jogcon")
					fmt::format(TRANSLATE_FS("Pad",
									"Controller port {0}, slot {1} has a {2} connected, but the save state has a "
									"{3}.\nEjecting {2} and replacing it with {3}."),
						port, slot,
						GetControllerTypeName(currentPad ? currentPad->GetType() : Pad::ControllerType::NotConnected),
						GetControllerTypeName(statePadType)));

				// Run the freeze, using a new pad instance of the old type just so we make sure all those attributes
				// from the state are read out and we aren't going to run into some sort of consistency problem.
				currentPad = CreatePad(unifiedSlot, statePadType);

				if (currentPad)
				{
					currentPad->Freeze(sw);

					// Now immediately discard whatever malformed pad state we just created, and replace it with a fresh pad loaded
					// using whatever the current user settings are. Savestates are, by definition, never going to occur in the middle
					// of a transfer between SIO2 and the peripheral, since they aren't captured until the VM is at a point where everything
					// is "stoppable". For all intents and purposes, by the time a savestate is captured, the IOP is "done" and there is no
					// "pending work" left hanging in SIO2 or the pads. So there is nothing actually lost from just throwing the pad away and making a new one here.
					currentPad = CreatePad(unifiedSlot, currentPadType, Pad::DEFAULT_EJECT_TICKS);
				}
				else
				{
					pxAssertMsg(false, fmt::format("Pad::Freeze (on read) State Pad {0} was nullptr", unifiedSlot).c_str());
				}
			}
			// ... else, just run the freeze normally.
			else if (currentPad && !currentPad->Freeze(sw))
			{
				return false;
			}
		}
	}
	else
	{
		if (!sw.DoMarker("PAD"))
			return false;

		for (u32 unifiedSlot = 0; unifiedSlot < NUM_CONTROLLER_PORTS; unifiedSlot++)
		{
			PadBase* pad = GetPad(unifiedSlot);
			ControllerType type = pad->GetType();
			sw.Do(&type);
			if (sw.HasError() || !pad->Freeze(sw))
				return false;
		}
	}

	// Host inputs must not rewind to a pressed combo state stored in a savestate.
	if (restore_live_inputs)
		RestoreSequenceLiveInputs();

	return !sw.HasError();
}


void Pad::LoadMacroButtonConfig(const SettingsInterface& si, u32 pad, const ControllerInfo* ci, const std::string& section)
{
	for (u32 i = 0; i < NUM_MACRO_BUTTONS_PER_CONTROLLER; i++)
	{
		if (pad == 0 && ci->type == ControllerType::DualShock2 &&
			si.GetBoolValue(section.c_str(), TinyString::from_format("Macro{}SequenceEnabled", i + 1), false))
		{
			const std::string text = si.GetStringValue(section.c_str(), TinyString::from_format("Macro{}Sequence", i + 1));
			if (auto steps = FrameSequence::Parse(text))
				s_sequences[i] = std::move(steps.value());
			else
				Console.Error("Invalid frame sequence in macro %u; playback disabled.", i + 1);
			continue;
		}

		std::string binds_string;
		if (!si.GetStringValue(section.c_str(), TinyString::from_format("Macro{}Binds", i + 1), &binds_string))
			continue;

		const u32 frequency = std::min<u32>(si.GetUIntValue(section.c_str(),
												TinyString::from_format("Macro{}Frequency", i + 1), 0u),
			std::numeric_limits<u16>::max());
		const float pressure = si.GetFloatValue(section.c_str(), TinyString::from_format("Macro{}Pressure", i + 1), 1.0f);
		const bool toggle = si.GetBoolValue(section.c_str(), TinyString::from_format("Macro{}Toggle", i + 1), false);

		// convert binds
		std::vector<u32> bind_indices;
		std::vector<std::string_view> buttons_split(StringUtil::SplitString(binds_string, '&', true));
		if (buttons_split.empty())
			continue;
		for (const std::string_view& button : buttons_split)
		{
			std::optional<u32> bind_index = ci->GetBindIndex(button);
			if (!bind_index.has_value())
			{
				Console.Error(fmt::format("Invalid bind '{}' in macro button {} for pad {}", button, i, pad));
				continue;
			}

			bind_indices.push_back(bind_index.value());
		}
		if (bind_indices.empty())
			continue;

		MacroButton& macro = s_macro_buttons[pad][i];
		macro.buttons = std::move(bind_indices);
		macro.toggle_frequency = static_cast<u16>(frequency);
		macro.pressure = pressure;
		macro.trigger_toggle = toggle;
	}
}

void Pad::SetMacroButtonState(InputBindingKey& key, u32 pad, u32 index, bool state)
{
	//0 appears for some reason and breaks mb.active_buttons.size() != binding_count
	if (key.bits == 0)
		return;

	if (pad >= Pad::NUM_CONTROLLER_PORTS || index >= NUM_MACRO_BUTTONS_PER_CONTROLLER)
		return;

	MacroButton& mb = s_macro_buttons[pad][index];
	if (mb.buttons.empty())
		return;

	SettingsInterface& sif = *Host::GetSettingsInterface();
	std::vector<std::string> data = sif.GetStringList(fmt::format("Pad{}", pad+1).c_str(), fmt::format("Macro{}", index+1).c_str());
	size_t binding_count = 0;
	//just in case there's more than one index
	for (std::string bind : data)
	{
		binding_count += InputManager::SplitChord(bind).size();
	}
	if (mb.active_buttons.find(key.bits) != mb.active_buttons.end())
		mb.active_buttons.erase(key.bits);

	mb.active_buttons.emplace(key.bits, state);

	if (mb.active_buttons.size() != binding_count)
		return;

	if (mb.active_buttons.size() > 1 && state)
	{
		for (auto it = mb.active_buttons.begin(); it != mb.active_buttons.end(); ++it)
		{
			if (!it->second)
				return;
		}
	}
	const bool trigger_state = (mb.trigger_toggle ? (state ? !mb.trigger_state : mb.trigger_state) : state);
	if (mb.trigger_state == trigger_state)
		return;

	mb.toggle_counter = mb.toggle_frequency;
	mb.trigger_state = trigger_state;
	if (mb.toggle_state != trigger_state)
	{
		mb.toggle_state = trigger_state;
		ApplyMacroButton(pad, mb);
	}
}

void Pad::ApplyMacroButton(u32 controller, const Pad::MacroButton& mb)
{
	const float value = mb.toggle_state ? mb.pressure : 0.0f;
	for (const u32 btn : mb.buttons)
		SetControllerState(controller, btn, value);
}

void Pad::UpdateMacroButtons()
{
	for (u32 pad = 0; pad < Pad::NUM_CONTROLLER_PORTS; pad++)
	{
		for (u32 index = 0; index < NUM_MACRO_BUTTONS_PER_CONTROLLER; index++)
		{
			Pad::MacroButton& mb = s_macro_buttons[pad][index];

			if (!mb.trigger_state || mb.toggle_frequency == 0)
			{
				continue;
			}

			mb.toggle_counter--;

			if (mb.toggle_counter > 0)
			{
				continue;
			}

			mb.toggle_counter = mb.toggle_frequency;
			mb.toggle_state = !mb.toggle_state;
			ApplyMacroButton(pad, mb);
		}
	}
}
