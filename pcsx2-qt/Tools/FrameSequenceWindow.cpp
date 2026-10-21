// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "FrameSequenceWindow.h"
#include "QtHost.h"
#include "pcsx2/Host.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/VMManager.h"
#include "common/SettingsInterface.h"

#include <QtCore/QAbstractTableModel>
#include <QtCore/QPointer>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <functional>

using namespace Pad::FrameSequence;

// A lazy model avoids constructing 16 widgets/items for every frame in a long combo.
class FrameSequenceModel final : public QAbstractTableModel
{
public:
	explicit FrameSequenceModel(QObject* parent)
		: QAbstractTableModel(parent)
	{
	}
	std::vector<std::uint16_t> frames = {0};
	std::function<void()> edited;

	int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(frames.size()); }
	int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 17; }
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section < 0 || section >= 17)
			return {};
		return section == 0 ? tr("Frame") : QString::fromStdString(std::string(BUTTON_NAMES[section - 1]));
	}
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (!index.isValid())
			return {};
		if (role == Qt::DisplayRole && index.column() == 0)
			return index.row() == m_playhead ? QStringLiteral("> %1").arg(index.row() + 1) : QString::number(index.row() + 1);
		if (role == Qt::CheckStateRole && index.column() > 0)
			return (frames[index.row()] & (1u << (index.column() - 1))) ? Qt::Checked : Qt::Unchecked;
		if (role == Qt::BackgroundRole && index.row() == m_playhead)
			return QApplication::palette().brush(QPalette::Highlight);
		if (role == Qt::ForegroundRole && index.row() == m_playhead)
			return QApplication::palette().brush(QPalette::HighlightedText);
		if (role == Qt::TextAlignmentRole)
			return static_cast<int>(Qt::AlignCenter);
		return {};
	}
	Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		return QAbstractTableModel::flags(index) | (index.column() > 0 ? Qt::ItemIsUserCheckable : Qt::NoItemFlags);
	}
	bool setData(const QModelIndex& index, const QVariant& value, int role) override
	{
		if (!index.isValid() || index.column() == 0 || role != Qt::CheckStateRole)
			return false;
		const auto bit = static_cast<std::uint16_t>(1u << (index.column() - 1));
		if (value.toInt() == Qt::Checked)
			frames[index.row()] |= bit;
		else
			frames[index.row()] &= static_cast<std::uint16_t>(~bit);
		emit dataChanged(index, index, {Qt::CheckStateRole});
		if (edited)
			edited();
		return true;
	}
	void replace(std::vector<std::uint16_t> next)
	{
		beginResetModel();
		frames = std::move(next);
		m_playhead = -1;
		endResetModel();
	}
	void insertFrames(int row, int count)
	{
		count = std::min(count, static_cast<int>(MAX_FRAMES) - rowCount());
		if (count <= 0)
			return;
		row = std::clamp(row, 0, rowCount());
		beginInsertRows({}, row, row + count - 1);
		frames.insert(frames.begin() + row, count, 0);
		endInsertRows();
		if (edited)
			edited();
	}
	void deleteFrames(int first, int last)
	{
		if (first < 0 || last < first || last >= rowCount())
			return;
		beginRemoveRows({}, first, last);
		frames.erase(frames.begin() + first, frames.begin() + last + 1);
		endRemoveRows();
		if (frames.empty())
			insertFrames(0, 1);
		if (edited)
			edited();
	}
	bool playhead(int row)
	{
		const int old = m_playhead;
		m_playhead = row >= 0 && row < rowCount() ? row : -1;
		if (old == m_playhead)
			return false;
		if (old >= 0 && old < rowCount())
			emit dataChanged(index(old, 0), index(old, 16), {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole});
		if (m_playhead >= 0)
			emit dataChanged(index(m_playhead, 0), index(m_playhead, 16), {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole});
		return true;
	}

private:
	int m_playhead = -1;
};

// Toggle anywhere in a button cell, not only inside the small checkbox indicator.
class FrameButtonDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem&, const QModelIndex& index) override
	{
		if (index.column() == 0)
			return false;
		if ((event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) ||
			(event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space))
		{
			return model->setData(index, index.data(Qt::CheckStateRole).toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked,
				Qt::CheckStateRole);
		}
		return false;
	}
};

FrameSequenceWindow::FrameSequenceWindow(QWidget* parent)
	: QMainWindow(parent, Qt::Window)
{
	setWindowTitle(tr("Player 1 Input Timeline"));
	setWindowModality(Qt::NonModal);
	resize(1120, 650);
	QToolBar* controls = addToolBar(tr("Timeline Controls"));
	controls->setMovable(false);
	m_slots = new QComboBox(controls);
	for (u32 slot = 1; slot <= Pad::NUM_MACRO_BUTTONS_PER_CONTROLLER; slot++)
		m_slots->addItem(tr("Combo %1").arg(slot));
	controls->addWidget(m_slots);
	m_name = new QLineEdit(controls);
	m_name->setPlaceholderText(tr("Combo name"));
	m_name->setMaxLength(64);
	controls->addWidget(m_name);
	controls->addAction(tr("Save"), this, [this]() { saveSlot(); });
	controls->addSeparator();
	controls->addAction(tr("Arm + Pause"), this, [this]() { startPlayback(true); });
	controls->addAction(tr("Play / Restart"), this, [this]() { startPlayback(false); });
	controls->addAction(tr("Stop"), this, []() { Host::RunOnCPUThread([]() { Pad::CancelInputSequence(); }); });
	controls->addAction(tr("Pause / Resume"), this, []() {
		Host::RunOnCPUThread([]() {
			if (VMManager::HasValidVM())
				VMManager::SetPaused(VMManager::GetState() != VMState::Paused);
		});
	});
	controls->addAction(tr("Frame Advance"), this, []() {
		Host::RunOnCPUThread([]() { VMManager::FrameAdvance(1); });
	});

	QWidget* central = new QWidget(this);
	QVBoxLayout* layout = new QVBoxLayout(central);
	QLabel* help = new QLabel(tr("Click a button cell to toggle it. Each row is one emulated frame; all unchecked means Neutral. "
								 "Use Arm + Pause, then Frame Advance to test from frame 1. Edits apply on the next Arm/Play. "
								 "This window does not block the game window."),
		central);
	help->setWordWrap(true);
	layout->addWidget(help);
	m_model = new FrameSequenceModel(this);
	m_model->edited = [this]() { setDirty(); };
	m_table = new QTableView(central);
	m_table->setModel(m_model);
	m_table->setItemDelegate(new FrameButtonDelegate(m_table));
	m_table->setSelectionMode(QAbstractItemView::ContiguousSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->verticalHeader()->hide();
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->horizontalHeader()->setMinimumSectionSize(48);
	m_table->horizontalHeader()->setStretchLastSection(true);
	layout->addWidget(m_table, 1);
	QHBoxLayout* rows = new QHBoxLayout();
	m_add_count = new QSpinBox(central);
	m_add_count->setRange(1, MAX_FRAMES);
	m_add_count->setSuffix(tr(" frames"));
	rows->addWidget(m_add_count);
	QPushButton* append = new QPushButton(tr("Append Neutral"), central);
	QPushButton* insert = new QPushButton(tr("Insert Before Selection"), central);
	QPushButton* remove = new QPushButton(tr("Delete Selected Frames"), central);
	rows->addWidget(append);
	rows->addWidget(insert);
	rows->addWidget(remove);
	rows->addStretch();
	m_follow = new QCheckBox(tr("Follow playhead"), central);
	m_follow->setChecked(true);
	rows->addWidget(m_follow);
	layout->addLayout(rows);
	connect(append, &QPushButton::clicked, this, [this]() { m_model->insertFrames(m_model->rowCount(), m_add_count->value()); });
	connect(insert, &QPushButton::clicked, this, [this]() {
		m_model->insertFrames(m_table->currentIndex().isValid() ? m_table->currentIndex().row() : 0, m_add_count->value());
	});
	connect(remove, &QPushButton::clicked, this, [this]() {
		const QModelIndexList selection = m_table->selectionModel()->selectedIndexes();
		int first = m_model->rowCount(), last = -1;
		for (const QModelIndex& index : selection)
		{
			first = std::min(first, index.row());
			last = std::max(last, index.row());
		}
		m_model->deleteFrames(first, last);
	});
	m_message = new QLabel(central);
	m_message->setWordWrap(true);
	layout->addWidget(m_message);
	m_playback = new QLabel(central);
	layout->addWidget(m_playback);
	setCentralWidget(central);
	connect(m_name, &QLineEdit::textEdited, this, [this]() { setDirty(); });
	connect(m_slots, &QComboBox::currentIndexChanged, this, &FrameSequenceWindow::selectSlot);
	m_timer = new QTimer(this);
	m_timer->setInterval(50);
	connect(m_timer, &QTimer::timeout, this, &FrameSequenceWindow::pollPlayback);
	loadSlot(0);
}

void FrameSequenceWindow::setDirty()
{
	if (m_loading)
		return;
	m_dirty = true;
	setWindowTitle(tr("Player 1 Input Timeline *"));
}

void FrameSequenceWindow::selectSlot(int index)
{
	if (index < 0 || index >= m_slots->count() || index == m_slot)
		return;
	if (m_dirty)
	{
		const auto answer = QMessageBox::question(this, tr("Unsaved Timeline"), tr("Save the current combo before switching?"),
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveSlot()))
		{
			const QSignalBlocker blocker(m_slots);
			m_slots->setCurrentIndex(m_slot);
			return;
		}
	}
	loadSlot(index);
}

void FrameSequenceWindow::loadSlot(int index)
{
	m_loading = true;
	m_slot = index;
	const QSignalBlocker blocker(m_slots);
	m_slots->setCurrentIndex(index);
	const std::string prefix = "Macro" + std::to_string(index + 1);
	m_name->setText(QString::fromStdString(Host::GetBaseStringSettingValue("Pad1", (prefix + "SequenceName").c_str())));
	const std::string stored = Host::GetBaseStringSettingValue("Pad1", (prefix + "Sequence").c_str());
	const auto steps = Parse(stored);
	const auto frames = steps.has_value() ? Expand(steps.value()) : std::nullopt;
	m_model->replace(frames.has_value() ? frames.value() : std::vector<std::uint16_t>{0});
	m_message->setText(!stored.empty() && !steps.has_value() ? tr("Invalid saved combo. Correct the timeline before saving.") :
															   tr("Global Player 1 macro slot. Save enables its bound trigger; Play uses this timeline directly, even with an input profile."));
	m_loading = false;
	m_dirty = false;
	setWindowTitle(tr("Player 1 Input Timeline"));
}

bool FrameSequenceWindow::saveSlot()
{
	const auto steps = Compress(m_model->frames);
	if (!steps.has_value())
	{
		m_message->setText(tr("Cannot save: use at most 36,000 frames and 256 consecutive input groups."));
		return false;
	}
	const std::string prefix = "Macro" + std::to_string(m_slot + 1);
	{
		auto lock = Host::GetSettingsLock();
		SettingsInterface* settings = Host::Internal::GetBaseSettingsLayer();
		settings->SetStringValue("Pad1", (prefix + "SequenceName").c_str(), m_name->text().toUtf8().constData());
		settings->SetStringValue("Pad1", (prefix + "Sequence").c_str(), Serialize(steps.value()).c_str());
		settings->SetBoolValue("Pad1", (prefix + "SequenceEnabled").c_str(), true);
	}
	Host::CommitBaseSettingChanges();
	g_emu_thread->applySettings();
	m_dirty = false;
	setWindowTitle(tr("Player 1 Input Timeline"));
	m_message->setText(tr("Saved to global macro slot %1. Settings reload stops playback; use Arm or Play to restart. "
						  "Input profiles may override the slot's bound trigger.")
			.arg(m_slot + 1));
	return true;
}

void FrameSequenceWindow::startPlayback(bool paused)
{
	const auto steps = Compress(m_model->frames);
	if (!steps.has_value())
	{
		m_message->setText(tr("Cannot play: use at most 36,000 frames and 256 consecutive input groups."));
		return;
	}
	const QPointer<FrameSequenceWindow> guard(this);
	Host::RunOnCPUThread([guard, program = steps.value(), paused, slot = m_slot]() {
		const bool started = Pad::StartInputSequence(program);
		if (started)
			VMManager::SetPaused(paused);
		QtHost::RunOnUIThread([guard, started, paused, slot]() {
			if (guard)
			{
				if (started)
					guard->m_direct_slot = slot;
				guard->m_message->setText(started ? (paused ? tr("Armed at frame 1. Use Frame Advance to step.") : tr("Playing from frame 1.")) :
													tr("Cannot play: start a game with a Player 1 DualShock 2 controller. Disable Hardcore Mode and input recording/playback."));
			}
		});
	});
}

void FrameSequenceWindow::pollPlayback()
{
	if (m_poll_pending)
		return;
	m_poll_pending = true;
	const QPointer<FrameSequenceWindow> guard(this);
	Host::RunOnCPUThread([guard]() {
		const auto status = Pad::GetInputSequenceStatus();
		const bool valid = VMManager::HasValidVM();
		const bool paused = VMManager::GetState() == VMState::Paused;
		QtHost::RunOnUIThread([guard, status, valid, paused]() {
			if (!guard)
				return;
			guard->m_poll_pending = false;
			const bool matching = status.active && (status.macro_slot < 0 ? guard->m_direct_slot == guard->m_slot :
																			status.macro_slot == guard->m_slot);
			const bool moved = guard->m_model->playhead(matching ? static_cast<int>(status.position) - 1 : -1);
			if (moved && matching && guard->m_follow->isChecked() && status.position > 0 && status.position <= guard->m_model->frames.size())
				guard->m_table->scrollTo(guard->m_model->index(static_cast<int>(status.position) - 1, 0), QAbstractItemView::PositionAtCenter);
			guard->m_playback->setText(!valid ? tr("No game running") : status.active ? tr("%1 | Input frame %2 / %3").arg(paused ? tr("Paused") : tr("Playing")).arg(status.position).arg(status.total) :
																						tr("%1 | No sequence active").arg(paused ? tr("Paused") : tr("Running")));
		});
	});
}

void FrameSequenceWindow::showEvent(QShowEvent* event)
{
	QMainWindow::showEvent(event);
	m_timer->start();
	pollPlayback();
}

void FrameSequenceWindow::closeEvent(QCloseEvent* event)
{
	// Closing hides the persistent tool, retaining the current draft for the next open.
	m_timer->stop();
	Host::RunOnCPUThread([]() { Pad::CancelInputSequence(); });
	QMainWindow::closeEvent(event);
}

#include "moc_FrameSequenceWindow.cpp"
