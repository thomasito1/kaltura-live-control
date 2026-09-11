/*
 * One list, every stream.
 *
 * Kaltura Live+ sends each track as its own RTMP connection to a shared ingest
 * prefix, told apart by stream name: video is "1", English "1000", Spanish "1001".
 * So the natural UI is a flat list of streams rather than separate "endpoints" and
 * "audio tracks" panels - each row is one stream, with a start/stop and a settings
 * drawer that opens in place.
 *
 *   Video      key 1      -> OBS's own streaming output (started via the frontend API)
 *   Backup     key 1      -> our own output on the .b host, needs OBS streaming
 *   <Language> key 1000+  -> our own AUDIO-ONLY output bound to one OBS mixer track
 */
#pragma once

#include <QVector>
#include <QWidget>

#include "kaltura/Endpoints.hpp"
#include "obs/MultiOutput.hpp"

class QPushButton;
class QTimer;
class QVBoxLayout;

class StreamRow;

class StreamsPanel : public QWidget {
	Q_OBJECT

public:
	explicit StreamsPanel(QWidget *parent = nullptr);

	void setEntry(const QString &entryId, const kaltura::Endpoints &endpoints);
	void clearEntry();
	bool anyActive() const;

signals:
	void statusMessage(const QString &text, bool isError);

private:
	void addAudioRow(int languageIndex, int mixerTrack);
	void refresh();

	QString m_entryId;
	kaltura::Endpoints m_endpoints;
	bool m_haveEntry = false;

	StreamRow *m_videoRow = nullptr;
	StreamRow *m_backupRow = nullptr;
	QVBoxLayout *m_audioRows = nullptr;
	QVector<StreamRow *> m_rows;
	QPushButton *m_addBtn = nullptr;
	QTimer *m_poll = nullptr;
};
