#include "AudioTracks.hpp"

namespace kaltura {

const QVector<AudioLanguage> &audioLanguages()
{
	static const QVector<AudioLanguage> table = {
		{"English", "en", 1000},    {"Spanish", "es", 1001},
		{"French", "fr", 1002},     {"German", "de", 1003},
		{"Portuguese", "pt", 1004}, {"Chinese", "zh", 1005},
		{"Arabic", "ar", 1006},     {"Hindi", "hi", 1007},
		{"Russian", "ru", 1008},    {"Japanese", "ja", 1009},
		{"Finnish", "fi", 1010},    {"Swedish", "sv", 1011},
		{"Korean", "ko", 1012},     {"Turkish", "tr", 1013},
		{"Polish", "pl", 1014},     {"Italian", "it", 1015},
		{"Ukrainian", "uk", 1016},  {"Kiswahili", "sw", 1017},
	};
	return table;
}

int streamNameForLanguage(const QString &displayName)
{
	for (const AudioLanguage &l : audioLanguages()) {
		if (displayName.compare(QLatin1String(l.name), Qt::CaseInsensitive) == 0)
			return l.streamName;
	}
	return -1;
}

} // namespace kaltura
