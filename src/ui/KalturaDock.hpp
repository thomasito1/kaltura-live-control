/*
 * The docked panel, laid out as a wizard.
 *
 * Earlier this was a column of collapsing sections. That was a mistake: expanding one
 * reflowed everything below it, so the content the user had just acted on slid out
 * from under them and they had to scroll back. A wizard shows exactly one step at a
 * time, so nothing moves unless the user asks it to.
 *
 * Nothing here hard-codes a font size. OBS themes set their own base font and any
 * fixed px value fights it; sizes are derived from the widget's own font instead.
 */
#pragma once

#include <QWidget>

#include "kaltura/Endpoints.hpp"
#include "kaltura/KalturaClient.hpp"
#include "obs/HealthMonitor.hpp"
#include "ui/StreamsPanel.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;

/* Clickable step rail across the top. */
class StepRail : public QWidget {
	Q_OBJECT
public:
	enum class State { Pending, Active, Done, Error };
	explicit StepRail(const QStringList &labels, QWidget *parent = nullptr);

	void setState(int index, State s);
	void setCurrent(int index);
	void setReachable(int index, bool reachable);

signals:
	void stepClicked(int index);

private:
	struct Cell {
		QPushButton *button = nullptr;
		State state = State::Pending;
		bool reachable = false;
	};
	QVector<Cell> m_cells;
	int m_current = 0;
	void restyle(int i);
};

class KalturaDock : public QWidget {
	Q_OBJECT

public:
	explicit KalturaDock(QWidget *parent = nullptr);

	enum Page { PageConnect = 0, PageEntry, PageSettings, PageStreams, PageMonitor };

private slots:
	void onConnectClicked();
	void onDisconnectClicked();
	void onSearchClicked();
	void onSearchResultActivated(int row);
	void onApplyFeatures();
	void onToggleViewMode();
	void onEntryUpdated(const QString &what);
	void onOpenPlayer();
	void onBack();
	void onNext();
	void onStepClicked(int index);

	void onBusyChanged(bool busy);
	void onSessionReady(int partnerId, const QString &userId, qint64 expiresAtUnix);
	void onSearchResults(const QVector<kaltura::EntrySummary> &results);
	void onLiveEntryReady(const kaltura::LiveEntry &entry);
	void onIsLiveResult(const QString &entryId, bool live);
	void onFailed(const QString &message);
	void onHealthSample(const obsbridge::HealthSample &s);
	void tickSessionClock();

private:
	QWidget *buildHeader();
	QWidget *buildConnectPage();
	QWidget *buildEntryPage();
	QWidget *buildSettingsPage();
	QWidget *buildStreamsPage();
	QWidget *buildMonitorPage();

	void goTo(int page);
	void updateNav();
	void setConnectStatus(const QString &text, const QString &colour);
	void setEntryStatus(const QString &text, const QString &colour);
	void setStreamStatus(const QString &text, const QString &colour);
	void setFeatureStatus(const QString &text, const QString &colour);
	void syncFeatureControls();
	void verifyWriteTookEffect();
	void setConnectionPill(bool connected, const QString &detail);
	void refreshEntryCard();

	kaltura::KalturaClient *m_client = nullptr;
	obsbridge::HealthMonitor *m_health = nullptr;

	enum class Ctx { Connect, Entry, Features, Streams };
	Ctx m_ctx = Ctx::Connect;

	/* What we last asked Kaltura to store, so the re-read can confirm it landed. */
	bool m_verifyPending = false;
	bool m_wantRecord = false;
	bool m_wantDvr = false;
	int m_wantDvrWindow = 0;
	bool m_verifyViewMode = false;
	int m_wantViewMode = 0;

	kaltura::LiveEntry m_entry;
	kaltura::Endpoints m_endpoints;
	bool m_haveEntry = false;
	bool m_isLive = false;

	/* Chrome */
	QLabel *m_pill = nullptr;
	QProgressBar *m_busy = nullptr;
	StepRail *m_rail = nullptr;
	QStackedWidget *m_pages = nullptr;
	QPushButton *m_backBtn = nullptr;
	QPushButton *m_nextBtn = nullptr;

	/* Connect */
	QLineEdit *m_hostEdit = nullptr;
	QComboBox *m_authMode = nullptr;
	QStackedWidget *m_authStack = nullptr;
	QLineEdit *m_ksEdit = nullptr;
	QLineEdit *m_pidEdit = nullptr;
	QLineEdit *m_secretEdit = nullptr;
	QPushButton *m_connectBtn = nullptr;
	QPushButton *m_disconnectBtn = nullptr;
	QLabel *m_connectStatus = nullptr;

	/* Entry */
	QLineEdit *m_queryEdit = nullptr;
	QPushButton *m_searchBtn = nullptr;
	QListWidget *m_resultsList = nullptr;
	QLabel *m_entryStatus = nullptr;
	QLabel *m_entryCard = nullptr;
	QVector<kaltura::EntrySummary> m_results;

	/* Settings */
	QCheckBox *m_recordChk = nullptr;
	QCheckBox *m_dvrChk = nullptr;
	QSpinBox *m_dvrWindow = nullptr;
	QPushButton *m_applyFeaturesBtn = nullptr;
	QPushButton *m_viewModeBtn = nullptr;
	QLabel *m_viewModeLabel = nullptr;
	QLabel *m_featureStatus = nullptr;
	bool m_syncingFeatures = false;

	/* Streams */
	StreamsPanel *m_streams = nullptr;
	QLabel *m_streamStatus = nullptr;

	/* Monitor */
	QLabel *m_healthLabel = nullptr;
	QPlainTextEdit *m_rawView = nullptr;
};
