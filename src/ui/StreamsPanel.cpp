#include "StreamsPanel.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "kaltura/AudioTracks.hpp"
#include "obs/StreamService.hpp"

namespace {

const char *kBlue = "#006EFA";
const char *kSky = "#41BEFF";
const char *kGreen = "#5BC686";
const char *kAmber = "#FFAA00";
const char *kRed = "#FF3D23";
const char *kMuted = "#9aa3b2";

/* Sizes derive from the widget's own font; OBS themes set their own base font. */
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

QString dot(const QString &colour)
{
	return QStringLiteral("<span style='color:%1;font-size:14px'>&#9679;</span>").arg(colour);
}

QLabel *caption(const QString &t)
{
	auto *l = new QLabel(t);
	l->setFont(scaledFont(l, 0.88));
	l->setStyleSheet(QStringLiteral("color:%1;").arg(QString::fromLatin1(kMuted)));
	return l;
}

} // namespace

/* =========================================================== StreamRow === */

class StreamRow : public QWidget {
public:
	enum class Kind { MainVideo, Backup, Audio };

	StreamRow(Kind kind, QWidget *parent = nullptr) : QWidget(parent), m_kind(kind)
	{
		setObjectName(QStringLiteral("streamRow"));
		setStyleSheet(QStringLiteral(
			"#streamRow { background: rgba(127,127,127,18); border-radius: 6px; }"));

		auto *v = new QVBoxLayout(this);
		v->setContentsMargins(10, 8, 10, 8);
		v->setSpacing(6);

		/* ---- header line: status, title, key, buttons ---- */
		auto *head = new QHBoxLayout;
		head->setContentsMargins(0, 0, 0, 0);
		head->setSpacing(8);

		m_dot = new QLabel;
		m_dot->setTextFormat(Qt::RichText);
		m_dot->setFixedWidth(14);
		head->addWidget(m_dot, 0);

		m_title = new QLabel(defaultTitle());
		m_title->setStyleSheet(QStringLiteral(" font-weight:600;"));
		head->addWidget(m_title, 1);

		m_keyBadge = new QLabel;
		m_keyBadge->setFont(scaledFont(this, 0.82));
		m_keyBadge->setStyleSheet(
			QStringLiteral("color:%1; font-family:Consolas,monospace;""background:rgba(127,127,127,35); border-radius:3px;""padding:2px 6px;")
				.arg(QString::fromLatin1(kSky)));
		head->addWidget(m_keyBadge, 0);

		m_toggle = new QPushButton(tr("Start"));
		m_toggle->setCursor(Qt::PointingHandCursor);
		m_toggle->setFixedWidth(62);
		setToggleStyle(false);
		head->addWidget(m_toggle, 0);

		m_gear = new QToolButton;
		m_gear->setText(QStringLiteral("⋯")); /* midline ellipsis */
		m_gear->setCheckable(true);
		m_gear->setCursor(Qt::PointingHandCursor);
		m_gear->setToolTip(tr("Settings"));
		m_gear->setStyleSheet(QStringLiteral(
			"QToolButton{border:none;color:%1;padding:0 4px;}""QToolButton:checked{color:%2;}")
					      .arg(QString::fromLatin1(kMuted),
						   QString::fromLatin1(kSky)));
		head->addWidget(m_gear, 0);

		v->addLayout(head);

		m_status = new QLabel;
		m_status->setFont(scaledFont(this, 0.85));
		m_status->setStyleSheet(
			QStringLiteral("color:%1;").arg(QString::fromLatin1(kMuted)));
		v->addWidget(m_status);

		/* ---- settings drawer, hidden until the ... is pressed ---- */
		m_settings = new QWidget;
		auto *sv = new QVBoxLayout(m_settings);
		sv->setContentsMargins(0, 4, 0, 0);
		sv->setSpacing(6);

		auto *line = new QFrame;
		line->setFrameShape(QFrame::HLine);
		line->setStyleSheet(QStringLiteral("color:rgba(127,127,127,55);"));
		sv->addWidget(line);

		if (kind == Kind::Audio) {
			auto *r1 = new QHBoxLayout;
			r1->setSpacing(6);
			r1->addWidget(caption(tr("Language")), 0);
			m_language = new QComboBox;
			for (const kaltura::AudioLanguage &l : kaltura::audioLanguages())
				m_language->addItem(QString::fromLatin1(l.name), l.streamName);
			r1->addWidget(m_language, 1);
			sv->addLayout(r1);

			auto *r2 = new QHBoxLayout;
			r2->setSpacing(6);
			r2->addWidget(caption(tr("OBS audio track")), 0);
			m_track = new QSpinBox;
			m_track->setRange(1, 6);
			r2->addWidget(m_track, 1);
			sv->addLayout(r2);

			auto *r3 = new QHBoxLayout;
			r3->setSpacing(6);
			r3->addWidget(caption(tr("Bitrate")), 0);
			m_bitrate = new QSpinBox;
			m_bitrate->setRange(32, 512);
			m_bitrate->setSingleStep(32);
			m_bitrate->setValue(128);
			m_bitrate->setSuffix(tr(" kbps"));
			r3->addWidget(m_bitrate, 1);
			sv->addLayout(r3);
		}

		if (kind == Kind::MainVideo) {
			m_strip = new QCheckBox(tr("Strip URL to ?t= and force key 1"));
			m_strip->setStyleSheet(QStringLiteral(""));
			sv->addWidget(m_strip);
		}

		auto *urlRow = new QHBoxLayout;
		urlRow->setSpacing(6);
		m_url = new QLabel(QStringLiteral("-"));
		m_url->setWordWrap(true);
		m_url->setMinimumWidth(60);
		m_url->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_url->setFont(scaledFont(this, 0.82));
		m_url->setStyleSheet(QStringLiteral(
			"font-family:Consolas,monospace;""background:rgba(127,127,127,28); border-radius:3px; padding:5px 6px;"));
		urlRow->addWidget(m_url, 1);
		auto *copy = new QPushButton(tr("Copy"));
		copy->setFixedWidth(48);
		copy->setCursor(Qt::PointingHandCursor);
		copy->setStyleSheet(QStringLiteral("padding:4px;"));
		urlRow->addWidget(copy, 0, Qt::AlignTop);
		sv->addLayout(urlRow);

		connect(copy, &QPushButton::clicked, this, [this]() {
			QApplication::clipboard()->setText(m_server + QStringLiteral("  key=") +
							   streamKey());
		});

		m_settings->setVisible(false);
		v->addWidget(m_settings);

		connect(m_gear, &QToolButton::toggled, m_settings, &QWidget::setVisible);
		if (m_language)
			connect(m_language, &QComboBox::currentIndexChanged, this,
				[this](int) { refreshChrome(); });
		if (m_strip)
			connect(m_strip, &QCheckBox::toggled, this, [this](bool) { refreshChrome(); });

		refreshChrome();
	}

	~StreamRow() override { delete m_dest; }

	Kind kind() const { return m_kind; }

	QString streamKey() const
	{
		if (m_kind == Kind::Audio)
			return QString::number(m_language->currentData().toInt());
		if (m_kind == Kind::MainVideo && m_strip && m_strip->isChecked())
			return QStringLiteral("1");
		return m_fallbackKey.isEmpty() ? kaltura::videoStreamName() : m_fallbackKey;
	}

	QString title() const
	{
		return m_kind == Kind::Audio ? m_language->currentText() : defaultTitle();
	}

	bool isActive() const
	{
		if (m_kind == Kind::MainVideo)
			return obsbridge::isStreamingActive();
		return m_dest && m_dest->isActive();
	}

	void setEndpoints(const QString &server, const QString &strippedServer,
			  const QString &derivedKey)
	{
		m_server = server;
		m_serverStripped = strippedServer;
		m_fallbackKey = derivedKey;
		refreshChrome();
	}

	QPushButton *toggleButton() const { return m_toggle; }

	/* Empty string on success. */
	QString toggle()
	{
		if (m_kind == Kind::MainVideo)
			return toggleMainVideo();

		if (isActive()) {
			m_dest->stop();
			lockSettings(false);
			return QString();
		}

		obsbridge::DestinationSpec spec;
		spec.label = title();
		spec.server = effectiveServer();
		spec.streamKey = streamKey();
		spec.audioOnly = (m_kind == Kind::Audio);
		spec.mixerTrack = m_track ? m_track->value() - 1 : 0;
		spec.audioBitrateKbps = m_bitrate ? m_bitrate->value() : 128;

		if (!m_dest)
			m_dest = new obsbridge::Destination(spec);
		else
			m_dest->setSpec(spec);

		QString error;
		if (!m_dest->start(&error))
			return error;

		lockSettings(true);
		return QString();
	}

	void refreshChrome()
	{
		m_title->setText(title());
		m_keyBadge->setText(streamKey());
		m_url->setText(effectiveServer().isEmpty() ? QStringLiteral("-")
							   : effectiveServer());

		const bool active = isActive();
		setToggleStyle(active);
		m_toggle->setText(active ? tr("Stop") : tr("Start"));

		QString colour = QString::fromLatin1(kMuted);
		QString text;

		if (m_kind == Kind::MainVideo) {
			colour = active ? QString::fromLatin1(kGreen) : QString::fromLatin1(kMuted);
			text = active ? tr("streaming from OBS")
				      : tr("OBS main output  ·  video + main audio");
		} else if (m_dest) {
			const obsbridge::DestinationStatus s = m_dest->status();
			if (s.active) {
				colour = s.reconnecting ? QString::fromLatin1(kAmber)
							: QString::fromLatin1(kGreen);
				text = s.reconnecting ? tr("reconnecting") : tr("live");
				text += tr("  ·  %1 kbps").arg(QString::number(s.sentKbps, 'f', 0));
				if (s.droppedFrames > 0)
					text += tr("  ·  %1 dropped").arg(s.droppedFrames);
			} else if (!s.lastError.isEmpty()) {
				colour = QString::fromLatin1(kRed);
				text = s.lastError;
			} else {
				text = detailLine();
			}
		} else {
			text = detailLine();
		}

		m_dot->setText(dot(colour));
		m_status->setText(text);
		m_status->setStyleSheet(
			QStringLiteral("color:%1;").arg(colour));

		if (!active)
			lockSettings(false);
	}

private:
	QString defaultTitle() const
	{
		switch (m_kind) {
		case Kind::MainVideo:
			return tr("Video");
		case Kind::Backup:
			return tr("Backup video");
		default:
			return tr("Audio");
		}
	}

	QString detailLine() const
	{
		if (m_kind == Kind::Backup)
			return tr("backup ingest  ·  needs OBS streaming");
		if (m_kind == Kind::Audio)
			return tr("audio only  ·  OBS track %1").arg(m_track ? m_track->value() : 1);
		return QString();
	}

	QString effectiveServer() const
	{
		if (m_kind == Kind::MainVideo && m_strip && m_strip->isChecked())
			return m_serverStripped;
		return m_server;
	}

	QString toggleMainVideo()
	{
		if (obsbridge::isStreamingActive()) {
			obsbridge::stopMainStream();
			return QString();
		}

		QString error;
		if (!obsbridge::applyCustomService(effectiveServer(), streamKey(), &error))
			return error;

		obsbridge::startMainStream();
		return QString();
	}

	void setToggleStyle(bool active)
	{
		m_toggle->setStyleSheet(
			QStringLiteral("QPushButton{background:%1;color:#fff;border:none;""border-radius:4px;padding:5px;""font-weight:600;}""QPushButton:hover{filter:brightness(1.1);}")
				.arg(QString::fromLatin1(active ? kRed : kBlue)));
	}

	void lockSettings(bool locked)
	{
		if (m_language)
			m_language->setEnabled(!locked);
		if (m_track)
			m_track->setEnabled(!locked);
		if (m_bitrate)
			m_bitrate->setEnabled(!locked);
		if (m_strip)
			m_strip->setEnabled(!locked);
	}

	Kind m_kind;
	QString m_server, m_serverStripped, m_fallbackKey;

	QLabel *m_dot = nullptr;
	QLabel *m_title = nullptr;
	QLabel *m_keyBadge = nullptr;
	QLabel *m_status = nullptr;
	QLabel *m_url = nullptr;
	QPushButton *m_toggle = nullptr;
	QToolButton *m_gear = nullptr;
	QWidget *m_settings = nullptr;

	QComboBox *m_language = nullptr;
	QSpinBox *m_track = nullptr;
	QSpinBox *m_bitrate = nullptr;
	QCheckBox *m_strip = nullptr;

	obsbridge::Destination *m_dest = nullptr;
};

/* ======================================================== StreamsPanel === */

StreamsPanel::StreamsPanel(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(0, 0, 0, 8);
	v->setSpacing(6);

	m_videoRow = new StreamRow(StreamRow::Kind::MainVideo, this);
	v->addWidget(m_videoRow);

	m_backupRow = new StreamRow(StreamRow::Kind::Backup, this);
	v->addWidget(m_backupRow);

	m_audioRows = new QVBoxLayout;
	m_audioRows->setContentsMargins(0, 0, 0, 0);
	m_audioRows->setSpacing(6);
	v->addLayout(m_audioRows);

	m_addBtn = new QPushButton(tr("+  Add audio language"));
	m_addBtn->setCursor(Qt::PointingHandCursor);
	m_addBtn->setStyleSheet(
		QStringLiteral("QPushButton{background:transparent;border:1px dashed ""rgba(127,127,127,80);border-radius:6px;padding:8px;""color:%1;}""QPushButton:hover{border-color:%2;color:%2;}")
			.arg(QString::fromLatin1(kMuted), QString::fromLatin1(kSky)));
	v->addWidget(m_addBtn);

	auto *note = caption(tr("Language streams are audio-only: Kaltura discards audio ""interleaved with video. Needs Live+ with audio flavors."));
	note->setWordWrap(true);
	v->addWidget(note);

	for (StreamRow *r : {m_videoRow, m_backupRow})
		connect(r->toggleButton(), &QPushButton::clicked, this, [this, r]() {
			const QString err = r->toggle();
			if (!err.isEmpty())
				emit statusMessage(err, true);
			r->refreshChrome();
		});

	connect(m_addBtn, &QPushButton::clicked, this,
		[this]() { addAudioRow(m_rows.size(), m_rows.size() + 1); });

	m_poll = new QTimer(this);
	connect(m_poll, &QTimer::timeout, this, &StreamsPanel::refresh);
	m_poll->start(1000);

	setEnabled(false);
}

void StreamsPanel::addAudioRow(int languageIndex, int mixerTrack)
{
	auto *row = new StreamRow(StreamRow::Kind::Audio, this);
	row->setEndpoints(m_endpoints.rtmpPrimary, m_endpoints.rtmpPrimaryStripped,
			  QString());

	if (auto *combo = row->findChild<QComboBox *>())
		combo->setCurrentIndex(
			qBound(0, languageIndex, kaltura::audioLanguages().size() - 1));
	if (auto *spin = row->findChild<QSpinBox *>())
		spin->setValue(qBound(1, mixerTrack, 6));

	connect(row->toggleButton(), &QPushButton::clicked, this, [this, row]() {
		const QString err = row->toggle();
		if (!err.isEmpty())
			emit statusMessage(err, true);
		row->refreshChrome();
	});

	m_rows.append(row);
	m_audioRows->addWidget(row);
	row->refreshChrome();
}

void StreamsPanel::setEntry(const QString &entryId, const kaltura::Endpoints &endpoints)
{
	m_entryId = entryId;
	m_endpoints = endpoints;
	m_haveEntry = true;
	setEnabled(true);

	m_videoRow->setEndpoints(endpoints.rtmpPrimary, endpoints.rtmpPrimaryStripped,
				 endpoints.streamKey);
	m_backupRow->setEndpoints(endpoints.rtmpSecondary, endpoints.rtmpSecondaryStripped,
				  endpoints.streamKey);
	for (StreamRow *r : m_rows)
		r->setEndpoints(endpoints.rtmpPrimary, endpoints.rtmpPrimaryStripped, QString());

	if (m_rows.isEmpty())
		addAudioRow(0, 1); /* English on track 1 */

	refresh();
}

void StreamsPanel::clearEntry()
{
	m_haveEntry = false;
	setEnabled(false);
}

bool StreamsPanel::anyActive() const
{
	if (m_backupRow && m_backupRow->isActive())
		return true;
	for (StreamRow *r : m_rows)
		if (r->isActive())
			return true;
	return false;
}

void StreamsPanel::refresh()
{
	if (m_videoRow)
		m_videoRow->refreshChrome();
	if (m_backupRow)
		m_backupRow->refreshChrome();
	for (StreamRow *r : m_rows)
		r->refreshChrome();
}
