/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM.
 *
 *    FOOM is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    FOOM is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "shortcut_settings.h"

#include <algorithm>

#include <QAction>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QStringList>
#include <QVariant>


namespace OpenOrienteering {

namespace {

QString key(const QByteArray& id)
{
	return ShortcutSettings::group() + QLatin1Char('/') + QString::fromLatin1(id);
}

}  // namespace


QString ShortcutSettings::group()
{
	return QStringLiteral("Shortcuts");
}

bool ShortcutSettings::load(QSettings& settings, const QByteArray& id, QKeySequence& out)
{
	auto const value = settings.value(key(id));
	if (!value.isValid())
		return false;
	out = QKeySequence(value.toString(), QKeySequence::PortableText);
	return true;
}

void ShortcutSettings::store(QSettings& settings, const QByteArray& id, const QKeySequence& sequence)
{
	settings.setValue(key(id), sequence.toString(QKeySequence::PortableText));
}

void ShortcutSettings::remove(QSettings& settings, const QByteArray& id)
{
	settings.remove(key(id));
}

void ShortcutSettings::removeAll(QSettings& settings)
{
	settings.remove(group());
}

int ShortcutSettings::apply(QSettings& settings, const Actions& actions)
{
	auto count = 0;
	for (auto it = actions.constBegin(); it != actions.constEnd(); ++it)
	{
		QKeySequence sequence;
		if (it.key().isEmpty() || !it.value() || !load(settings, it.key(), sequence))
			continue;
		it.value()->setShortcut(sequence);
		++count;
	}
	return count;
}

QAction* ShortcutSettings::conflict(const Actions& actions, const QKeySequence& sequence, const QAction* except)
{
	if (sequence.isEmpty())
		return nullptr;
	for (auto it = actions.constBegin(); it != actions.constEnd(); ++it)
	{
		auto* action = it.value();
		if (action && action != except && !it.key().isEmpty() && action->shortcut() == sequence)
			return action;
	}
	return nullptr;
}

QByteArray ShortcutSettings::toJson(const Actions& actions)
{
	QJsonObject shortcuts;
	auto ids = actions.keys();
	std::sort(ids.begin(), ids.end());
	for (auto const& id : ids)
	{
		auto const* action = actions.value(id);
		if (!id.isEmpty() && action)
			shortcuts.insert(QString::fromLatin1(id), action->shortcut().toString(QKeySequence::PortableText));
	}
	QJsonObject document;
	document.insert(QStringLiteral("shortcuts"), shortcuts);
	return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

int ShortcutSettings::fromJson(QSettings& settings, const QByteArray& json)
{
	auto const document = QJsonDocument::fromJson(json);
	auto const shortcuts = document.object().value(QStringLiteral("shortcuts"));
	if (!shortcuts.isObject())
		return -1;
	auto count = 0;
	auto const object = shortcuts.toObject();
	for (auto it = object.constBegin(); it != object.constEnd(); ++it)
	{
		if (!it.value().isString())
			continue;
		store(settings, it.key().toLatin1(), QKeySequence(it.value().toString(), QKeySequence::PortableText));
		++count;
	}
	return count;
}


}  // namespace OpenOrienteering
