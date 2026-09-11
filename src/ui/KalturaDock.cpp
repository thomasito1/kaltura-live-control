#include "KalturaDock.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include <obs.h>

#include "obs/StreamService.hpp"
#include "plugin-support.h"

using kaltura::EntrySummary;
using kaltura::LiveEntry;

namespace {

const char *kBlue = "#006EFA";
const char *kSky = "#41BEFF";
const char *kGreen = "#5BC686";
const char *kAmber = "#FFAA00";
const char *kRed = "#FF3D23";
const char *kMuted = "#9aa3b2";

/*
 * Fonts derive from the widget's own font rather than being set in px. OBS themes
 * choose their own base size and family, and a hard-coded "font-size:10px" ignores
 * that - which is why the panel looked out of place next to the rest of the UI.
 */
QFont scaledFont(const QWidget *w, qreal scale, bool bold = false)
{
	QFont f = w->font();
	if (f.pointSizeF() > 0.0)
		f.setPointSizeF(f.pointSizeF() * scale);
	else
		f.setPixelSize(qMax(8, qRound(f.pixelSize() * scale)));
	f.setBold(bold);
	return f;
}

void applyCaption(QLabel *l)
{
	l->setFont(scaledFont(l, 0.88));
	l->setStyleSheet(QStringLiteral("color:%1;").arg(QString::fromLatin1(kMuted)));
}

QLabel *caption(const QString &t)
{
	auto *l = new QLabel(t);
	l->setWordWrap(true);
	applyCaption(l);
	return l;
}

QLabel *sectionTitle(const QString &t)
{
	auto *l = new QLabel(t);
	l->setFont(scaledFont(l, 1.0, true));
	return l;
}

QFrame *hLine()
{
	auto *l = new QFrame;
	l->setFrameShape(QFrame::HLine);
	l->setStyleSheet(QStringLiteral("color: rgba(127,127,127,55);"));
	return l;
}

QString primaryButtonQss(const QString &bg)
{
	return QStringLiteral("QPushButton{background:%1;color:#fff;border:none;"
			      "border-radius:4px;padding:7px 14px;}"
			      "QPushButton:disabled{background:rgba(127,127,127,55);"
			      "color:rgba(255,255,255,110);}")
		.arg(bg);
}

QString severityColour(obsbridge::Severity s)
{
	switch (s) {
	case obsbridge::Severity::Critical:
		return QString::fromLatin1(kRed);
	case obsbridge::Severity::Warn:
		return QString::fromLatin1(kAmber);
	default:
		return QString::fromLatin1(kGreen);
	}
}

QPixmap renderLogo(int heightPx, qreal dpr)
{
	QSvgRenderer renderer(QStringLiteral(":/kaltura/logo.svg"));
	if (!renderer.isValid())
		return QPixmap();
	const QSizeF native = renderer.defaultSize();
	if (native.height() <= 0)
		return QPixmap();

	const int w = qRound(heightPx * (native.width() / native.height()));
	QPixmap pm(qRound(w * dpr), qRound(heightPx * dpr));
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	renderer.render(&p);
	p.end();
	pm.setDevicePixelRatio(dpr);
	return pm;
}

void styleStatus(QLabel *l, const QString &text, const QString &colour)
{
	l->setText(text);
	l->setStyleSheet(QStringLiteral("color:%1;").arg(colour));
	l->setVisible(!text.isEmpty());
}

} // namespace

/* ============================================================ StepRail === */

StepRail::StepRail(const QStringList &labels, QWidget *parent) : QWidget(parent)
{
	auto *h = new QHBoxLayout(this);
	h->setContentsMargins(0, 2, 0, 2);
	h->setSpacing(2);

	for (int i = 0; i < labels.size(); ++i) {
		Cell c;
		c.button = new QPushButton(labels.at(i));
		c.button->setFlat(true);
		c.button->setCursor(Qt::PointingHandCursor);
		c.button->setFont(scaledFont(c.button, 0.82));
		c.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		connect(c.button, &QPushButton::clicked, this, [this, i]() {
			if (m_cells[i].reachable)
				emit stepClicked(i);
		});
		h->addWidget(c.button, 1);
		m_cells.append(c);
	}

	for (int i = 0; i < m_cells.size(); ++i)
		restyle(i);
}

void StepRail::setState(int index, State s)
{
	if (index < 0 || index >= m_cells.size())
		return;
	m_cells[index].state = s;
	restyle(index);
}

void StepRail::setReachable(int index, bool reachable)
{
	if (index < 0 || index >= m_cells.size())
		return;
	m_cells[index].reachable = reachable;
	restyle(index);
}

void StepRail::setCurrent(int index)
{
	m_current = index;
	for (int i = 0; i < m_cells.size(); ++i)
		restyle(i);
}

void StepRail::restyle(int i)
{
	Cell &c = m_cells[i];
	const bool current = (i == m_current);

	QString colour = QString::fromLatin1(kMuted);
	if (c.state == State::Done)
		colour = QString::fromLatin1(kGreen);
	if (c.state == State::Error)
		colour = QString::fromLatin1(kRed);
	if (current)
		colour = QString::fromLatin1(kSky);

	c.button->setEnabled(c.reachable || current);
	c.button->setStyleSheet(
		QStringLiteral("QPushButton{border:none;background:transparent;color:%1;"
			       "border-bottom:2px solid %2;padding:5px 2px;}"
			       "QPushButton:disabled{color:rgba(127,127,127,110);}")
			.arg(colour, current ? colour : QStringLiteral("rgba(127,127,127,45)")));
}

/* ========================================================= KalturaDock === */

KalturaDock::KalturaDock(QWidget *parent)
	: QWidget(parent), m_client(new kaltura::KalturaClient(this)),
	  m_health(new obsbridge::HealthMonitor(this))
{
	setMinimumWidth(290);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 10, 12, 10);
	root->setSpacing(8);

	root->addWidget(buildHeader());

	m_rail = new StepRail(
		{tr("Connect"), tr("Entry"), tr("Settings"), tr("Streams"), tr("Monitor")});
	connect(m_rail, &StepRail::stepClicked, this, &KalturaDock::onStepClicked);
	root->addWidget(m_rail);

	m_pages = new QStackedWidget;
	m_pages->addWidget(buildConnectPage());
	m_pages->addWidget(buildEntryPage());
	m_pages->addWidget(buildSettingsPage());
	m_pages->addWidget(buildStreamsPage());
	m_pages->addWidget(buildMonitorPage());
	root->addWidget(m_pages, 1);

	root->addWidget(hLine());

	auto *nav = new QHBoxLayout;
	nav->setContentsMargins(0, 0, 0, 0);
	nav->setSpacing(6);
	m_backBtn = new QPushButton(tr("Back"));
	m_backBtn->setCursor(Qt::PointingHandCursor);
	m_backBtn->setStyleSheet(
		QStringLiteral("QPushButton{background:transparent;border:1px solid "
			       "rgba(127,127,127,70);border-radius:4px;padding:7px 14px;}"
			       "QPushButton:disabled{color:rgba(127,127,127,110);}"));
	m_nextBtn = new QPushButton(tr("Next"));
	m_nextBtn->setCursor(Qt::PointingHandCursor);
	m_nextBtn->setStyleSheet(primaryButtonQss(QString::fromLatin1(kBlue)));
	nav->addWidget(m_backBtn, 0);
	nav->addStretch(1);
	nav->addWidget(m_nextBtn, 0);
	root->addLayout(nav);

	connect(m_backBtn, &QPushButton::clicked, this, &KalturaDock::onBack);
	connect(m_nextBtn, &QPushButton::clicked, this, &KalturaDock::onNext);

	connect(m_client, &kaltura::KalturaClient::busyChanged, this, &KalturaDock::onBusyChanged);
	connect(m_client, &kaltura::KalturaClient::sessionReady, this, &KalturaDock::onSessionReady);
	connect(m_client, &kaltura::KalturaClient::searchResults, this, &KalturaDock::onSearchResults);
	connect(m_client, &kaltura::KalturaClient::liveEntryReady, this, &KalturaDock::onLiveEntryReady);
	connect(m_client, &kaltura::KalturaClient::isLiveResult, this, &KalturaDock::onIsLiveResult);
	connect(m_client, &kaltura::KalturaClient::entryUpdated, this, &KalturaDock::onEntryUpdated);
	connect(m_client, &kaltura::KalturaClient::failed, this, &KalturaDock::onFailed);
	connect(m_health, &obsbridge::HealthMonitor::sampled, this, &KalturaDock::onHealthSample);
	connect(m_streams, &StreamsPanel::statusMessage, this,
		[this](const QString &text, bool isError) {
			setStreamStatus(text, QString::fromLatin1(isError ? kRed : kGreen));
		});

	m_health->start(1000);

	auto *clock = new QTimer(this);
	connect(clock, &QTimer::timeout, this, &KalturaDock::tickSessionClock);
	clock->start(20000);

	setConnectionPill(false, tr("not connected"));
	goTo(PageConnect);
}

QWidget *KalturaDock::buildHeader()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);

	auto *row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);

	auto *logo = new QLabel;
	const QPixmap pm = renderLogo(20, devicePixelRatioF());
	if (!pm.isNull())
		logo->setPixmap(pm);
	else
		logo->setText(QStringLiteral("kaltura"));
	row->addWidget(logo, 0, Qt::AlignVCenter);
	row->addStretch(1);

	m_pill = new QLabel;
	applyCaption(m_pill);
	row->addWidget(m_pill, 0, Qt::AlignVCenter);
	v->addLayout(row);

	m_busy = new QProgressBar;
	m_busy->setRange(0, 0);
	m_busy->setTextVisible(false);
	m_busy->setFixedHeight(2);
	m_busy->setVisible(false);
	m_busy->setStyleSheet(QStringLiteral("QProgressBar{border:none;background:transparent;}"
					     "QProgressBar::chunk{background:%1;}")
				      .arg(QString::fromLatin1(kBlue)));
	v->addWidget(m_busy);

	return w;
}

QWidget *KalturaDock::buildConnectPage()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(8);

	v->addWidget(sectionTitle(tr("Connect to Kaltura")));
	v->addWidget(caption(tr("A KS is safer than an admin secret: it expires. Nothing "
				"typed here is written to disk.")));

	v->addWidget(caption(tr("API host")));
	m_hostEdit = new QLineEdit(QStringLiteral("cdnapisec.kaltura.com"));
	v->addWidget(m_hostEdit);

	m_authMode = new QComboBox;
	m_authMode->addItem(tr("Paste a KS"));
	m_authMode->addItem(tr("Partner ID + admin secret"));
	v->addWidget(m_authMode);

	m_ksEdit = new QLineEdit;
	m_ksEdit->setPlaceholderText(QStringLiteral("djJ8..."));
	auto *ksPage = new QWidget;
	auto *ksL = new QVBoxLayout(ksPage);
	ksL->setContentsMargins(0, 0, 0, 0);
	ksL->addWidget(m_ksEdit);

	m_pidEdit = new QLineEdit;
	m_pidEdit->setPlaceholderText(tr("Partner ID"));
	m_secretEdit = new QLineEdit;
	m_secretEdit->setEchoMode(QLineEdit::Password);
	m_secretEdit->setPlaceholderText(tr("Admin secret"));
	auto *secretPage = new QWidget;
	auto *scL = new QVBoxLayout(secretPage);
	scL->setContentsMargins(0, 0, 0, 0);
	scL->setSpacing(6);
	scL->addWidget(m_pidEdit);
	scL->addWidget(m_secretEdit);

	m_authStack = new QStackedWidget;
	m_authStack->addWidget(ksPage);
	m_authStack->addWidget(secretPage);
	v->addWidget(m_authStack);

	auto *btnRow = new QHBoxLayout;
	btnRow->setContentsMargins(0, 0, 0, 0);
	btnRow->setSpacing(6);
	m_connectBtn = new QPushButton(tr("Connect"));
	m_connectBtn->setCursor(Qt::PointingHandCursor);
	m_connectBtn->setStyleSheet(primaryButtonQss(QString::fromLatin1(kBlue)));
	btnRow->addWidget(m_connectBtn, 1);
	m_disconnectBtn = new QPushButton(tr("Sign out"));
	m_disconnectBtn->setCursor(Qt::PointingHandCursor);
	m_disconnectBtn->setVisible(false);
	btnRow->addWidget(m_disconnectBtn, 0);
	v->addLayout(btnRow);

	m_connectStatus = new QLabel;
	m_connectStatus->setWordWrap(true);
	m_connectStatus->setVisible(false);
	v->addWidget(m_connectStatus);

	v->addStretch(1);

	connect(m_authMode, &QComboBox::currentIndexChanged, m_authStack,
		&QStackedWidget::setCurrentIndex);
	connect(m_connectBtn, &QPushButton::clicked, this, &KalturaDock::onConnectClicked);
	connect(m_disconnectBtn, &QPushButton::clicked, this, &KalturaDock::onDisconnectClicked);
	connect(m_ksEdit, &QLineEdit::returnPressed, this, &KalturaDock::onConnectClicked);
	connect(m_secretEdit, &QLineEdit::returnPressed, this, &KalturaDock::onConnectClicked);

	return w;
}

QWidget *KalturaDock::buildEntryPage()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(8);

	v->addWidget(sectionTitle(tr("Find a live entry")));
	v->addWidget(caption(tr("Search by name, or paste an entry ID for an exact match. "
				"Only live entries have ingest endpoints.")));

	auto *row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(6);
	m_queryEdit = new QLineEdit;
	m_queryEdit->setPlaceholderText(tr("entry ID or name"));
	m_queryEdit->setMinimumWidth(60);
	m_searchBtn = new QPushButton(tr("Search"));
	m_searchBtn->setCursor(Qt::PointingHandCursor);
	m_searchBtn->setStyleSheet(primaryButtonQss(QString::fromLatin1(kBlue)));
	row->addWidget(m_queryEdit, 1);
	row->addWidget(m_searchBtn, 0);
	v->addLayout(row);

	m_resultsList = new QListWidget;
	m_resultsList->setAlternatingRowColors(true);
	v->addWidget(m_resultsList, 1);

	m_entryStatus = new QLabel;
	m_entryStatus->setWordWrap(true);
	m_entryStatus->setVisible(false);
	v->addWidget(m_entryStatus);

	m_entryCard = new QLabel;
	m_entryCard->setWordWrap(true);
	m_entryCard->setTextFormat(Qt::RichText);
	m_entryCard->setVisible(false);
	m_entryCard->setStyleSheet(QStringLiteral(
		"background: rgba(127,127,127,22); border-radius:5px; padding:9px;"));
	v->addWidget(m_entryCard);

	connect(m_searchBtn, &QPushButton::clicked, this, &KalturaDock::onSearchClicked);
	connect(m_queryEdit, &QLineEdit::returnPressed, this, &KalturaDock::onSearchClicked);
	connect(m_resultsList, &QListWidget::currentRowChanged, this,
		&KalturaDock::onSearchResultActivated);

	return w;
}

QWidget *KalturaDock::buildSettingsPage()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(8);

	v->addWidget(sectionTitle(tr("Entry settings")));

	auto *vmRow = new QHBoxLayout;
	vmRow->setContentsMargins(0, 0, 0, 0);
	vmRow->setSpacing(6);
	m_viewModeLabel = new QLabel;
	m_viewModeLabel->setTextFormat(Qt::RichText);
	vmRow->addWidget(m_viewModeLabel, 1);
	m_viewModeBtn = new QPushButton(tr("Go live"));
	m_viewModeBtn->setCursor(Qt::PointingHandCursor);
	m_viewModeBtn->setStyleSheet(primaryButtonQss(QString::fromLatin1(kGreen)));
	vmRow->addWidget(m_viewModeBtn, 0);
	v->addLayout(vmRow);
	v->addWidget(caption(tr("Preview is visible to moderators only.")));

	v->addWidget(hLine());

	m_recordChk = new QCheckBox(tr("Record this stream"));
	v->addWidget(m_recordChk);

	m_dvrChk = new QCheckBox(tr("DVR"));
	v->addWidget(m_dvrChk);

	auto *dvrRow = new QHBoxLayout;
	dvrRow->setContentsMargins(20, 0, 0, 0);
	dvrRow->setSpacing(6);
	dvrRow->addWidget(caption(tr("window")), 0);
	m_dvrWindow = new QSpinBox;
	m_dvrWindow->setRange(1, 1440);
	m_dvrWindow->setValue(120);
	m_dvrWindow->setSuffix(tr(" min"));
	dvrRow->addWidget(m_dvrWindow, 1);
	v->addLayout(dvrRow);

	m_applyFeaturesBtn = new QPushButton(tr("No changes"));
	m_applyFeaturesBtn->setCursor(Qt::PointingHandCursor);
	m_applyFeaturesBtn->setEnabled(false);
	m_applyFeaturesBtn->setStyleSheet(primaryButtonQss(QString::fromLatin1(kBlue)));
	v->addWidget(m_applyFeaturesBtn);

	m_featureStatus = new QLabel;
	m_featureStatus->setWordWrap(true);
	m_featureStatus->setVisible(false);
	v->addWidget(m_featureStatus);

	v->addStretch(1);

	connect(m_viewModeBtn, &QPushButton::clicked, this, &KalturaDock::onToggleViewMode);
	connect(m_applyFeaturesBtn, &QPushButton::clicked, this, &KalturaDock::onApplyFeatures);

	auto dirty = [this]() {
		if (m_syncingFeatures || !m_haveEntry)
			return;
		m_dvrWindow->setEnabled(m_dvrChk->isChecked());
		const bool changed = (m_recordChk->isChecked() != (m_entry.recordStatus != 0)) ||
				     (m_dvrChk->isChecked() != (m_entry.dvrStatus != 0)) ||
				     (m_dvrChk->isChecked() &&
				      m_dvrWindow->value() != m_entry.dvrWindow);
		m_applyFeaturesBtn->setEnabled(changed);
		m_applyFeaturesBtn->setText(changed ? tr("Save changes") : tr("No changes"));
	};
	connect(m_recordChk, &QCheckBox::toggled, this, dirty);
	connect(m_dvrChk, &QCheckBox::toggled, this, dirty);
	connect(m_dvrWindow, &QSpinBox::valueChanged, this, dirty);

	return w;
}

QWidget *KalturaDock::buildStreamsPage()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(8);

	v->addWidget(sectionTitle(tr("Streams")));

	m_streamStatus = new QLabel;
	m_streamStatus->setWordWrap(true);
	m_streamStatus->setVisible(false);
	v->addWidget(m_streamStatus);

	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_streams = new StreamsPanel;
	scroll->setWidget(m_streams);
	v->addWidget(scroll, 1);

	return w;
}

QWidget *KalturaDock::buildMonitorPage()
{
	auto *w = new QWidget;
	auto *v = new QVBoxLayout(w);
	v->setContentsMargins(0, 4, 0, 0);
	v->setSpacing(8);

	v->addWidget(sectionTitle(tr("Encoder health")));
	m_healthLabel = new QLabel(tr("Not streaming."));
	m_healthLabel->setTextFormat(Qt::RichText);
	v->addWidget(m_healthLabel);

	v->addWidget(hLine());

	auto *diagRow = new QHBoxLayout;
	diagRow->setContentsMargins(0, 0, 0, 0);
	diagRow->addWidget(sectionTitle(tr("Last Kaltura response")), 1);
	auto *refresh = new QPushButton(tr("Refresh"));
	refresh->setCursor(Qt::PointingHandCursor);
	diagRow->addWidget(refresh, 0);
	v->addLayout(diagRow);

	m_rawView = new QPlainTextEdit;
	m_rawView->setReadOnly(true);
	m_rawView->setFont(scaledFont(m_rawView, 0.85));
	v->addWidget(m_rawView, 1);

	auto *player = new QPushButton(tr("Open playback manifest in browser"));
	player->setCursor(Qt::PointingHandCursor);
	v->addWidget(player);

	connect(refresh, &QPushButton::clicked, this, [this]() {
		m_rawView->setPlainText(
			kaltura::redactSecrets(QString::fromUtf8(m_client->lastRawResponse())));
	});
	connect(player, &QPushButton::clicked, this, &KalturaDock::onOpenPlayer);

	return w;
}

/* ------------------------------ navigation ------------------------------ */

void KalturaDock::goTo(int page)
{
	m_pages->setCurrentIndex(page);
	m_rail->setCurrent(page);
	updateNav();
}

void KalturaDock::updateNav()
{
	const int page = m_pages->currentIndex();
	const bool connected = m_client->isConnected();

	m_rail->setReachable(PageConnect, true);
	m_rail->setReachable(PageEntry, connected);
	m_rail->setReachable(PageSettings, connected && m_haveEntry);
	m_rail->setReachable(PageStreams, connected && m_haveEntry);
	m_rail->setReachable(PageMonitor, true);

	m_rail->setState(PageConnect,
			 connected ? StepRail::State::Done : StepRail::State::Pending);
	m_rail->setState(PageEntry,
			 m_haveEntry ? StepRail::State::Done : StepRail::State::Pending);

	m_backBtn->setEnabled(page > 0);

	bool canNext = false;
	switch (page) {
	case PageConnect:
		canNext = connected;
		break;
	case PageEntry:
		canNext = m_haveEntry;
		break;
	case PageSettings:
	case PageStreams:
		canNext = true;
		break;
	default:
		canNext = false;
	}
	m_nextBtn->setEnabled(canNext);
	m_nextBtn->setText(page == PageStreams ? tr("Monitor") : tr("Next"));
	m_nextBtn->setVisible(page != PageMonitor);
}

void KalturaDock::onBack()
{
	if (m_pages->currentIndex() > 0)
		goTo(m_pages->currentIndex() - 1);
}

void KalturaDock::onNext()
{
	if (m_pages->currentIndex() < m_pages->count() - 1)
		goTo(m_pages->currentIndex() + 1);
}

void KalturaDock::onStepClicked(int index)
{
	goTo(index);
}

/* -------------------------------- status -------------------------------- */

void KalturaDock::setConnectStatus(const QString &t, const QString &c)
{
	styleStatus(m_connectStatus, t, c);
}
void KalturaDock::setEntryStatus(const QString &t, const QString &c)
{
	styleStatus(m_entryStatus, t, c);
}
void KalturaDock::setStreamStatus(const QString &t, const QString &c)
{
	styleStatus(m_streamStatus, t, c);
}
void KalturaDock::setFeatureStatus(const QString &t, const QString &c)
{
	styleStatus(m_featureStatus, t, c);
}

void KalturaDock::setConnectionPill(bool connected, const QString &detail)
{
	m_pill->setText(QStringLiteral("<span style='color:%1'>&#9679;</span> %2")
				.arg(connected ? QString::fromLatin1(kGreen)
					       : QString::fromLatin1(kMuted),
				     detail));
}

void KalturaDock::onBusyChanged(bool busy)
{
	m_busy->setVisible(busy);
	m_connectBtn->setEnabled(!busy);
	m_searchBtn->setEnabled(!busy);
}

/* ------------------------------- connect -------------------------------- */

void KalturaDock::onConnectClicked()
{
	m_ctx = Ctx::Connect;
	m_client->setHost(m_hostEdit->text());
	setConnectStatus(tr("Contacting %1...").arg(m_client->host()),
			 QString::fromLatin1(kSky));

	if (m_authMode->currentIndex() == 1)
		m_client->startSession(m_pidEdit->text().trimmed(), m_secretEdit->text());
	else
		m_client->validateKs(m_ksEdit->text());
}

void KalturaDock::onDisconnectClicked()
{
	m_client->clearSession();
	m_ksEdit->clear();
	m_secretEdit->clear();
	m_haveEntry = false;
	m_streams->clearEntry();
	m_resultsList->clear();
	m_entryCard->setVisible(false);
	m_disconnectBtn->setVisible(false);
	setConnectStatus(tr("Signed out."), QString::fromLatin1(kMuted));
	setConnectionPill(false, tr("not connected"));
	goTo(PageConnect);
}

void KalturaDock::onSessionReady(int partnerId, const QString &userId, qint64 expiresAtUnix)
{
	Q_UNUSED(expiresAtUnix);
	QString msg = tr("Connected to partner %1").arg(partnerId);
	if (!userId.isEmpty())
		msg += QStringLiteral(" / %1").arg(userId);
	setConnectStatus(msg, QString::fromLatin1(kGreen));
	m_disconnectBtn->setVisible(true);

	tickSessionClock();
	goTo(PageEntry);
	m_queryEdit->setFocus();
}

void KalturaDock::tickSessionClock()
{
	if (!m_client->isConnected()) {
		setConnectionPill(false, tr("not connected"));
		return;
	}
	const qint64 exp = m_client->expiresAtUnix();
	if (exp <= 0) {
		setConnectionPill(true, tr("p%1").arg(m_client->partnerId()));
		return;
	}

	const qint64 mins = (exp * 1000 - QDateTime::currentMSecsSinceEpoch()) / 60000;
	if (mins <= 0) {
		setConnectionPill(false, tr("p%1 - KS EXPIRED").arg(m_client->partnerId()));
		m_rail->setState(PageConnect, StepRail::State::Error);
	} else {
		setConnectionPill(true,
				  tr("p%1 - %2m left").arg(m_client->partnerId()).arg(mins));
	}
}

/* -------------------------------- entry --------------------------------- */

void KalturaDock::onSearchClicked()
{
	if (!m_client->isConnected()) {
		setConnectStatus(tr("Connect first."), QString::fromLatin1(kRed));
		goTo(PageConnect);
		return;
	}
	const QString q = m_queryEdit->text().trimmed();
	if (q.isEmpty())
		return;

	m_ctx = Ctx::Entry;
	m_resultsList->clear();
	m_entryCard->setVisible(false);
	m_haveEntry = false;
	updateNav();
	setEntryStatus(tr("Searching for \"%1\"...").arg(q), QString::fromLatin1(kSky));
	m_client->searchEntries(q);
}

void KalturaDock::onSearchResults(const QVector<EntrySummary> &results)
{
	m_results = results;
	m_resultsList->clear();

	int liveCount = 0;
	for (const EntrySummary &s : results) {
		if (s.isLive())
			++liveCount;
		auto *item = new QListWidgetItem(
			QStringLiteral("%1  %2\n%3")
				.arg(s.isLive() ? QStringLiteral("[LIVE]")
						: QStringLiteral("[VOD]"),
				     s.name.isEmpty() ? tr("(no name)") : s.name, s.id));
		if (!s.isLive())
			item->setForeground(QColor(QString::fromLatin1(kMuted)));
		item->setToolTip(s.id);
		m_resultsList->addItem(item);
	}

	if (results.isEmpty()) {
		setEntryStatus(tr("Nothing matched. Try fewer words, or paste the entry ID."),
			       QString::fromLatin1(kAmber));
		return;
	}

	if (liveCount == 0)
		setEntryStatus(tr("%1 result(s), but none are live entries - a VOD entry has "
				  "no ingest endpoints.")
				       .arg(results.size()),
			       QString::fromLatin1(kAmber));
	else
		setEntryStatus(tr("%1 live result(s). Pick one to load.").arg(liveCount),
			       QString::fromLatin1(kMuted));
}

void KalturaDock::onSearchResultActivated(int row)
{
	if (row < 0 || row >= m_results.size())
		return;
	m_ctx = Ctx::Entry;
	const EntrySummary &s = m_results.at(row);
	setEntryStatus(tr("Loading %1...").arg(s.id), QString::fromLatin1(kSky));
	m_client->fetchLiveEntry(s.id);
}

void KalturaDock::onLiveEntryReady(const LiveEntry &entry)
{
	m_entry = entry;
	m_endpoints = kaltura::deriveEndpoints(entry);
	m_haveEntry = true;
	m_isLive = false;

	setEntryStatus(tr("Loaded %1").arg(entry.id), QString::fromLatin1(kGreen));
	syncFeatureControls();
	verifyWriteTookEffect();
	m_streams->setEntry(entry.id, m_endpoints);
	m_client->checkIsLive(entry.id);
	refreshEntryCard();
	updateNav();
}

void KalturaDock::onIsLiveResult(const QString &entryId, bool live)
{
	if (entryId != m_entry.id)
		return;
	m_isLive = live;
	refreshEntryCard();
}

void KalturaDock::refreshEntryCard()
{
	if (!m_haveEntry)
		return;

	QStringList warnings;
	if (m_endpoints.streamNameHadTemplate)
		warnings << tr("streamName had a %i placeholder - substituted 1");
	if (m_endpoints.streamNameWasUndefined)
		warnings << tr("streamName was the literal \"undefined\"");
	if (m_endpoints.srtDerived && !m_endpoints.srtHost.isEmpty())
		warnings << tr("SRT host derived from the RTMP URL");
	if (m_endpoints.rtmpPrimary.isEmpty())
		warnings << tr("no RTMP ingest URL on this entry");

	QString html =
		QStringLiteral("<b>%1</b><br/><span style='color:%2'>%3</span>"
			       "<br/>%4 &nbsp; %5<br/>DVR %6 &nbsp; REC %7")
			.arg(m_entry.name.toHtmlEscaped(), QString::fromLatin1(kSky), m_entry.id,
			     kaltura::entryTypeName(kaltura::detectEntryType(m_entry)),
			     m_isLive ? QStringLiteral("<span style='color:%1'>LIVE</span>")
						.arg(QString::fromLatin1(kGreen))
				      : tr("offline"),
			     m_entry.dvrStatus ? tr("%1 min").arg(m_entry.dvrWindow) : tr("off"),
			     m_entry.recordStatus ? tr("on") : tr("off"));

	if (!warnings.isEmpty())
		html += QStringLiteral("<br/><span style='color:%1'>&#9888; %2</span>")
				.arg(QString::fromLatin1(kAmber),
				     warnings.join(QStringLiteral("<br/>&#9888; ")));

	m_entryCard->setText(html);
	m_entryCard->setVisible(true);
}

/* ------------------------------- settings ------------------------------- */

void KalturaDock::syncFeatureControls()
{
	m_syncingFeatures = true;
	m_recordChk->setChecked(m_entry.recordStatus != 0);
	m_dvrChk->setChecked(m_entry.dvrStatus != 0);
	if (m_entry.dvrWindow > 0)
		m_dvrWindow->setValue(m_entry.dvrWindow);
	m_dvrWindow->setEnabled(m_entry.dvrStatus != 0);

	const bool live = (m_entry.viewMode == static_cast<int>(kaltura::ViewMode::Live));
	m_viewModeLabel->setText(QStringLiteral("view mode <b style='color:%1'>%2</b>")
					 .arg(live ? QString::fromLatin1(kGreen)
						   : QString::fromLatin1(kAmber),
					      live ? tr("LIVE") : tr("PREVIEW")));
	m_viewModeBtn->setText(live ? tr("Back to preview") : tr("Go live"));
	m_viewModeBtn->setStyleSheet(primaryButtonQss(live ? QString::fromLatin1(kAmber)
							   : QString::fromLatin1(kGreen)));

	m_applyFeaturesBtn->setEnabled(false);
	m_applyFeaturesBtn->setText(tr("No changes"));
	m_syncingFeatures = false;
}

void KalturaDock::verifyWriteTookEffect()
{
	if (!m_verifyPending && !m_verifyViewMode)
		return;

	QStringList rejected;
	if (m_verifyPending) {
		if ((m_entry.recordStatus != 0) != m_wantRecord)
			rejected << tr("recording (still %1)")
					    .arg(m_entry.recordStatus ? tr("on") : tr("off"));
		if ((m_entry.dvrStatus != 0) != m_wantDvr)
			rejected << tr("DVR (still %1)")
					    .arg(m_entry.dvrStatus ? tr("on") : tr("off"));
		else if (m_wantDvr && m_entry.dvrWindow != m_wantDvrWindow)
			rejected << tr("DVR window (still %1 min)").arg(m_entry.dvrWindow);
	}
	if (m_verifyViewMode && m_entry.viewMode != m_wantViewMode)
		rejected << tr("view mode (still %1)")
				    .arg(m_entry.viewMode ==
						 static_cast<int>(kaltura::ViewMode::Live)
					     ? tr("LIVE")
					     : tr("PREVIEW"));

	m_verifyPending = false;
	m_verifyViewMode = false;

	if (rejected.isEmpty()) {
		setFeatureStatus(tr("Saved and confirmed by Kaltura."),
				 QString::fromLatin1(kGreen));
		return;
	}

	setFeatureStatus(tr("Kaltura accepted the request but did not change: %1. "
			    "See the Monitor step for the raw response.")
				 .arg(rejected.join(QStringLiteral(", "))),
			 QString::fromLatin1(kAmber));
	if (m_rawView)
		m_rawView->setPlainText(
			kaltura::redactSecrets(QString::fromUtf8(m_client->lastRawResponse())));
}

void KalturaDock::onApplyFeatures()
{
	if (!m_haveEntry)
		return;
	m_ctx = Ctx::Features;
	m_verifyPending = true;
	m_wantRecord = m_recordChk->isChecked();
	m_wantDvr = m_dvrChk->isChecked();
	m_wantDvrWindow = m_dvrWindow->value();
	setFeatureStatus(tr("Saving..."), QString::fromLatin1(kSky));
	m_client->updateRecordingAndDvr(m_entry.id, m_wantRecord, m_wantDvr, m_wantDvrWindow);
}

void KalturaDock::onToggleViewMode()
{
	if (!m_haveEntry)
		return;
	const bool live = (m_entry.viewMode == static_cast<int>(kaltura::ViewMode::Live));
	const kaltura::ViewMode target =
		live ? kaltura::ViewMode::Preview : kaltura::ViewMode::Live;

	m_ctx = Ctx::Features;
	m_verifyViewMode = true;
	m_wantViewMode = static_cast<int>(target);
	setFeatureStatus(live ? tr("Switching to preview...") : tr("Going live..."),
			 QString::fromLatin1(kSky));
	m_client->updateViewMode(m_entry.id, target);
}

void KalturaDock::onEntryUpdated(const QString &what)
{
	setFeatureStatus(tr("Saved %1 - re-reading...").arg(what), QString::fromLatin1(kGreen));
}

void KalturaDock::onOpenPlayer()
{
	if (!m_haveEntry || !m_client->isConnected())
		return;
	QDesktopServices::openUrl(QUrl(kaltura::playbackManifestUrl(
		m_client->host(), m_client->partnerId(), m_entry.id)));
}

/* ------------------------------- failures ------------------------------- */

void KalturaDock::onFailed(const QString &message)
{
	switch (m_ctx) {
	case Ctx::Connect:
		setConnectStatus(message, QString::fromLatin1(kRed));
		m_rail->setState(PageConnect, StepRail::State::Error);
		goTo(PageConnect);
		break;
	case Ctx::Entry:
		setEntryStatus(message, QString::fromLatin1(kRed));
		m_rail->setState(PageEntry, StepRail::State::Error);
		break;
	case Ctx::Features:
		setFeatureStatus(message, QString::fromLatin1(kRed));
		syncFeatureControls();
		break;
	default:
		setStreamStatus(message, QString::fromLatin1(kRed));
		break;
	}

	m_verifyPending = false;
	m_verifyViewMode = false;
	if (m_rawView)
		m_rawView->setPlainText(
			kaltura::redactSecrets(QString::fromUtf8(m_client->lastRawResponse())));

	obs_log(LOG_WARNING, "%s", message.toUtf8().constData());
}

void KalturaDock::onHealthSample(const obsbridge::HealthSample &s)
{
	if (!s.streaming) {
		m_healthLabel->setText(
			QStringLiteral("<span style='color:%1'>Not streaming.</span>")
				.arg(QString::fromLatin1(kMuted)));
		return;
	}

	const auto row = [](const QString &name, double v, const QString &unit,
			    obsbridge::Severity sev) {
		return QStringLiteral("<tr><td style='color:%1'>&#9679;</td>"
				      "<td style='color:%2;padding-right:10px'>%3</td>"
				      "<td align='right'><b>%4</b>%5</td></tr>")
			.arg(severityColour(sev), QString::fromLatin1(kMuted), name,
			     QString::number(v, 'f', 2), unit);
	};

	/* dropped = network, skipped = encoder, lagged = rendering. Never merged. */
	m_healthLabel->setText(
		QStringLiteral("<table cellspacing='0' cellpadding='2' width='100%'>") +
		row(tr("dropped &middot; network"), s.droppedPercent, QStringLiteral("%"),
		    obsbridge::gradeDropped(s.droppedPercent)) +
		row(tr("skipped &middot; encoder"), s.skippedPercent, QStringLiteral("%"),
		    obsbridge::gradeSkippedOrLagged(s.skippedPercent)) +
		row(tr("lagged &middot; render"), s.laggedPercent, QStringLiteral("%"),
		    obsbridge::gradeSkippedOrLagged(s.laggedPercent)) +
		row(tr("congestion"), s.congestion, QString(),
		    obsbridge::gradeCongestion(s.congestion)) +
		QStringLiteral(
			"</table><div style='color:%1'>%2 kbps &middot; connect %3 ms</div>")
			.arg(QString::fromLatin1(kMuted), QString::number(s.sendKbps, 'f', 0))
			.arg(s.connectTimeMs));
}
