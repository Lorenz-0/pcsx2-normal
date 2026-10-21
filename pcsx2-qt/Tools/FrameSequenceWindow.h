// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <QtWidgets/QMainWindow>

class FrameSequenceModel;
class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;
class QTimer;
class QCloseEvent;
class QShowEvent;

class FrameSequenceWindow final : public QMainWindow
{
	Q_OBJECT

public:
	explicit FrameSequenceWindow(QWidget* parent = nullptr);
	void selectSlot(int index);

protected:
	void closeEvent(QCloseEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	void loadSlot(int index);
	bool saveSlot();
	void setDirty();
	void startPlayback(bool paused);
	void pollPlayback();

	FrameSequenceModel* m_model;
	QTableView* m_table;
	QComboBox* m_slots;
	QLineEdit* m_name;
	QSpinBox* m_add_count;
	QLabel* m_message;
	QLabel* m_playback;
	QTimer* m_timer;
	QCheckBox* m_follow;
	int m_slot = 0;
	int m_direct_slot = -1;
	bool m_dirty = false;
	bool m_loading = false;
	bool m_poll_pending = false;
};
